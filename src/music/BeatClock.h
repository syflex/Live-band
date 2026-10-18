#pragma once

#include <cstdint>

namespace lb
{

// Maps the sample timeline to musical beats while the band plays.
//
// The clock follows a target line, "beat B falls on sample S at T samples per beat". A new
// tempo takes effect at once, without moving the current beat. A phase correction (steer)
// is approached gradually: the clock runs at most maxRateDeviation faster or slower than
// the target tempo until it is back on the line, so the band never jumps or runs backwards.
//
// Real-time safe. Used by one thread.
class BeatClock
{
public:
    struct Settings
    {
        double maxRateDeviation = 0.08; // the largest catch-up speed-up or slow-down
        double correctionBeats = 1.0;   // time constant of the phase correction
    };

    struct Span
    {
        double startBeat = 0.0;      // the beat at the first sample of the block
        double beatsPerSample = 0.0; // constant through the block
    };

    void setSettings(const Settings& settings) noexcept { settings_ = settings; }

    // Beat `beat` falls on sample `sample`. The clock jumps there.
    void start(std::int64_t sample, double beat, double samplesPerBeat) noexcept;

    // A new tempo from `sample` on, keeping the beat the clock has there.
    void setTempo(std::int64_t sample, double samplesPerBeat) noexcept;

    // A new target line, approached without a jump.
    void steer(std::int64_t anchorSample, double anchorBeat, double samplesPerBeat) noexcept;

    // Moves the clock across [blockStart, blockStart + numSamples). Blocks must follow each
    // other without gaps or overlaps after start().
    Span advance(std::int64_t blockStart, int numSamples) noexcept;

    // The beat at `sample`, extrapolated at the current rate.
    double beatAt(std::int64_t sample) const noexcept;

    // The beat the target line puts at `sample`.
    double targetBeatAt(std::int64_t sample) const noexcept;

    double samplesPerBeat() const noexcept { return 1.0 / targetRate_; }
    double currentBeatsPerSample() const noexcept { return rate_; }
    std::int64_t position() const noexcept { return position_; }

private:
    Settings settings_;
    std::int64_t position_ = 0; // the sample at which beat_ holds
    double beat_ = 0.0;
    double rate_ = 1.0 / 24000.0; // beats per sample in the last block

    std::int64_t anchorSample_ = 0;
    double anchorBeat_ = 0.0;
    double targetRate_ = 1.0 / 24000.0;
};

} // namespace lb
