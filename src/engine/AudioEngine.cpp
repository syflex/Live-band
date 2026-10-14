#include "engine/AudioEngine.h"

#include "common/RtAllocGuard.h"

#include <algorithm>
#include <cmath>

namespace lb
{

bool AudioEngine::loadInstrument(const juce::File& sfzFile)
{
    const bool ok = instrument_.load(sfzFile.getFullPathName().toStdString());
    instrumentName_ = ok ? sfzFile.getFileName() : juce::String();
    return ok;
}

void AudioEngine::noteOn(int note, int velocity) noexcept
{
    pushNoteEvent({ NoteEvent::Type::on, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity) });
}

void AudioEngine::noteOff(int note) noexcept
{
    pushNoteEvent({ NoteEvent::Type::off, static_cast<std::uint8_t>(note), 0 });
}

void AudioEngine::allNotesOff() noexcept
{
    pushNoteEvent({ NoteEvent::Type::allOff, 0, 0 });
}

void AudioEngine::pushNoteEvent(NoteEvent event) noexcept
{
    if (!noteEvents_.push(event))
        droppedNoteEvents_.fetch_add(1, std::memory_order_relaxed);
}

AudioEngine::DeviceInfo AudioEngine::deviceInfo() const noexcept
{
    return { infoSampleRate_.load(), infoBufferSize_.load(), infoInputLatency_.load(), infoOutputLatency_.load() };
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate_ = device->getCurrentSampleRate();
    maxBlockSize_ = std::max(device->getCurrentBufferSizeSamples(), 1);

    instrumentBuffer_.setSize(2, maxBlockSize_);
    probeBuffer_.setSize(1, maxBlockSize_);
    instrument_.prepare(sampleRate_, maxBlockSize_);
    probe_.prepare(sampleRate_);
    currentMonitorGain_ = 0.0f;

    infoSampleRate_.store(sampleRate_);
    infoBufferSize_.store(device->getCurrentBufferSizeSamples());
    infoInputLatency_.store(device->getInputLatencyInSamples());
    infoOutputLatency_.store(device->getOutputLatencyInSamples());
}

void AudioEngine::audioDeviceStopped()
{
    instrument_.allNotesOff();
    infoSampleRate_.store(0.0);
    infoBufferSize_.store(0);
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                                   float* const* outputs, int numOutputs, int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    const rt::ScopedRealtimeSection realtimeSection;
    const juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputs[ch], numSamples);

    if (maxBlockSize_ <= 0 || sampleRate_ <= 0.0)
        return;

    // The voice comes from the first active input channel.
    const float* mic = nullptr;
    for (int ch = 0; ch < numInputs && mic == nullptr; ++ch)
        mic = inputs[ch];

    if (mic != nullptr)
        raisePeak(inputPeak_, mic, numSamples);

    applyNoteEvents();

    // Drivers normally stay within the buffer size they reported, but split larger
    // callbacks so the preallocated buffers are never overrun.
    for (int offset = 0; offset < numSamples;)
    {
        const int chunk = std::min(numSamples - offset, maxBlockSize_);
        processChunk(mic != nullptr ? mic + offset : nullptr, outputs, numOutputs, offset, chunk);
        offset += chunk;
    }

    if (numOutputs > 0 && outputs[0] != nullptr)
        raisePeak(outputPeak_, outputs[0], numSamples);

    activeVoices_.store(instrument_.activeVoices(), std::memory_order_relaxed);

    const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - startTicks);
    stats_.record(elapsed, numSamples / sampleRate_);
}

void AudioEngine::processChunk(const float* mic, float* const* outputs, int numOutputs, int offset, int numSamples) noexcept
{
    // While the latency probe runs, it owns the outputs: no voice, no instrument.
    float* probeSignal = probeBuffer_.getWritePointer(0);
    if (probe_.process(mic, probeSignal, numSamples))
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::copy(outputs[ch] + offset, probeSignal, numSamples);
        return;
    }

    // Voice pass-through, ramped so that switching it on or off does not click.
    const float targetGain = monitorEnabled_.load(std::memory_order_relaxed) ? monitorGain_.load(std::memory_order_relaxed) : 0.0f;
    if (mic != nullptr && (targetGain > 0.0f || currentMonitorGain_ > 0.0f))
    {
        const float step = (targetGain - currentMonitorGain_) / static_cast<float>(numSamples);
        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr)
                continue;

            float gain = currentMonitorGain_;
            float* out = outputs[ch] + offset;
            for (int i = 0; i < numSamples; ++i)
            {
                gain += step;
                out[i] += mic[i] * gain;
            }
        }
    }
    currentMonitorGain_ = targetGain;

    // Test instrument.
    float* left = instrumentBuffer_.getWritePointer(0);
    float* right = instrumentBuffer_.getWritePointer(1);
    instrument_.render(left, right, numSamples);

    if (numOutputs >= 2)
    {
        if (outputs[0] != nullptr)
            juce::FloatVectorOperations::add(outputs[0] + offset, left, numSamples);
        if (outputs[1] != nullptr)
            juce::FloatVectorOperations::add(outputs[1] + offset, right, numSamples);
    }
    else if (numOutputs == 1 && outputs[0] != nullptr)
    {
        juce::FloatVectorOperations::addWithMultiply(outputs[0] + offset, left, 0.5f, numSamples);
        juce::FloatVectorOperations::addWithMultiply(outputs[0] + offset, right, 0.5f, numSamples);
    }
}

void AudioEngine::applyNoteEvents() noexcept
{
    NoteEvent event;
    while (noteEvents_.pop(event))
    {
        switch (event.type)
        {
            case NoteEvent::Type::on:     instrument_.noteOn(0, event.note, event.velocity); break;
            case NoteEvent::Type::off:    instrument_.noteOff(0, event.note); break;
            case NoteEvent::Type::allOff: instrument_.allNotesOff(); break;
        }
    }
}

void AudioEngine::raisePeak(std::atomic<float>& peak, const float* samples, int numSamples) noexcept
{
    const auto range = juce::FloatVectorOperations::findMinAndMax(samples, numSamples);
    const float blockPeak = std::max(std::abs(range.getStart()), std::abs(range.getEnd()));

    float current = peak.load(std::memory_order_relaxed);
    while (blockPeak > current && !peak.compare_exchange_weak(current, blockPeak, std::memory_order_relaxed))
    {
    }
}

} // namespace lb
