#pragma once

namespace lb
{

// A safety limiter for the master output: no sample leaves above the threshold.
//
// The gain drops at once on a peak and recovers smoothly. There is no look-ahead, so it adds
// no latency and leaves signals below the threshold untouched. Real-time safe.
class Limiter
{
public:
    void prepare(double sampleRate, float thresholdDb = -1.0f, float releaseMs = 80.0f) noexcept;
    void reset() noexcept { gain_ = 1.0f; }

    // Limits both channels by the same gain. right may be nullptr for mono.
    void process(float* left, float* right, int numSamples) noexcept;

    float currentGain() const noexcept { return gain_; }

private:
    float threshold_ = 0.89f;
    float releaseCoefficient_ = 0.9997f;
    float gain_ = 1.0f;
};

} // namespace lb
