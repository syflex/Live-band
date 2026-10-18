#pragma once

#include "audio/AnalysisFrame.h"
#include "audio/LatencyProbe.h"
#include "audio/Limiter.h"
#include "audio/VocalAnalyzer.h"
#include "common/CallbackStats.h"
#include "common/SpscQueue.h"
#include "common/TimeMap.h"
#include "engine/SessionRecorder.h"
#include "engine/SfizzInstrument.h"
#include "music/ClickTrack.h"
#include "music/Key.h"
#include "music/PatternPlayer.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace lb
{

// Owns everything that runs in the audio callback: voice input (mic or a file), the vocal
// analysis, voice pass-through, the band (a pattern player driving the drum kit, bass, keys
// and pad), a test instrument, the click track, the master limiter, session recording, the
// latency probe, level meters and callback timing.
//
// Methods marked "message thread" must only be called from the JUCE message thread, which is
// the single producer of the command queue and the single consumer of the analysis, click
// and beat queues.
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    // Seconds on a steady clock. The default is JUCE's high-resolution millisecond counter,
    // the clock MIDI input timestamps use; tests replace it.
    using Clock = double (*)();
    static double systemClock() noexcept;

    struct BandStatus
    {
        BandState state = BandState::stopped;
        int section = 0;
        int pendingSection = -1;
        bool fillScheduled = false;
        double samplesPerBeat = 0.0;
        double bpm = 0.0;
        Key key;                    // the key the band plays in now
        std::uint32_t layers = 0;   // the layers playing: staged entry and the layer switches
        float velocityScale = 1.0f; // the singer's dynamics
        bool intense = false;       // the intense variation is playing
    };

    // The band's instruments. A style's layers are routed by name: "bass", "keys" and "pad"
    // to their own instruments, everything else to the drum kit.
    enum class BandPart : std::uint8_t
    {
        drums,
        bass,
        keys,
        pad
    };
    static constexpr int numBandParts = 4;
    static const char* partName(BandPart part) noexcept;
    static BandPart partForLayer(const std::string& layerName) noexcept;

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
    bool loadBandInstrument(BandPart part, const juce::File& sfzFile);
    bool loadDrumKit(const juce::File& sfzFile) { return loadBandInstrument(BandPart::drums, sfzFile); }
    void setFileInput(std::vector<float> samples, const juce::String& name);
    void setClock(Clock clock) noexcept { clock_ = clock; }

    // Message thread. The band plays this style from now on (it stops first). The engine keeps
    // every style it was given until it is destroyed, so the audio thread never reads a freed one.
    void setStyle(std::unique_ptr<Style> style);
    const Style* style() const noexcept { return style_; }
    juce::String bandInstrumentName(BandPart part) const { return parts_[static_cast<std::size_t>(part)].name; }
    juce::String drumKitName() const { return bandInstrumentName(BandPart::drums); }

    // Message thread: band commands, carried out at the start of the next callback.
    // An invalid key starts the band without one: the drums alone, until setBandKey().
    void startBand(std::int64_t downbeatSample, double samplesPerBeat, int section, int countInBars, Key key = {}) noexcept;
    void stopBand() noexcept;    // a fill, then the ending
    void stopBandNow() noexcept; // silence at once
    void nextSection() noexcept;
    void selectSection(int section) noexcept;
    void fill() noexcept;
    void setBandTempo(double samplesPerBeat) noexcept;
    void steerBand(std::int64_t sample, double samplesPerBeat) noexcept; // a beat falls on `sample`
    void setBandKey(const Key& key) noexcept; // from the next downbeat
    void setBandDynamics(float velocityScale, bool intense) noexcept;
    void advanceBandEntry() noexcept; // the next staged-entry step on the next downbeat
    void setLayerEnabled(int layer, bool enabled) noexcept;
    bool isLayerEnabled(int layer) const noexcept { return (layerMask_.load() & (1u << layer)) != 0; }
    void setPartGain(BandPart part, float linearGain) noexcept { parts_[static_cast<std::size_t>(part)].gain.store(linearGain); }
    void setDrumsGain(float linearGain) noexcept { setPartGain(BandPart::drums, linearGain); }
    bool popBeatTick(BeatTick& tick) noexcept { return beatTicks_.pop(tick); }

    // Any thread.
    BandStatus bandStatus() const noexcept;
    double now() const noexcept { return clock_(); }
    // The sample the audio thread is computing at a given clock time; false before audio runs.
    bool sampleAtTime(double seconds, double& sample) const noexcept { return timeMap_.sampleAt(seconds, sample); }
    // How long a computed sample takes to reach the ears: half the measured round trip when
    // the latency test has run, otherwise the driver's output latency plus one buffer.
    int outputLatencySamples() const noexcept;
    void setMeasuredRoundTripLatency(int samples) noexcept { measuredRoundTrip_.store(samples); }
    std::uint64_t droppedBeatTicks() const noexcept { return droppedTicks_.load(std::memory_order_relaxed); }

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
            stopSession,
            setStyle,
            startBand,
            stopBand,
            stopBandNow,
            nextSection,
            selectSection,
            fill,
            setBandTempo,
            steerBand,
            setLayerMask,
            setBandKey,
            setDynamics,
            advanceEntry
        };

        Type type = Type::noteOn;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool clickEnabled = false;
        ClickSettings click;
        const Style* style = nullptr;
        std::int64_t sample = 0;
        double samplesPerBeat = 0.0;
        int section = 0;
        int countInBars = 0;
        std::uint32_t mask = 0;
        std::int8_t keyIndex = -1;
        float velocityScale = 1.0f;
        bool intense = false;
        std::array<BandPart, 32> routes {}; // with setStyle: the instrument of each layer
        std::array<float, numBandParts> styleGains { 1.0f, 1.0f, 1.0f, 1.0f }; // and the style's mix
    };

    // One of the band's instruments, with its volume.
    struct Part
    {
        SfizzInstrument instrument;
        juce::String name;
        juce::AudioBuffer<float> buffer; // sized in audioDeviceAboutToStart()
        std::atomic<float> gain { 1.0f };
        float currentGain = 1.0f;        // audio thread
    };

    void pushCommand(const Command& command) noexcept;
    void pushBandCommand(Command::Type type) noexcept;
    void applyCommands() noexcept;
    void renderBand(std::int64_t chunkStart, int numSamples) noexcept;
    void publishBandStatus() noexcept;
    void processChunk(const float* mic, float* const* outputs, int numOutputs, int offset, int numSamples) noexcept;
    const float* readFileInput(int numSamples) noexcept;
    static void raisePeak(std::atomic<float>& peak, const float* samples, int numSamples) noexcept;
    static void addRamped(float* out, int offset, const float* in, float startGain, float endGain, int numSamples) noexcept;

    SfizzInstrument instrument_;
    juce::String instrumentName_;
    std::array<Part, numBandParts> parts_;
    std::array<BandPart, 32> layerRoutes_ {}; // audio thread
    std::array<BandPart, 32> nextLayerRoutes_ {};
    std::array<float, numBandParts> styleGains_ { 1.0f, 1.0f, 1.0f, 1.0f };     // audio thread
    std::array<float, numBandParts> nextStyleGains_ { 1.0f, 1.0f, 1.0f, 1.0f };
    bool routesChanged_ = false;
    std::vector<std::unique_ptr<Style>> styles_; // owned for the engine's lifetime
    const Style* style_ = nullptr;               // message thread's view
    Clock clock_ = &AudioEngine::systemClock;
    std::vector<float> fileInput_;
    juce::String fileInputName_;

    SpscQueue<Command> commands_ { 256 };
    SpscQueue<AnalysisFrame> analysisFrames_ { 2048 }; // about 16 s of frames
    SpscQueue<ClickEvent> clickEvents_ { 512 };
    SpscQueue<BeatTick> beatTicks_ { 512 };

    LatencyProbe probe_;
    CallbackStats stats_;
    VocalAnalyzer analyzer_;
    ClickTrack clickTrack_;
    PatternPlayer player_;
    Limiter limiter_;
    TimeMap timeMap_;
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
    std::atomic<std::uint64_t> droppedTicks_ { 0 };
    std::atomic<std::uint32_t> layerMask_ { 0xffffffffu };
    std::atomic<int> measuredRoundTrip_ { 0 };

    // Band status, published by the audio thread after each callback.
    std::atomic<int> bandState_ { 0 };
    std::atomic<int> bandSection_ { 0 };
    std::atomic<int> bandPendingSection_ { -1 };
    std::atomic<bool> bandFillScheduled_ { false };
    std::atomic<double> bandSamplesPerBeat_ { 0.0 };
    std::atomic<int> bandKey_ { -1 };
    std::atomic<std::uint32_t> bandLayers_ { 0 };
    std::atomic<float> bandVelocityScale_ { 1.0f };
    std::atomic<bool> bandIntense_ { false };
    std::atomic<std::int64_t> publishedSampleTime_ { 0 };
    std::atomic<std::int64_t> sessionStart_ { -1 };

    std::atomic<double> infoSampleRate_ { 0.0 };
    std::atomic<int> infoBufferSize_ { 0 };
    std::atomic<int> infoInputLatency_ { 0 };
    std::atomic<int> infoOutputLatency_ { 0 };
};

} // namespace lb
