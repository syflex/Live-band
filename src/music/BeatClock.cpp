#include "music/BeatClock.h"

#include <algorithm>

namespace lb
{

void BeatClock::start(std::int64_t sample, double beat, double samplesPerBeat) noexcept
{
    position_ = sample;
    beat_ = beat;
    targetRate_ = rate_ = 1.0 / samplesPerBeat;
    anchorSample_ = sample;
    anchorBeat_ = beat;
}

void BeatClock::setTempo(std::int64_t sample, double samplesPerBeat) noexcept
{
    anchorBeat_ = targetBeatAt(sample);
    anchorSample_ = sample;
    targetRate_ = 1.0 / samplesPerBeat;
}

void BeatClock::steer(std::int64_t anchorSample, double anchorBeat, double samplesPerBeat) noexcept
{
    anchorSample_ = anchorSample;
    anchorBeat_ = anchorBeat;
    targetRate_ = 1.0 / samplesPerBeat;
}

BeatClock::Span BeatClock::advance(std::int64_t blockStart, int numSamples) noexcept
{
    if (blockStart != position_)
        beat_ = beatAt(blockStart); // should not happen; keep going rather than stall

    // Proportional phase correction: the error in beats, divided by the time constant, is
    // the fraction by which this block runs fast or slow.
    const double error = targetBeatAt(blockStart) - beat_;
    const double correction = std::clamp(error / settings_.correctionBeats, -settings_.maxRateDeviation, settings_.maxRateDeviation);
    rate_ = targetRate_ * (1.0 + correction);

    const Span span { beat_, rate_ };
    beat_ += rate_ * numSamples;
    position_ = blockStart + numSamples;
    return span;
}

double BeatClock::beatAt(std::int64_t sample) const noexcept
{
    return beat_ + static_cast<double>(sample - position_) * rate_;
}

double BeatClock::targetBeatAt(std::int64_t sample) const noexcept
{
    return anchorBeat_ + static_cast<double>(sample - anchorSample_) * targetRate_;
}

} // namespace lb
