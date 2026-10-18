#include "engine/AudioEngine.h"

#include "common/RtAllocGuard.h"

#include <algorithm>
#include <cmath>

namespace lb
{
namespace
{
enum ScratchChannel
{
    probeChannel,
    clickChannel,
    fileChannel,
    silenceChannel,
    numScratchChannels
};
} // namespace

double AudioEngine::systemClock() noexcept
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

bool AudioEngine::loadInstrument(const juce::File& sfzFile)
{
    const bool ok = instrument_.load(sfzFile.getFullPathName().toStdString());
    instrumentName_ = ok ? sfzFile.getFileName() : juce::String();
    return ok;
}

const char* AudioEngine::partName(BandPart part) noexcept
{
    switch (part)
    {
        case BandPart::drums: return "drums";
        case BandPart::bass:  return "bass";
        case BandPart::keys:  return "keys";
        case BandPart::pad:   return "pad";
    }
    return "";
}

AudioEngine::BandPart AudioEngine::partForLayer(const std::string& layerName) noexcept
{
    for (const auto part : { BandPart::bass, BandPart::keys, BandPart::pad })
        if (layerName == partName(part))
            return part;
    return BandPart::drums;
}

bool AudioEngine::loadBandInstrument(BandPart part, const juce::File& sfzFile)
{
    auto& p = parts_[static_cast<std::size_t>(part)];
    const bool ok = p.instrument.load(sfzFile.getFullPathName().toStdString());
    p.name = ok ? sfzFile.getFileNameWithoutExtension() : juce::String();
    return ok;
}

void AudioEngine::setStyle(std::unique_ptr<Style> style)
{
    style_ = style.get();
    if (style != nullptr)
        styles_.push_back(std::move(style));
    Command command;
    command.type = Command::Type::setStyle;
    command.style = style_;
    if (style_ != nullptr)
        for (std::size_t i = 0; i < style_->layers.size() && i < command.routes.size(); ++i)
            command.routes[i] = partForLayer(style_->layers[i]);
    pushCommand(command);
}

void AudioEngine::startBand(std::int64_t downbeatSample, double samplesPerBeat, int section, int countInBars, Key key) noexcept
{
    Command command;
    command.type = Command::Type::startBand;
    command.sample = downbeatSample;
    command.samplesPerBeat = samplesPerBeat;
    command.section = section;
    command.countInBars = countInBars;
    command.keyIndex = static_cast<std::int8_t>(key.index());
    pushCommand(command);
}

void AudioEngine::setBandKey(const Key& key) noexcept
{
    Command command;
    command.type = Command::Type::setBandKey;
    command.keyIndex = static_cast<std::int8_t>(key.index());
    pushCommand(command);
}

void AudioEngine::pushBandCommand(Command::Type type) noexcept
{
    Command command;
    command.type = type;
    pushCommand(command);
}

void AudioEngine::stopBand() noexcept { pushBandCommand(Command::Type::stopBand); }
void AudioEngine::stopBandNow() noexcept { pushBandCommand(Command::Type::stopBandNow); }
void AudioEngine::nextSection() noexcept { pushBandCommand(Command::Type::nextSection); }
void AudioEngine::fill() noexcept { pushBandCommand(Command::Type::fill); }

void AudioEngine::selectSection(int section) noexcept
{
    Command command;
    command.type = Command::Type::selectSection;
    command.section = section;
    pushCommand(command);
}

void AudioEngine::setBandTempo(double samplesPerBeat) noexcept
{
    Command command;
    command.type = Command::Type::setBandTempo;
    command.samplesPerBeat = samplesPerBeat;
    pushCommand(command);
}

void AudioEngine::steerBand(std::int64_t sample, double samplesPerBeat) noexcept
{
    Command command;
    command.type = Command::Type::steerBand;
    command.sample = sample;
    command.samplesPerBeat = samplesPerBeat;
    pushCommand(command);
}

void AudioEngine::setLayerEnabled(int layer, bool enabled) noexcept
{
    const auto bit = 1u << layer;
    const auto mask = enabled ? (layerMask_.fetch_or(bit) | bit) : (layerMask_.fetch_and(~bit) & ~bit);
    Command command;
    command.type = Command::Type::setLayerMask;
    command.mask = mask;
    pushCommand(command);
}

AudioEngine::BandStatus AudioEngine::bandStatus() const noexcept
{
    BandStatus status;
    status.state = static_cast<BandState>(bandState_.load());
    status.section = bandSection_.load();
    status.pendingSection = bandPendingSection_.load();
    status.fillScheduled = bandFillScheduled_.load();
    status.samplesPerBeat = bandSamplesPerBeat_.load();
    status.key = Key::fromIndex(bandKey_.load());
    status.layers = bandLayers_.load();
    const double rate = infoSampleRate_.load();
    status.bpm = status.samplesPerBeat > 0.0 && rate > 0.0 ? 60.0 * rate / status.samplesPerBeat : 0.0;
    return status;
}

int AudioEngine::outputLatencySamples() const noexcept
{
    if (const int roundTrip = measuredRoundTrip_.load(); roundTrip > 0)
        return roundTrip / 2;
    return infoOutputLatency_.load() + infoBufferSize_.load();
}

void AudioEngine::setFileInput(std::vector<float> samples, const juce::String& name)
{
    fileInput_ = std::move(samples);
    fileInputName_ = fileInput_.empty() ? juce::String() : name;
    filePosition_ = 0;
}

void AudioEngine::setInputSource(InputSource source) noexcept
{
    if (source == InputSource::file && inputSource_.load() != InputSource::file)
        rewindFile_.store(true);
    inputSource_.store(source);
}

void AudioEngine::noteOn(int note, int velocity) noexcept
{
    Command command;
    command.type = Command::Type::noteOn;
    command.note = static_cast<std::uint8_t>(note);
    command.velocity = static_cast<std::uint8_t>(velocity);
    pushCommand(command);
}

void AudioEngine::noteOff(int note) noexcept
{
    Command command;
    command.type = Command::Type::noteOff;
    command.note = static_cast<std::uint8_t>(note);
    pushCommand(command);
}

void AudioEngine::allNotesOff() noexcept
{
    Command command;
    command.type = Command::Type::allNotesOff;
    pushCommand(command);
}

bool AudioEngine::startSession(const juce::File& folder, bool clickEnabled, const ClickSettings& click, juce::String& error)
{
    const double rate = infoSampleRate_.load();
    if (rate <= 0.0)
    {
        error = "No audio device is running.";
        return false;
    }
    if (!recorder_.open(folder, rate, error))
        return false;

    Command command;
    command.type = Command::Type::startSession;
    command.clickEnabled = clickEnabled;
    command.click = click;
    pushCommand(command);
    return true;
}

void AudioEngine::stopSession(juce::DynamicObject::Ptr metadata)
{
    Command command;
    command.type = Command::Type::stopSession;
    pushCommand(command);
    // The recorder stops accepting audio straight away, even before the audio thread sees
    // the command.
    recorder_.close(metadata);
}

void AudioEngine::pushCommand(const Command& command) noexcept
{
    if (!commands_.push(command))
        droppedCommands_.fetch_add(1, std::memory_order_relaxed);
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
    scratch_.setSize(numScratchChannels, maxBlockSize_);
    scratch_.clear();
    instrument_.prepare(sampleRate_, maxBlockSize_);
    for (auto& part : parts_)
    {
        part.buffer.setSize(2, maxBlockSize_);
        part.buffer.clear();
        part.instrument.prepare(sampleRate_, maxBlockSize_);
        part.currentGain = part.gain.load();
    }
    player_.prepare(sampleRate_); // a restart stops the band
    player_.setLayerMask(layerMask_.load());
    limiter_.prepare(sampleRate_);
    timeMap_.restart();
    probe_.prepare(sampleRate_);
    analyzer_.prepare(sampleRate_);
    analyzer_.reset(sampleTime_);
    clickTrack_.prepare(sampleRate_);
    currentMonitorGain_ = 0.0f;

    // A restart (possibly at a new sample rate) ends any running session; the UI notices
    // that sessionStartSample() went back to -1 and closes the files.
    sessionActive_ = false;
    sessionStart_.store(-1);

    infoSampleRate_.store(sampleRate_);
    infoBufferSize_.store(device->getCurrentBufferSizeSamples());
    infoInputLatency_.store(device->getInputLatencyInSamples());
    infoOutputLatency_.store(device->getOutputLatencyInSamples());
    publishBandStatus();
}

void AudioEngine::audioDeviceStopped()
{
    instrument_.allNotesOff();
    for (auto& part : parts_)
        part.instrument.allNotesOff();
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

    timeMap_.update(sampleTime_, clock_(), sampleRate_);

    // The mic is the first active input channel.
    const float* mic = nullptr;
    for (int ch = 0; ch < numInputs && mic == nullptr; ++ch)
        mic = inputs[ch];

    applyCommands();

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

    int voices = instrument_.activeVoices();
    for (const auto& part : parts_)
        voices += part.instrument.activeVoices();
    activeVoices_.store(voices, std::memory_order_relaxed);
    publishBandStatus();
    sampleTime_ += numSamples;
    publishedSampleTime_.store(sampleTime_, std::memory_order_relaxed);

    const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - startTicks);
    stats_.record(elapsed, numSamples / sampleRate_);
}

