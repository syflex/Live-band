#pragma once

#include "audio/AnalysisFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// A scrolling view of the last few seconds of analysis: the sung pitch on a note grid,
// onsets as vertical lines, and the onset detection function along the bottom. Mainly a
// tuning and demo aid.
class PitchView final : public juce::Component
{
public:
    PitchView();

    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate; }

    // Message thread, once per analysis frame.
    void addFrame(const lb::AnalysisFrame& frame);

    void paint(juce::Graphics& g) override;

private:
    static constexpr double windowSeconds = 6.0;
    static constexpr int capacity = 1024; // more than 6 s of 8 ms frames
    static constexpr float lowestMidi = 36.0f;  // C2
    static constexpr float highestMidi = 84.0f; // C6

    std::vector<lb::AnalysisFrame> frames_;
    struct Onset
    {
        std::int64_t sampleTime;
        bool fromPitch;
    };

    std::vector<Onset> onsets_;
    int newest_ = -1;
    int count_ = 0;
    double sampleRate_ = 48000.0;
    float odfScale_ = 0.1f;
};
