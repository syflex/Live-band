#pragma once

#include "audio/LatencyAnalysis.h"

#include <atomic>
#include <optional>
#include <vector>

namespace lb
{

// Measures round-trip latency: it plays a short chirp on the outputs, records the input
// at the same time, and finds where the chirp shows up in the recording.
//
// Threads: prepare(), start() and takeResult() run on a non-real-time thread (the UI);
// process() runs on the audio thread and never allocates.
class LatencyProbe
{
public:
    static constexpr double chirpSeconds = 0.05;
    static constexpr double recordSeconds = 0.5;
    static constexpr double chirpStartHz = 500.0;
    static constexpr double chirpEndHz = 6000.0;
    static constexpr float chirpAmplitude = 0.3f;

    // Non-real-time. Allocates the buffers; call while the audio callback is not running.
    void prepare(double sampleRate);

    // Non-real-time. Starts one measurement. Returns false if one is already running
    // or prepare() has not been called.
    bool start();

    // Non-real-time. Once the recording is complete, analyses it and returns to idle.
    // Returns nothing while a measurement is still running or when idle.
    std::optional<DelayEstimate> takeResult();

    bool isBusy() const noexcept { return state_.load(std::memory_order_acquire) != State::idle; }

    // Audio thread. While a measurement runs, records `input` (which may be null) and
    // writes the probe signal into `output`, then returns true: the caller should send
    // `output` to every channel instead of its normal signal. Returns false when idle.
    bool process(const float* input, float* output, int numSamples) noexcept;

private:
    enum class State
    {
        idle,
        running,
        finished
    };

    std::atomic<State> state_ { State::idle };
    std::vector<float> chirp_;
    std::vector<float> recording_;
    std::size_t position_ = 0; // audio thread only while running
};

} // namespace lb
