#include "music/ClickTrack.h"

#include <algorithm>
#include <numbers>

namespace lb
{
namespace
{
std::vector<float> makeClick(double sampleRate, double frequencyHz, float gain)
{
    // A short sine burst with a fast exponential decay: 25 ms, clearly pitched.
    const auto length = static_cast<std::size_t>(0.025 * sampleRate);
    std::vector<float> click(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double envelope = std::exp(-t / 0.006) * std::min(1.0, t / 0.0005);
        click[i] = gain * static_cast<float>(envelope * std::sin(2.0 * std::numbers::pi * frequencyHz * t));
    }
    return click;
}
} // namespace

void ClickTrack::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    accent_ = makeClick(sampleRate, 1500.0, 1.0f);
    beat_ = makeClick(sampleRate, 1000.0, 0.8f);
    subdivision_ = makeClick(sampleRate, 800.0, 0.45f);
    running_ = false;
    sound_ = nullptr;
}

void ClickTrack::start(const ClickSettings& settings, std::int64_t firstClickSample) noexcept
{
    settings_ = settings;
    settings_.beatsPerBar = std::max(1, settings_.beatsPerBar);
    settings_.clicksPerBeat = std::max(1, settings_.clicksPerBeat);
    settings_.countInBars = std::max(0, settings_.countInBars);
    settings_.rampBars = std::max(1, settings_.rampBars);
    settings_.bpm = std::clamp(settings_.bpm, 20.0, 400.0);

    nextClickTime_ = static_cast<double>(firstClickSample);
    clickIndex_ = 0;
    running_ = true;
}

double ClickTrack::bpmAtClick(std::int64_t clickIndex) const noexcept
{
    // Bars since the end of the count-in; the ramp is linear over rampBars.
    const double barsIn = static_cast<double>(clickIndex) / clicksPerBar() - settings_.countInBars;
    const double progress = std::clamp(barsIn / settings_.rampBars, 0.0, 1.0);
    return settings_.bpm * (1.0 + settings_.rampPercent / 100.0 * progress);
}

double ClickTrack::samplesUntilNextClick(std::int64_t clickIndex) const noexcept
{
    return 60.0 / bpmAtClick(clickIndex) / settings_.clicksPerBeat * sampleRate_;
}

ClickEvent ClickTrack::eventFor(std::int64_t clickIndex, std::int64_t sampleTime) const noexcept
{
    const int perBar = clicksPerBar();
    const auto withinBar = static_cast<int>(clickIndex % perBar);

    ClickEvent event;
    event.sampleTime = sampleTime;
    event.bar = static_cast<std::int32_t>(clickIndex / perBar) - settings_.countInBars;
    event.beat = static_cast<std::int16_t>(withinBar / settings_.clicksPerBeat);
    event.subdivision = static_cast<std::int16_t>(withinBar % settings_.clicksPerBeat);
    event.bpm = static_cast<float>(bpmAtClick(clickIndex));
    return event;
}

} // namespace lb
