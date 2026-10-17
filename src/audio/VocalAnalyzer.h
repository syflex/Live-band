#pragma once

#include "audio/AnalysisFrame.h"
#include "audio/Biquad.h"
#include "audio/Decimator.h"
#include "audio/OnsetDetector.h"
#include "audio/YinPitch.h"

#include <cstdint>
#include <vector>

namespace lb
{

// Turns the singer's voice into a stream of AnalysisFrames: pitch (YIN), level and
// onsets, one frame per 8 ms hop. The input is decimated to about 16 kHz first.
//
// Threads: prepare() and reset() are not real-time safe; process() is, and allocates nothing.
class VocalAnalyzer
{
public:
    struct Settings
    {
        YinPitch::Settings pitch;
        OnsetDetector::Settings onsets;
        double targetAnalysisRate = 16000.0;
        double hopSeconds = 0.008;
        int fftSize = 512;               // at the analysis rate (32 ms at 16 kHz)
        double highPassHz = 50.0;
        float gateDb = -50.0f;           // quieter frames are never voiced and never onsets
        float voicingHoldCmnd = 0.2f;    // a voiced note stays voiced while the CMND stays below this
        int medianFrames = 5;            // pitch smoothing window
        float outlierSemitones = 1.0f;   // frames further than this from the median are replaced by it
        float noteChangeSemitones = 0.8f;
        int noteChangeFrames = 4;        // frames a pitch change must last to count as a new note
        int noteGapFrames = 3;           // unvoiced frames that end a note
    };

    void prepare(double inputSampleRate, const Settings& settings);
    void prepare(double inputSampleRate) { prepare(inputSampleRate, Settings {}); }

    // Clears all state. `startSampleTime` is the timeline position of the next input sample.
    void reset(std::int64_t startSampleTime = 0) noexcept;

    double analysisRate() const noexcept { return analysisRate_; }
    int decimationFactor() const noexcept { return decimator_.factor(); }
    int hopSize() const noexcept { return hop_; } // in analysis samples

    // How far a frame's centre lags behind the newest input sample, in input samples.
    std::int64_t frameDelaySamples() const noexcept { return centreDelayInput_; }

    // Real-time safe. Calls onFrame(const AnalysisFrame&) for every frame completed inside
    // this block. `input` may be null, which counts as silence.
    template <typename OnFrame>
    void process(const float* input, int numSamples, OnFrame&& onFrame) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float decimated = 0.0f;
            const bool ready = decimator_.push(input != nullptr ? input[i] : 0.0f, decimated);
            ++inputSamplesSeen_;
            if (!ready)
                continue;

            pending_[static_cast<std::size_t>(pendingCount_++)] = highPass_.process(decimated);
            if (pendingCount_ == hop_)
            {
                pendingCount_ = 0;
                onFrame(analyseFrame());
            }
        }
    }

private:
    AnalysisFrame analyseFrame() noexcept;
    float smoothedMidi(float midi, bool voiced) noexcept;
    void reportOnset(AnalysisFrame& frame, std::int64_t time, float strength, std::uint8_t flag) noexcept;

    Settings settings_;
    double inputRate_ = 0.0;
    double analysisRate_ = 0.0;
    int hop_ = 0;
    int historyLength_ = 0;
    int fftOffset_ = 0; // where the onset detector's frame starts in history_
    int yinOffset_ = 0; // where the pitch detector's input starts in history_
    std::int64_t centreDelayInput_ = 0;
    std::int64_t hopInput_ = 0;
    std::int64_t minInterOnsetInput_ = 0;

    Decimator decimator_;
    Biquad highPass_;
    YinPitch yin_;
    OnsetDetector onsetDetector_;

    std::vector<float> history_; // analysis-rate samples, oldest first
    std::vector<float> pending_; // samples waiting for the next hop
    int pendingCount_ = 0;

    std::int64_t startSampleTime_ = 0;
    std::int64_t inputSamplesSeen_ = 0;

    // Voicing and pitch smoothing.
    bool voiced_ = false;
    std::vector<float> recentMidi_;
    std::vector<float> medianScratch_;
    int recentWrite_ = 0;

    // Note tracking for pitch onsets.
    bool inNote_ = false;
    float noteMidi_ = 0.0f;
    int voicedRun_ = 0;
    int unvoicedRun_ = 0;
    int changeRun_ = 0;
    std::int64_t runStartTime_ = 0;
    std::int64_t changeStartTime_ = 0;

    float previousRmsDb_ = -120.0f;
    std::int64_t lastOnsetTime_ = 0;
    bool anyOnset_ = false;
};

} // namespace lb
