#pragma once

#include <cstddef>
#include <span>

namespace lb
{

// Fills `out` with an exponential (log) sine sweep from startHz to endHz, with short
// raised-cosine fades at both ends so it starts and stops without clicks.
void makeLogChirp(std::span<float> out, double sampleRate, double startHz, double endHz, float amplitude);

struct DelayEstimate
{
    long lagSamples = -1;  // where `reference` starts inside `recorded`; -1 if nothing was found
    float correlation = 0; // absolute normalised correlation at that lag, from 0 to 1
};

// Finds the lag at which `reference` best matches `recorded`, using normalised
// cross-correlation. The absolute value is used, so a polarity flip in the
// speaker-to-mic path does not matter. A correlation below about 0.3 means the
// probe signal was not clearly heard.
DelayEstimate estimateDelay(std::span<const float> recorded, std::span<const float> reference);

} // namespace lb
