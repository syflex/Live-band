#pragma once

#include <cstdint>

namespace lb
{

// One analysis result per hop (8 ms). Times are positions on the engine's sample counter,
// at the input sample rate. They are not yet corrected for input latency.
struct AnalysisFrame
{
    enum Flags : std::uint8_t
    {
        voiced = 1,
        energyOnset = 2, // onset found in the spectrum
        pitchOnset = 4   // onset found from the pitch: a new note or the voice starting
    };

    std::int64_t sampleTime = 0;      // centre of the analysis window
    float f0Hz = 0.0f;                // raw YIN estimate; 0 when unvoiced
    float midiPitch = 0.0f;           // smoothed pitch in MIDI note numbers (69 = A4); 0 when unvoiced
    float periodicity = 0.0f;         // 0 to 1; how clearly pitched the frame is
    float rmsDb = -120.0f;            // level of the analysis window, dBFS
    float odf = 0.0f;                 // onset detection function
    float onsetStrength = 0.0f;       // > 0 when this frame reports an onset
    std::int64_t onsetSampleTime = 0; // when that onset happened, usually a few frames earlier
    std::uint8_t flags = 0;

    bool isVoiced() const noexcept { return (flags & voiced) != 0; }
    bool hasOnset() const noexcept { return onsetStrength > 0.0f; }
};

} // namespace lb
