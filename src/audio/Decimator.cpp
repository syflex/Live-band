#include "audio/Decimator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lb
{

void Decimator::prepare(int factor)
{
    factor_ = std::max(factor, 1);

    if (factor_ == 1)
    {
        taps_.assign(1, 1.0f);
    }
    else
    {
        // Pass band to 0.45 of the output rate; a Blackman window keeps aliasing well down.
        const int numTaps = 32 * factor_ + 1;
        const double cutoff = 0.45 / factor_; // cycles per input sample
        const double centre = (numTaps - 1) / 2.0;

        taps_.resize(static_cast<std::size_t>(numTaps));
        double sum = 0.0;
        for (int i = 0; i < numTaps; ++i)
        {
            const double x = i - centre;
            const double sinc = x == 0.0 ? 2.0 * cutoff : std::sin(2.0 * std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
            const double phase = 2.0 * std::numbers::pi * i / (numTaps - 1);
            const double window = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
            taps_[static_cast<std::size_t>(i)] = static_cast<float>(sinc * window);
            sum += sinc * window;
        }
        for (auto& tap : taps_)
            tap = static_cast<float>(tap / sum); // unity gain at DC
    }

    history_.assign(2 * taps_.size(), 0.0f);
    reset();
}

void Decimator::reset() noexcept
{
    std::fill(history_.begin(), history_.end(), 0.0f);
    writePos_ = 0;
    phase_ = 0;
}

} // namespace lb
