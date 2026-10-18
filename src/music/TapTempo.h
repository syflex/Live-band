#pragma once

#include <array>
#include <cstddef>

namespace lb
{

// Turns taps (a key, a pedal, a pad) into a tempo and a beat phase.
//
// A run of taps is fitted with a straight line, tap time = phase + index * period, by least
// squares, so one early or late tap moves the estimate much less than it would with the last
// interval alone. A run restarts after a long pause, or when an interval is far from the
// current period (a new tempo, or a stray tap).
//
// Times are in seconds on any steady clock. Not thread-safe; used from one thread.
class TapTempo
{
public:
    struct Settings
    {
        double resetGapSeconds = 2.0;  // a longer pause starts a new run
        double outlierFraction = 0.3;  // an interval this far from the period starts a new run
        double minPeriodSeconds = 0.2; // 300 BPM
    };

    struct Estimate
    {
        int taps = 0;           // in the current run
        double period = 0.0;    // seconds per beat
        double phase = 0.0;     // fitted time of the run's first tap
        double lastTap = 0.0;

        // The fitted time of tap `index` of the run (0 is the first tap).
        double timeOfTap(int index) const noexcept { return phase + index * period; }
        double bpm() const noexcept { return period > 0.0 ? 60.0 / period : 0.0; }
    };

    static constexpr int maxTaps = 8; // the fit uses at most the latest eight taps

    void setSettings(const Settings& settings) noexcept { settings_ = settings; }

    // Adds a tap. Returns the number of taps in the current run.
    int tap(double seconds) noexcept;
    void reset() noexcept { count_ = 0; total_ = 0; head_ = 0; }

    int count() const noexcept { return total_; }
    // Needs two taps or more; otherwise taps == count() and period is 0.
    Estimate estimate() const noexcept;

private:
    Settings settings_;
    std::array<double, maxTaps> times_ {}; // ring buffer of the latest taps
    int count_ = 0;  // taps in the ring buffer
    int total_ = 0;  // taps in the run
    int head_ = 0;   // index of the oldest tap in the ring buffer
};

} // namespace lb
