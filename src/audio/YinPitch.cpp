#include "audio/YinPitch.h"

#include <algorithm>
#include <cmath>

namespace lb
{

void YinPitch::prepare(double sampleRate, const Settings& settings)
{
    sampleRate_ = sampleRate;
    settings_ = settings;
    window_ = static_cast<int>(std::lround(settings.windowSeconds * sampleRate));
    minLag_ = std::max(2, static_cast<int>(std::floor(sampleRate / settings.maxHz)));
    // One extra lag so the parabolic fit has a right-hand neighbour at the lowest pitch.
    maxLag_ = static_cast<int>(std::ceil(sampleRate / settings.minHz)) + 1;
    difference_.assign(static_cast<std::size_t>(maxLag_ + 1), 0.0f);
    cmnd_.assign(static_cast<std::size_t>(maxLag_ + 1), 1.0f);
}

float YinPitch::interpolatedCmnd(int lag) const noexcept
{
    // Minimum of the parabola through the CMND at lag - 1, lag and lag + 1.
    const float left = cmnd_[static_cast<std::size_t>(lag - 1)];
    const float centre = cmnd_[static_cast<std::size_t>(lag)];
    const float right = cmnd_[static_cast<std::size_t>(lag + 1)];
    const float curvature = left - 2.0f * centre + right;
    if (curvature <= 0.0f)
        return centre;
    return std::max(0.0f, centre - 0.125f * (left - right) * (left - right) / curvature);
}

PitchEstimate YinPitch::estimate(const float* input) noexcept
{
    // 1. Difference function d(tau) = sum_j (x[j] - x[j + tau])^2 over the window.
    //    Four partial sums let the compiler keep several multiply-adds in flight.
    for (int lag = 1; lag <= maxLag_; ++lag)
    {
        const float* a = input;
        const float* b = input + lag;
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        int j = 0;
        for (; j + 3 < window_; j += 4)
        {
            const float d0 = a[j] - b[j];
            const float d1 = a[j + 1] - b[j + 1];
            const float d2 = a[j + 2] - b[j + 2];
            const float d3 = a[j + 3] - b[j + 3];
            s0 += d0 * d0;
            s1 += d1 * d1;
            s2 += d2 * d2;
            s3 += d3 * d3;
        }
        for (; j < window_; ++j)
        {
            const float d = a[j] - b[j];
            s0 += d * d;
        }
        difference_[static_cast<std::size_t>(lag)] = (s0 + s1) + (s2 + s3);
    }

    // 2. Cumulative mean normalised difference.
    cmnd_[0] = 1.0f;
    double runningSum = 0.0;
    for (int lag = 1; lag <= maxLag_; ++lag)
    {
        const double d = difference_[static_cast<std::size_t>(lag)];
        runningSum += d;
        cmnd_[static_cast<std::size_t>(lag)] = runningSum > 0.0 ? static_cast<float>(d * lag / runningSum) : 1.0f;
    }

    // 3. The first dip below the threshold, followed down to its local minimum.
    //    If there is none, fall back to the deepest point in range.
    int best = -1;
    for (int lag = minLag_; lag < maxLag_; ++lag)
    {
        if (cmnd_[static_cast<std::size_t>(lag)] < settings_.threshold)
        {
            while (lag + 1 < maxLag_ && cmnd_[static_cast<std::size_t>(lag + 1)] < cmnd_[static_cast<std::size_t>(lag)])
                ++lag;
            best = lag;
            break;
        }
    }

    PitchEstimate result;
    result.confident = best >= 0;
    if (!result.confident)
    {
        best = minLag_;
        for (int lag = minLag_ + 1; lag < maxLag_; ++lag)
            if (cmnd_[static_cast<std::size_t>(lag)] < cmnd_[static_cast<std::size_t>(best)])
                best = lag;
    }

    // 4. Sub-octave check. When the second harmonic is stronger than the fundamental, the dip
    //    at half the period can cross the threshold first, giving an answer an octave too high.
    //    Both dips are compared at their interpolated minima: when the period falls between
    //    samples, the sampled CMND overstates the depth of the dip at the true period.
    if (result.confident && settings_.octaveCheckRatio > 0.0f && 2 * best + 3 < maxLag_)
    {
        const float chosenDepth = interpolatedCmnd(best);
        if (chosenDepth > settings_.octaveCheckMinCmnd)
        {
            int candidate = 2 * best;
            for (int lag = 2 * best - 2; lag <= 2 * best + 2; ++lag)
                if (cmnd_[static_cast<std::size_t>(lag)] < cmnd_[static_cast<std::size_t>(candidate)])
                    candidate = lag;
            if (interpolatedCmnd(candidate) < settings_.octaveCheckRatio * chosenDepth)
                best = candidate;
        }
    }

    // 5. Parabolic interpolation on the raw difference function for sub-sample precision.
    const float left = difference_[static_cast<std::size_t>(best - 1)];
    const float centre = difference_[static_cast<std::size_t>(best)];
    const float right = difference_[static_cast<std::size_t>(best + 1)];
    const float curvature = left - 2.0f * centre + right;
    float shift = 0.0f;
    if (curvature > 0.0f)
        shift = std::clamp(0.5f * (left - right) / curvature, -0.5f, 0.5f);

    result.frequencyHz = static_cast<float>(sampleRate_ / (best + shift));
    result.periodicity = std::clamp(1.0f - cmnd_[static_cast<std::size_t>(best)], 0.0f, 1.0f);
    return result;
}

} // namespace lb
