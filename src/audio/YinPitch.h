#pragma once

#include <vector>

namespace lb
{

struct PitchEstimate
{
    float frequencyHz = 0.0f; // best period candidate, even when not confident
    float periodicity = 0.0f; // 1 - CMND at the chosen lag: near 1 for a clean tone, near 0 for noise
    bool confident = false;   // the CMND dipped below the threshold
};

// YIN fundamental-frequency estimator (de Cheveigné and Kawahara, 2002): difference
// function, cumulative mean normalised difference (CMND), absolute threshold, a sub-octave
// check, and parabolic interpolation of the chosen minimum.
class YinPitch
{
public:
    struct Settings
    {
        float minHz = 65.0f;     // C2
        float maxHz = 1050.0f;   // a little above C6
        float threshold = 0.12f; // CMND threshold
        double windowSeconds = 0.032;

        // Sub-octave check against octave-too-high errors: when the chosen dip is only
        // moderately deep (CMND above octaveCheckMinCmnd) and the dip at twice the period is
        // much deeper (below octaveCheckRatio times it), the longer period wins. A ratio of
        // 0 turns the check off.
        float octaveCheckMinCmnd = 0.05f;
        float octaveCheckRatio = 0.3f;
    };

    // Not real-time safe.
    void prepare(double sampleRate, const Settings& settings);
    void prepare(double sampleRate) { prepare(sampleRate, Settings {}); }

    int windowSize() const noexcept { return window_; }
    int maxLag() const noexcept { return maxLag_; }

    // Number of consecutive samples estimate() reads.
    int requiredInput() const noexcept { return window_ + maxLag_; }

    // Real-time safe. `input` holds requiredInput() samples, oldest first.
    PitchEstimate estimate(const float* input) noexcept;

    // CMND value at a lag, valid after estimate(); exposed for tests and diagnostics.
    float cmndAt(int lag) const noexcept { return cmnd_[static_cast<std::size_t>(lag)]; }

private:
    float interpolatedCmnd(int lag) const noexcept;

    double sampleRate_ = 0.0;
    Settings settings_;
    int window_ = 0;
    int minLag_ = 0;
    int maxLag_ = 0;
    std::vector<float> difference_;
    std::vector<float> cmnd_;
};

} // namespace lb
