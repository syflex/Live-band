#pragma once

#include "music/Groove.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace lb
{

// Reads a style folder: style.json and the MIDI files it names (see
// tools/python/make_grooves.py for the format). MIDI tracks are matched to the style's
// layers by track name; a pattern is as many whole bars long as its last event needs.
//
// Not real-time safe. Returns nullptr and sets error when the style cannot be used.
std::unique_ptr<Style> loadStyle(const juce::File& folder, juce::String& error);

} // namespace lb
