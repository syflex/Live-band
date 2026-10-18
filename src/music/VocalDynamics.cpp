#include "music/VocalDynamics.h"

#include <algorithm>
#include <cmath>

namespace lb
{

void VocalDynamics::reset() noexcept
{
    histogram_.fill(0.0);
    total_ = 0.0;
    smoothedDb_ = lowestDb;
    haveLevel_ = false;
    lastSeconds_ = -1.0;
    voicedSeconds_ = 0.0;
    unvoicedFor_ = 0.0;
    percentile_ = 0.5;
    loudFor_ = 0.0;
    quietFor_ = 0.0;
    intense_ = false;
}

double VocalDynamics::levelDb() const noexcept
{
    return smoothedDb_;
}

double VocalDynamics::velocityScale() const noexcept
{
    if (percentile_ < 0.5)
        return settings_.minScale + (1.0 - settings_.minScale) * percentile_ / 0.5;
    return 1.0 + (settings_.maxScale - 1.0) * (percentile_ - 0.5) / 0.5;
}

void VocalDynamics::process(double seconds, bool voiced, float rmsDb) noexcept
{
    const double dt = lastSeconds_ >= 0.0 ? std::clamp(seconds - lastSeconds_, 0.0, 0.1) : 0.0;
    lastSeconds_ = seconds;
    voiced = voiced && rmsDb > static_cast<float>(lowestDb);

    if (voiced)
    {
        unvoicedFor_ = 0.0;
        const double level = static_cast<double>(rmsDb);
        smoothedDb_ = haveLevel_ ? smoothedDb_ + (level - smoothedDb_) * (1.0 - std::exp(-dt / settings_.smoothingSeconds)) : level;
        haveLevel_ = true;

        // The session's distribution of the level, fading slowly so that it follows the
        // singer through a long service.
        const double fade = std::exp(-dt / settings_.historySeconds);
        for (auto& weight : histogram_)
            weight *= fade;
        const auto bin = static_cast<std::size_t>(std::clamp(static_cast<int>((levelDb() - lowestDb) / binDb), 0, numBins - 1));
        histogram_[bin] += dt;
        total_ = total_ * fade + dt;
        voicedSeconds_ += dt;

        if (voicedSeconds_ >= settings_.minVoicedSeconds && total_ > 0.0)
        {
            // The share of the session quieter than now, each moment spread over a normal
            // distribution of spreadDb around its level.
            const double level = levelDb();
            double below = 0.0;
            for (std::size_t i = 0; i < histogram_.size(); ++i)
            {
                if (histogram_[i] <= 0.0)
                    continue;
                const double centre = lowestDb + (static_cast<double>(i) + 0.5) * binDb;
                below += histogram_[i] * 0.5 * std::erfc((centre - level) / (settings_.spreadDb * std::sqrt(2.0)));
            }
            percentile_ = std::clamp(below / total_, 0.0, 1.0);
        }
    }
    else
    {
        unvoicedFor_ += dt;
        if (unvoicedFor_ > settings_.breathSeconds)
            percentile_ += (0.5 - percentile_) * (1.0 - std::exp(-dt / settings_.relaxSeconds));
    }

    // Loud or quiet for long enough changes the band; a breath changes nothing.
    const bool breath = !voiced && unvoicedFor_ <= settings_.breathSeconds;
    if (!breath)
    {
        const bool loud = voiced && percentile_ >= settings_.loudPercentile;
        const bool quiet = !voiced || percentile_ < settings_.quietPercentile;
        loudFor_ = loud ? loudFor_ + dt : 0.0;
        quietFor_ = quiet ? quietFor_ + dt : 0.0;
    }
    if (!intense_ && loudFor_ >= holdSeconds_)
    {
        intense_ = true;
        quietFor_ = 0.0;
    }
    else if (intense_ && quietFor_ >= holdSeconds_)
    {
        intense_ = false;
        loudFor_ = 0.0;
    }
}

} // namespace lb
