#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace lb
{

struct ClickSettings
{
    double bpm = 100.0;       // beats per minute; in 6/8 a beat is a dotted quarter
    int beatsPerBar = 4;      // 4 for 4/4, 2 for 6/8
    int clicksPerBeat = 1;    // 1 for 4/4, 3 for 6/8 (a click on every eighth note)
    int countInBars = 1;
    double rampPercent = 0.0; // tempo change reached after rampBars, e.g. +5 or -5
    int rampBars = 16;
    float level = 0.5f;
};

struct ClickEvent
{
    std::int64_t sampleTime = 0;
    std::int32_t bar = 0;          // bar number; negative during the count-in
    std::int16_t beat = 0;         // beat within the bar, from 0
    std::int16_t subdivision = 0;  // click within the beat, from 0
    float bpm = 0.0f;              // tempo at this click
};

// A metronome for recording test clips: accented downbeats, a count-in, 4/4 or 6/8, and an
// optional gradual tempo change so recordings can test tempo following later.
//
// Threads: prepare() is not real-time safe; everything else is.
class ClickTrack
{
public:
    void prepare(double sampleRate);

    void start(const ClickSettings& settings, std::int64_t firstClickSample) noexcept;
    void stop() noexcept { running_ = false; }
    bool isRunning() const noexcept { return running_; }

    // Adds the clicks that fall in [blockStart, blockStart + numSamples) to `out`, and calls
    // onClick(const ClickEvent&) as each one starts.
    template <typename OnClick>
    void process(float* out, int numSamples, std::int64_t blockStart, OnClick&& onClick) noexcept
    {
        int position = 0;
        while (position < numSamples)
        {
            const std::int64_t nextClick = running_ ? std::llround(nextClickTime_) : std::numeric_limits<std::int64_t>::max();
            const bool clickInBlock = nextClick < blockStart + numSamples;
            const int segmentEnd = clickInBlock ? std::max(position, static_cast<int>(nextClick - blockStart)) : numSamples;

            if (sound_ != nullptr)
            {
                const int soundLength = static_cast<int>(sound_->size());
                for (int i = position; i < segmentEnd && soundPosition_ < soundLength; ++i)
                    out[i] += settings_.level * (*sound_)[static_cast<std::size_t>(soundPosition_++)];
            }
            position = segmentEnd;

            if (clickInBlock)
            {
                onClick(eventFor(clickIndex_, nextClick));
                const int clickInBar = static_cast<int>(clickIndex_ % clicksPerBar());
                sound_ = clickInBar == 0 ? &accent_ : (clickInBar % settings_.clicksPerBeat == 0 ? &beat_ : &subdivision_);
                soundPosition_ = 0;
                nextClickTime_ += samplesUntilNextClick(clickIndex_);
                ++clickIndex_;
            }
        }
    }

    // Tempo for a given click, following the ramp. Exposed for tests.
    double bpmAtClick(std::int64_t clickIndex) const noexcept;

private:
    int clicksPerBar() const noexcept { return settings_.beatsPerBar * settings_.clicksPerBeat; }
    double samplesUntilNextClick(std::int64_t clickIndex) const noexcept;
    ClickEvent eventFor(std::int64_t clickIndex, std::int64_t sampleTime) const noexcept;

    double sampleRate_ = 48000.0;
    ClickSettings settings_;
    bool running_ = false;
    double nextClickTime_ = 0.0;
    std::int64_t clickIndex_ = 0;

    std::vector<float> accent_, beat_, subdivision_;
    const std::vector<float>* sound_ = nullptr;
    int soundPosition_ = 0;
};

} // namespace lb
