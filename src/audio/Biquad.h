#pragma once

#include <cmath>
#include <numbers>

namespace lb
{

// Second-order IIR filter (RBJ Audio EQ Cookbook), transposed direct form II.
class Biquad
{
public:
    void setHighPass(double sampleRate, double cutoffHz, double q = 0.7071067811865476)
    {
        const double w0 = 2.0 * std::numbers::pi * cutoffHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        b0_ = (1.0 + cosW0) / 2.0 / a0;
        b1_ = -(1.0 + cosW0) / a0;
        b2_ = b0_;
        a1_ = -2.0 * cosW0 / a0;
        a2_ = (1.0 - alpha) / a0;
        reset();
    }

    void reset() noexcept { z1_ = z2_ = 0.0; }

    float process(float x) noexcept
    {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return static_cast<float>(y);
    }

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

} // namespace lb