void AudioEngine::applyCommands() noexcept
{
    Command command;
    while (commands_.pop(command))
    {
        switch (command.type)
        {
            case Command::Type::noteOn:      instrument_.noteOn(0, command.note, command.velocity); break;
            case Command::Type::noteOff:     instrument_.noteOff(0, command.note); break;
            case Command::Type::allNotesOff: instrument_.allNotesOff(); break;

            case Command::Type::startSession:
                sessionActive_ = true;
                sessionStart_.store(sampleTime_);
                if (command.clickEnabled)
                    clickTrack_.start(command.click, sampleTime_);
                break;

            case Command::Type::stopSession:
                sessionActive_ = false;
                sessionStart_.store(-1);
                clickTrack_.stop();
                break;

            case Command::Type::setStyle:
                // Stops the band. Its notes are released in the next block, still routed by the
                // old style; the new routes apply after that.
                player_.setStyle(command.style);
                nextLayerRoutes_ = command.routes;
                routesChanged_ = true;
                break;
            case Command::Type::startBand:
                player_.start(command.sample, command.samplesPerBeat, command.section, command.countInBars, Key::fromIndex(command.keyIndex));
                break;
            case Command::Type::setBandKey:    player_.setKey(Key::fromIndex(command.keyIndex)); break;
            case Command::Type::stopBand:      player_.stop(); break;
            case Command::Type::stopBandNow:   player_.stopNow(); break;
            case Command::Type::nextSection:   player_.nextSection(); break;
            case Command::Type::selectSection: player_.selectSection(command.section); break;
            case Command::Type::fill:          player_.fill(); break;
            case Command::Type::setBandTempo:  player_.setTempo(command.samplesPerBeat); break;
            case Command::Type::steerBand:     player_.steerToNearestBeat(command.sample, command.samplesPerBeat); break;
            case Command::Type::setLayerMask:  player_.setLayerMask(command.mask); break;
        }
    }
}

