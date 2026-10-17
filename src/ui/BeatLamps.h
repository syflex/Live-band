#pragma once

#include "music/PatternPlayer.h"

#include <juce_gui_basics/juce_gui_basics.h>

// One light per beat of the bar, lit on the beat the singer is hearing now, and a line of
// text saying what the band is doing. Grey during the count-in, orange during a fill, red
// on the ending, green otherwise.
class BeatLamps final : public juce::Component
{
public:
    void setBeatsPerBar(int beats);
    // The beat now being heard; nullptr when the band is silent.
    void setBeat(const lb::BeatTick* tick, double secondsSinceBeat);
    void setStatus(const juce::String& text);

    void paint(juce::Graphics& g) override;

private:
    int beatsPerBar_ = 4;
    bool active_ = false;
    lb::BeatTick tick_;
    float flash_ = 0.0f; // 1 on the beat, fading
    juce::String status_;
};
