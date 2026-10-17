#include "music/TapTempo.h"

#include <cmath>

namespace lb
{

int TapTempo::tap(double seconds) noexcept
{
    if (count_ > 0)
    {
        const double last = times_[static_cast<std::size_t>((head_ + count_ - 1) % maxTaps)];
        const double interval = seconds - last;
        if (interval < settings_.minPeriodSeconds * 0.5)
            return total_; // a bounce or a double press, not a beat

        bool restart = interval > settings_.resetGapSeconds;
        if (!restart && total_ >= 2)
        {
            const double period = estimate().period;
            restart = std::abs(interval - period) > settings_.outlierFraction * period;
        }
        if (restart)
            reset();
    }

    if (count_ < maxTaps)
    {
        times_[static_cast<std::size_t>((head_ + count_) % maxTaps)] = seconds;
        ++count_;
    }
    else
    {
        times_[static_cast<std::size_t>(head_)] = seconds;
        head_ = (head_ + 1) % maxTaps;
    }
    ++total_;
    return total_;
}

TapTempo::Estimate TapTempo::estimate() const noexcept
{
    Estimate e;
    e.taps = total_;
    if (count_ == 0)
        return e;
    e.lastTap = times_[static_cast<std::size_t>((head_ + count_ - 1) % maxTaps)];
    if (count_ < 2)
    {
        e.phase = e.lastTap;
        return e;
    }

    // Least squares over the taps in the buffer, indexed from 0. Times are taken relative to
    // the first one to keep the sums well conditioned.
    const double origin = times_[static_cast<std::size_t>(head_)];
    const double n = count_;
    double sumI = 0.0, sumT = 0.0;
    for (int i = 0; i < count_; ++i)
    {
        sumI += i;
        sumT += times_[static_cast<std::size_t>((head_ + i) % maxTaps)] - origin;
    }
    const double meanI = sumI / n, meanT = sumT / n;
    double sxy = 0.0, sxx = 0.0;
    for (int i = 0; i < count_; ++i)
    {
        const double t = times_[static_cast<std::size_t>((head_ + i) % maxTaps)] - origin;
        sxy += (i - meanI) * (t - meanT);
        sxx += (i - meanI) * (i - meanI);
    }
    e.period = sxy / sxx;

    // The phase refers to the run's first tap, which may have left the buffer.
    const int dropped = total_ - count_;
    e.phase = origin + (meanT - e.period * meanI) - dropped * e.period;
    return e;
}

} // namespace lb
