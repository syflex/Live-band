#pragma once

#include "audio/AnalysisFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Shows the note being sung right now: its name, how many cents sharp or flat, and the
// frequency. Shows a dash when nothing has been sung for a moment.
class NoteDisplay final : public juce::Component
{
public:
    // Message thread, once per analysis frame.
    void addFrame(const lb::AnalysisFrame& frame);

    void paint(juce::Graphics& g) override;

private:
    static constexpr int holdFrames = 30; // keep showing the last note for about 0.25 s

    float midi_ = 0.0f;
    float frequencyHz_ = 0.0f;
    float periodicity_ = 0.0f;
    int framesSinceVoiced_ = holdFrames;
};