void AudioEngine::publishBandStatus() noexcept
{
    bandState_.store(static_cast<int>(player_.state()), std::memory_order_relaxed);
    bandSection_.store(player_.section(), std::memory_order_relaxed);
    bandPendingSection_.store(player_.pendingSection(), std::memory_order_relaxed);
    bandFillScheduled_.store(player_.fillScheduled(), std::memory_order_relaxed);
    bandSamplesPerBeat_.store(player_.style() != nullptr ? player_.samplesPerBeat() : 0.0, std::memory_order_relaxed);
    bandKey_.store(player_.playingKey().index(), std::memory_order_relaxed);
    bandLayers_.store(player_.state() == BandState::stopped ? 0u : player_.activeLayers(), std::memory_order_relaxed);
}

void AudioEngine::renderBand(std::int64_t chunkStart, int numSamples) noexcept
{
    player_.process(chunkStart, numSamples,
        [this](const PlayerNote& note) {
            auto& instrument = parts_[static_cast<std::size_t>(layerRoutes_[note.layer & 31u])].instrument;
            if (note.on)
                instrument.noteOn(note.offset, note.note, note.velocity);
            else
                instrument.noteOff(note.offset, note.note);
        },
        [this](const BeatTick& tick) {
            if (!beatTicks_.push(tick))
                droppedTicks_.fetch_add(1, std::memory_order_relaxed);
        });
    if (routesChanged_)
    {
        layerRoutes_ = nextLayerRoutes_;
        routesChanged_ = false;
    }
    // The instruments render even when the band has stopped, so the last notes ring out.
    for (auto& part : parts_)
        if (part.instrument.isLoaded())
            part.instrument.render(part.buffer.getWritePointer(0), part.buffer.getWritePointer(1), numSamples);
}

const float* AudioEngine::readFileInput(int numSamples) noexcept
{
    if (rewindFile_.exchange(false))
        filePosition_ = 0;

    float* out = scratch_.getWritePointer(fileChannel);
    for (int i = 0; i < numSamples; ++i)
    {
        out[i] = fileInput_[filePosition_];
        if (++filePosition_ >= fileInput_.size())
            filePosition_ = 0; // loop
    }
    return out;
}

