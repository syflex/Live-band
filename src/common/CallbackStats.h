#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace lb
{

// Timing statistics for the audio callback. The audio thread calls record() once per
// callback; any other thread may read a snapshot. Values are individually atomic, which
// is all a display needs.
class CallbackStats
{
public:
    // A callback that uses more than this fraction of its time budget counts as slow:
    // it did not drop out, but it came close.
    static constexpr float slowThreshold = 0.8f;

    struct Snapshot
    {
        std::uint64_t callbacks = 0;
        std::uint64_t slowCallbacks = 0; // load above slowThreshold
        std::uint64_t overruns = 0;      // load above 1.0: the callback took longer than the buffer lasts
        float lastLoad = 0.0f;           // fraction of the time budget used by the latest callback
        float averageLoad = 0.0f;        // exponential average over roughly the last second
        float peakLoad = 0.0f;           // highest load since the last resetPeak()
    };

    // Audio thread only.
    void record(double elapsedSeconds, double budgetSeconds) noexcept
    {
        if (budgetSeconds <= 0.0)
            return;

        const auto load = static_cast<float>(elapsedSeconds / budgetSeconds);

        if (resetRequested_.exchange(false, std::memory_order_acq_rel))
            peakLoad_.store(0.0f, std::memory_order_relaxed);

        // About one second of smoothing, whatever the buffer size.
        const auto alpha = static_cast<float>(std::min(1.0, budgetSeconds));
        const float average = averageLoad_.load(std::memory_order_relaxed);
        averageLoad_.store(average + alpha * (load - average), std::memory_order_relaxed);

        lastLoad_.store(load, std::memory_order_relaxed);
        if (load > peakLoad_.load(std::memory_order_relaxed))
            peakLoad_.store(load, std::memory_order_relaxed);

        if (load > slowThreshold)
            slowCallbacks_.fetch_add(1, std::memory_order_relaxed);
        if (load > 1.0f)
            overruns_.fetch_add(1, std::memory_order_relaxed);

        callbacks_.fetch_add(1, std::memory_order_relaxed);
    }

    // Any thread.
    Snapshot snapshot() const noexcept
    {
        Snapshot s;
        s.callbacks = callbacks_.load(std::memory_order_relaxed);
        s.slowCallbacks = slowCallbacks_.load(std::memory_order_relaxed);
        s.overruns = overruns_.load(std::memory_order_relaxed);
        s.lastLoad = lastLoad_.load(std::memory_order_relaxed);
        s.averageLoad = averageLoad_.load(std::memory_order_relaxed);
        s.peakLoad = peakLoad_.load(std::memory_order_relaxed);
        return s;
    }

    // Any thread. The audio thread clears the peak at its next callback.
    void resetPeak() noexcept { resetRequested_.store(true, std::memory_order_release); }

private:
    std::atomic<std::uint64_t> callbacks_ { 0 };
    std::atomic<std::uint64_t> slowCallbacks_ { 0 };
    std::atomic<std::uint64_t> overruns_ { 0 };
    std::atomic<float> lastLoad_ { 0.0f };
    std::atomic<float> averageLoad_ { 0.0f };
    std::atomic<float> peakLoad_ { 0.0f };
    std::atomic<bool> resetRequested_ { false };
};

} // namespace lb
