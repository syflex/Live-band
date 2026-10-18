#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace lb
{

// Converts times on the system clock (taps, MIDI timestamps) to the audio sample timeline.
//
// The audio thread calls update() at the start of every callback with the callback's first
// sample and the clock time. A callback can start late but never early, so the clock time
// minus the sample's time is smallest on the punctual callbacks; the map keeps that minimum,
// allowing it to creep up by 0.1 ms per second so that it tracks the drift between the audio
// clock and the system clock.
//
// update() is called by one thread (the audio thread); sampleAt() from any thread.
class TimeMap
{
public:
    static constexpr double creepPerSecond = 1.0e-4;

    void reset() noexcept { valid_.store(false); }

    // Audio thread.
    void update(std::int64_t sample, double seconds, double sampleRate) noexcept
    {
        const double offset = seconds - static_cast<double>(sample) / sampleRate;
        if (!hasEstimate_)
        {
            estimate_ = offset;
            hasEstimate_ = true;
        }
        else
        {
            const double elapsed = static_cast<double>(sample - lastSample_) / sampleRate;
            estimate_ = std::min(offset, estimate_ + creepPerSecond * elapsed);
        }
        lastSample_ = sample;

        // Seqlock: odd while writing.
        const auto sequence = sequence_.load(std::memory_order_relaxed);
        sequence_.store(sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        publishedOffset_.store(estimate_, std::memory_order_relaxed);
        publishedRate_.store(sampleRate, std::memory_order_relaxed);
        sequence_.store(sequence + 2, std::memory_order_release);
        valid_.store(true, std::memory_order_release);
    }

    void restart() noexcept { hasEstimate_ = false; } // audio thread, when the device (and maybe its rate) restarts

    // Any thread. Returns false until the first callback.
    bool sampleAt(double seconds, double& sample) const noexcept
    {
        if (!valid_.load(std::memory_order_acquire))
            return false;
        double offset = 0.0, rate = 0.0;
        for (;;)
        {
            const auto before = sequence_.load(std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;
            offset = publishedOffset_.load(std::memory_order_relaxed);
            rate = publishedRate_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_relaxed) == before)
                break;
        }
        sample = (seconds - offset) * rate;
        return true;
    }

private:
    // Audio thread only.
    bool hasEstimate_ = false;
    double estimate_ = 0.0;
    std::int64_t lastSample_ = 0;

    std::atomic<std::uint32_t> sequence_ { 0 };
    std::atomic<double> publishedOffset_ { 0.0 };
    std::atomic<double> publishedRate_ { 48000.0 };
    std::atomic<bool> valid_ { false };
};

} // namespace lb
