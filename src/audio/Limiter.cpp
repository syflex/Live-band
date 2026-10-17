#include "audio/Limiter.h"

#include <algorithm>
#include <cmath>

namespace lb
{

void Limiter::prepare(double sampleRate, float thresholdDb, float releaseMs) noexcept
{
    threshold_ = std::pow(10.0f, thresholdDb / 20.0f);
    releaseCoefficient_ = static_cast<float>(std::exp(-1.0 / (sampleRate * releaseMs * 0.001)));
    gain_ = 1.0f;
}

void Limiter::process(float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float peak = std::max(std::abs(left[i]), right != nullptr ? std::abs(right[i]) : 0.0f);
        const float needed = peak > threshold_ ? threshold_ / peak : 1.0f;
        // Recover towards unity, but never above what this sample needs.
        gain_ = std::min(needed, 1.0f - (1.0f - gain_) * releaseCoefficient_);
        left[i] *= gain_;
        if (right != nullptr)
            right[i] *= gain_;
    }
}

} // namespace lb
