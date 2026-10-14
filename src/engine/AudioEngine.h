#pragma once

#include "audio/LatencyProbe.h"
#include "common/CallbackStats.h"
#include "common/SpscQueue.h"
#include "engine/SfizzInstrument.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>

namespace lb
{

// Owns everything that runs in the audio callback. Phase 0: voice pass-through,
// a test instrument, the latency probe, level meters and callback timing.
//
// Methods marked "message thread" must only be called from the JUCE message thread,
// which is the single producer for the note-event queue.
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    struct DeviceInfo
    {
        double sampleRate = 0.0;
        int bufferSize = 0;
        int inputLatencySamples = 0;  // as reported by the driver
        int outputLatencySamples = 0; // as reported by the driver
    };

    AudioEngine() = default;
    ~AudioEngine() override = default;

    // Message thread, while this engine is NOT registered as an audio callback.
    bool loadInstrument(const juce::File& sfzFile);
    juce::String instrumentName() const { return instrumentName_; }

    // Message thread.
    void setMonitorEnabled(bool enabled) noexcept { monitorEnabled_.store(enabled); }
    void setMonitorGain(float linearGain) noexcept { monitorGain_.store(linearGain); }
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;

    // Any thread.
    LatencyProbe& latencyProbe() noexcept { return probe_; }
    CallbackStats& callbackStats() noexcept { return stats_; }
    DeviceInfo deviceInfo() const noexcept;
    int activeVoices() const noexcept { return activeVoices_.load(std::memory_order_relaxed); }
    std::uint64_t droppedNoteEvents() const noexcept { return droppedNoteEvents_.load(std::memory_order_relaxed); }

    // Highest absolute sample value since the previous call (one reader, the UI).
    float takeInputPeak() noexcept { return inputPeak_.exchange(0.0f, std::memory_order_relaxed); }
    float takeOutputPeak() noexcept { return outputPeak_.exchange(0.0f, std::memory_order_relaxed); }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    struct NoteEvent
    {
        enum class Type : std::uint8_t
        {
            on,
            off,
            allOff
        };

        Type type = Type::on;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
    };

    void pushNoteEvent(NoteEvent event) noexcept;
    void processChunk(const float* mic, float* const* outputs, int numOutputs, int offset, int numSamples) noexcept;
    void applyNoteEvents() noexcept;
    static void raisePeak(std::atomic<float>& peak, const float* samples, int numSamples) noexcept;

    SfizzInstrument instrument_;
    juce::String instrumentName_;
    SpscQueue<NoteEvent> noteEvents_ { 256 };
    LatencyProbe probe_;
    CallbackStats stats_;

    // Sized in audioDeviceAboutToStart(); used only by the audio thread afterwards.
    juce::AudioBuffer<float> instrumentBuffer_;
    juce::AudioBuffer<float> probeBuffer_;
    int maxBlockSize_ = 0;
    double sampleRate_ = 0.0;
    float currentMonitorGain_ = 0.0f;

    std::atomic<bool> monitorEnabled_ { false };
    std::atomic<float> monitorGain_ { 0.7f };
    std::atomic<float> inputPeak_ { 0.0f };
    std::atomic<float> outputPeak_ { 0.0f };
    std::atomic<int> activeVoices_ { 0 };
    std::atomic<std::uint64_t> droppedNoteEvents_ { 0 };

    std::atomic<double> infoSampleRate_ { 0.0 };
    std::atomic<int> infoBufferSize_ { 0 };
    std::atomic<int> infoInputLatency_ { 0 };
    std::atomic<int> infoOutputLatency_ { 0 };
};

} // namespace lb
