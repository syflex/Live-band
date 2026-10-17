#pragma once

#include <cmath>
#include <string>

namespace lb
{

inline float frequencyToMidi(float hz) noexcept
{
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

inline float midiToFrequency(float midi) noexcept
{
    return 440.0f * std::exp2((midi - 69.0f) / 12.0f);
}

inline const char* pitchClassName(int pitchClass) noexcept
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pitchClass % 12) + 12) % 12];
}

// "A4" for 69, "C4" for 60 (middle C). Allocates; not for the audio thread.
inline std::string noteName(int midiNote)
{
    const int octave = static_cast<int>(std::floor(midiNote / 12.0)) - 1;
    return std::string(pitchClassName(midiNote)) + std::to_string(octave);
}

} // namespace lb
