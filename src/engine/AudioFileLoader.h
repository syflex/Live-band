#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <vector>

namespace lb
{

// Not real-time safe. Reads an audio file (WAV, AIFF, FLAC or Ogg), mixes it to mono and
// resamples it to `targetSampleRate`. Returns nothing and sets `error` on failure.
std::optional<std::vector<float>> loadMonoAudio(const juce::File& file, double targetSampleRate, juce::String& error,
                                                double maxSeconds = 600.0);

} // namespace lb