void AudioEngine::processChunk(const float* mic, float* const* outputs, int numOutputs, int offset, int numSamples) noexcept
{
    const std::int64_t chunkStart = sampleTime_ + offset;

    // The voice: the mic, or the loaded file playing in a loop.
    const bool useFile = inputSource_.load(std::memory_order_relaxed) == InputSource::file && !fileInput_.empty();
    const float* voice = useFile ? readFileInput(numSamples) : mic;
    if (voice != nullptr)
        raisePeak(inputPeak_, voice, numSamples);

    analyzer_.process(voice, numSamples, [this](const AnalysisFrame& frame) {
        if (!analysisFrames_.push(frame))
            droppedFrames_.fetch_add(1, std::memory_order_relaxed);
    });

    // The band keeps time even while the latency probe owns the outputs.
    renderBand(chunkStart, numSamples);

    const float* silence = scratch_.getReadPointer(silenceChannel);
    const auto record = [&] {
        if (!sessionActive_)
            return;
        const float* left = numOutputs > 0 && outputs[0] != nullptr ? outputs[0] + offset : silence;
        const float* right = numOutputs > 1 && outputs[1] != nullptr ? outputs[1] + offset : left;
        recorder_.writeAudio(voice != nullptr ? voice : silence, left, right, numSamples);
    };

    // While the latency probe runs, it owns the outputs, and always listens to the real mic.
    float* probeSignal = scratch_.getWritePointer(probeChannel);
    if (probe_.process(mic, probeSignal, numSamples))
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::copy(outputs[ch] + offset, probeSignal, numSamples);
        record();
        return;
    }

    // Voice pass-through, ramped so that switching it on or off does not click.
    const float targetGain = monitorEnabled_.load(std::memory_order_relaxed) ? monitorGain_.load(std::memory_order_relaxed) : 0.0f;
    if (voice != nullptr && (targetGain > 0.0f || currentMonitorGain_ > 0.0f))
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
                out[i] += voice[i] * gain;
            }
        }
    }
    currentMonitorGain_ = targetGain;

    // Instruments: the test instrument, and the band's at their (ramped) volumes. A mono
    // output gets the average of the two channels.
    const auto mixStereo = [&](const float* left, const float* right, float startGain, float endGain) {
        if (numOutputs >= 2)
        {
            addRamped(outputs[0], offset, left, startGain, endGain, numSamples);
            addRamped(outputs[1], offset, right, startGain, endGain, numSamples);
        }
        else if (numOutputs == 1)
        {
            addRamped(outputs[0], offset, left, 0.5f * startGain, 0.5f * endGain, numSamples);
            addRamped(outputs[0], offset, right, 0.5f * startGain, 0.5f * endGain, numSamples);
        }
    };

    float* left = instrumentBuffer_.getWritePointer(0);
    float* right = instrumentBuffer_.getWritePointer(1);
    instrument_.render(left, right, numSamples);
    mixStereo(left, right, 1.0f, 1.0f);

    for (auto& part : parts_)
    {
        if (!part.instrument.isLoaded())
            continue;
        const float gain = part.gain.load(std::memory_order_relaxed);
        mixStereo(part.buffer.getReadPointer(0), part.buffer.getReadPointer(1), part.currentGain, gain);
        part.currentGain = gain;
    }

    // Click track, heard in the headphones only (it is not in input.wav).
    if (clickTrack_.isRunning())
    {
        float* click = scratch_.getWritePointer(clickChannel);
        juce::FloatVectorOperations::clear(click, numSamples);
        clickTrack_.process(click, numSamples, chunkStart, [this](const ClickEvent& event) {
            if (!clickEvents_.push(event))
                droppedClicks_.fetch_add(1, std::memory_order_relaxed);
        });
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::add(outputs[ch] + offset, click, numSamples);
    }

    // Master limiter: nothing reaches the headphones above -1 dBFS.
    if (numOutputs > 0 && outputs[0] != nullptr)
        limiter_.process(outputs[0] + offset, numOutputs > 1 && outputs[1] != nullptr ? outputs[1] + offset : nullptr, numSamples);

    record();
}

void AudioEngine::addRamped(float* out, int offset, const float* in, float startGain, float endGain, int numSamples) noexcept
{
    if (out == nullptr)
        return;
    out += offset;
    if (std::abs(endGain - startGain) < 1.0e-7f)
    {
        juce::FloatVectorOperations::addWithMultiply(out, in, startGain, numSamples);
        return;
    }
    const float step = (endGain - startGain) / static_cast<float>(numSamples);
    float gain = startGain;
    for (int i = 0; i < numSamples; ++i)
    {
        gain += step;
        out[i] += in[i] * gain;
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
