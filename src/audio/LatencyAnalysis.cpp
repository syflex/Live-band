#include "audio/LatencyAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lb
{

void makeLogChirp(std::span<float> out, double sampleRate, double startHz, double endHz, float amplitude)
{
    const std::size_t n = out.size();
    if (n == 0)
        return;

    const double duration = static_cast<double>(n) / sampleRate;
    const double k = std::log(endHz / startHz);
    const std::size_t fadeLength = std::min<std::size_t>(n / 4, static_cast<std::size_t>(0.002 * sampleRate));

    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        // Phase of an exponential sweep: 2*pi*f0*T/k * (exp(t*k/T) - 1).
        const double phase = 2.0 * std::numbers::pi * startHz * duration / k * (std::exp(t * k / duration) - 1.0);

        double gain = amplitude;
        if (fadeLength > 0)
        {
            const std::size_t fromEnd = n - 1 - i;
            const std::size_t edge = std::min(i, fromEnd);
            if (edge < fadeLength)
                gain *= 0.5 * (1.0 - std::cos(std::numbers::pi * static_cast<double>(edge) / static_cast<double>(fadeLength)));
        }

        out[i] = static_cast<float>(gain * std::sin(phase));
    }
}

DelayEstimate estimateDelay(std::span<const float> recorded, std::span<const float> reference)
{
    DelayEstimate best;
    const std::size_t m = reference.size();
    if (m == 0 || recorded.size() < m)
        return best;

    double referenceEnergy = 0.0;
    for (float x : reference)
        referenceEnergy += static_cast<double>(x) * x;
    if (referenceEnergy <= 0.0)
        return best;

    // Energy of the recorded window [lag, lag + m), kept as a running sum.
    double windowEnergy = 0.0;
    for (std::size_t i = 0; i < m; ++i)
        windowEnergy += static_cast<double>(recorded[i]) * recorded[i];

    const std::size_t lastLag = recorded.size() - m;
    for (std::size_t lag = 0; lag <= lastLag; ++lag)
    {
        if (lag > 0)
        {
            const double leaving = recorded[lag - 1];
            const double entering = recorded[lag + m - 1];
            windowEnergy = std::max(0.0, windowEnergy - leaving * leaving + entering * entering);
        }

        if (windowEnergy <= 1e-12)
            continue;

        double dot = 0.0;
        const float* window = recorded.data() + lag;
        for (std::size_t i = 0; i < m; ++i)
            dot += static_cast<double>(window[i]) * reference[i];

        const auto correlation = static_cast<float>(std::abs(dot) / std::sqrt(referenceEnergy * windowEnergy));
        if (correlation > best.correlation)
        {
            best.correlation = correlation;
            best.lagSamples = static_cast<long>(lag);
        }
    }

    return best;
}

} // namespace lb
