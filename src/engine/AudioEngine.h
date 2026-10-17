#pragma once

#include "audio/AnalysisFrame.h"
#include "audio/LatencyProbe.h"
#include "audio/VocalAnalyzer.h"
#include "common/CallbackStats.h"
#include "common/SpscQueue.h"
#include "engine/SessionRecorder.h"
#include "engine/SfizzInstrument.h"
#include "music/ClickTrack.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace lb
{

// Owns everything that runs in the audio callback: voice input (mic or a file), the vocal
// analysis, voice pass-through, a test instrument, the click track, session recording, the
// latency probe, level meters and callback timing.
//
// Methods marked "message thread" must only be called from the JUCE message thread, which is
// the single producer of the command queue and the single consumer of the analysis and
// click queues.
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

    enum class InputSource
    {
        microphone,
        file
    };

    AudioEngine() = default;
    ~AudioEngine() override = default;

    // Message thread, while this engine is NOT registered as an audio callback.
    bool loadInstrument(const juce::File& sfzFile);
    void setFileInput(std::vector<float> samples, const juce::String& name);

    // Message thread.
    juce::String instrumentName() const { return instrumentName_; }
    juce::String fileInputName() const { return fileInputName_; }
    bool hasFileInput() const noexcept { return !fileInput_.empty(); }
    void setInputSource(InputSource source) noexcept;
    InputSource inputSource() const noexcept { return inputSource_.load(); }
    void setMonitorEnabled(bool enabled) noexcept { monitorEnabled_.store(enabled); }
    void setMonitorGain(float linearGain) noexcept { monitorGain_.store(linearGain); }
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;

    // Message thread. Opens the session files and asks the audio thread to start writing
    // (and clicking, if enabled) at its next callback.
    bool startSession(const juce::File& folder, bool clickEnabled, const ClickSettings& click, juce::String& error);
    void stopSession(juce::DynamicObject::Ptr metadata);
    bool isSessionOpen() const noexcept { return recorder_.isOpen(); }
    SessionRecorder& recorder() noexcept { return recorder_; }

    // Message thread: the consumer side of the audio thread's output queues.
    bool popAnalysisFrame(AnalysisFrame& frame) noexcept { return analysisFrames_.pop(frame); }
    bool popClickEvent(ClickEvent& event) noexcept { return clickEvents_.pop(event); }

    // Any thread.
    LatencyProbe& latencyProbe() noexcept { return probe_; }
    CallbackStats& callbackStats() noexcept { return stats_; }
    DeviceInfo deviceInfo() const noexcept;
    int activeVoices() const noexcept { return activeVoices_.load(std::memory_order_relaxed); }
    std::uint64_t droppedCommands() const noexcept { return droppedCommands_.load(std::memory_order_relaxed); }
    std::uint64_t droppedAnalysisFrames() const noexcept { return droppedFrames_.load(std::memory_order_relaxed); }
    std::int64_t currentSampleTime() const noexcept { return publishedSampleTime_.load(std::memory_order_relaxed); }

    // Timeline position of the first sample written to the session, or -1 when no session
    // is running on the audio thread.
    std::int64_t sessionStartSample() const noexcept { return sessionStart_.load(); }

    // Highest absolute sample value since the previous call (one reader, the UI). The input
    // peak measures the voice being analysed: the mic, or the file.
    float takeInputPeak() noexcept { return inputPeak_.exchange(0.0f, std::memory_order_relaxed); }
    float takeOutputPeak() noexcept { return outputPeak_.exchange(0.0f, std::memory_order_relaxed); }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    struct Command
    {
        enum class Type : std::uint8_t
        {
            noteOn,
            noteOff,
            allNotesOff,
            startSession,
            stopSession
        };

        Type type = Type::noteOn;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool clickEnabled = false;
        ClickSettings click;
    };

    void pushCommand(const Command& command) noexcept;
    void applyCommands() noexcept;
    void processChunk(const float* mic, float* const* outputs, int numOutputs, int offset, int numSamples) noexcept;
    const float* readFileInput(int numSamples) noexcept;
    static void raisePeak(std::atomic<float>& peak, const float* samples, int numSamples) noexcept;

    SfizzInstrument instrument_;
    juce::String instrumentName_;
    std::vector<float> fileInput_;
    juce::String fileInputName_;

    SpscQueue<Command> commands_ { 256 };
    SpscQueue<AnalysisFrame> analysisFrames_ { 2048 }; // about 16 s of frames
    SpscQueue<ClickEvent> clickEvents_ { 512 };

    LatencyProbe probe_;
    CallbackStats stats_;
    VocalAnalyzer analyzer_;
    ClickTrack clickTrack_;
    SessionRecorder recorder_;

    // Sized in audioDeviceAboutToStart(); used only by the audio thread afterwards.
    juce::AudioBuffer<float> instrumentBuffer_;
    juce::AudioBuffer<float> scratch_; // channels: probe signal, click, file voice, silence
    int maxBlockSize_ = 0;
    double sampleRate_ = 0.0;
    float currentMonitorGain_ = 0.0f;
    std::size_t filePosition_ = 0;
    std::int64_t sampleTime_ = 0; // timeline position of the current callback's first sample
    bool sessionActive_ = false;

    std::atomic<InputSource> inputSource_ { InputSource::microphone };
    std::atomic<bool> rewindFile_ { false };
    std::atomic<bool> monitorEnabled_ { false };
    std::atomic<float> monitorGain_ { 0.7f };
    std::atomic<float> inputPeak_ { 0.0f };
    std::atomic<float> outputPeak_ { 0.0f };
    std::atomic<int> activeVoices_ { 0 };
    std::atomic<std::uint64_t> droppedCommands_ { 0 };
    std::atomic<std::uint64_t> droppedFrames_ { 0 };
    std::atomic<std::uint64_t> droppedClicks_ { 0 };
    std::atomic<std::int64_t> publishedSampleTime_ { 0 };
    std::atomic<std::int64_t> sessionStart_ { -1 };

    std::atomic<double> infoSampleRate_ { 0.0 };
    std::atomic<int> infoBufferSize_ { 0 };
    std::atomic<int> infoInputLatency_ { 0 };
    std::atomic<int> infoOutputLatency_ { 0 };
};

} // namespace lb
