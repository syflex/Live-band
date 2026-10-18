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
    steerBeatsLeft_ = 0.0;
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
    steerBeatsLeft_ = settings_.relockBeats;
}

BeatClock::Span BeatClock::advance(std::int64_t blockStart, int numSamples) noexcept
{
    if (blockStart != position_)
        beat_ = beatAt(blockStart); // should not happen; keep going rather than stall

    // The error in beats, divided by the beats left to fix it, is the fraction by which this
    // block runs fast or slow. During a steer that is the rest of its window (at least this
    // block), so the rate stays steady and the error is gone when the window ends; outside
    // one, small errors fade with the time constant.
    const double error = targetBeatAt(blockStart) - beat_;
    const double blockBeats = targetRate_ * numSamples;
    const double window = steerBeatsLeft_ > 0.0 ? std::max(steerBeatsLeft_, blockBeats) : settings_.correctionBeats;
    const double correction = std::clamp(error / window, -settings_.maxRateDeviation, settings_.maxRateDeviation);
    rate_ = targetRate_ * (1.0 + correction);
    if (steerBeatsLeft_ > 0.0)
    {
        // A steer the rate limit could not finish within its window carries on at the limit.
        const double left = std::abs(error - correction * blockBeats);
        steerBeatsLeft_ = left < 1e-6 ? 0.0 : std::max(steerBeatsLeft_ - blockBeats, left / settings_.maxRateDeviation);
    }

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
