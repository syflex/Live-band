#include "audio/LatencyProbe.h"

#include <algorithm>

namespace lb
{

void LatencyProbe::prepare(double sampleRate)
{
    state_.store(State::idle, std::memory_order_release);
    chirp_.assign(static_cast<std::size_t>(chirpSeconds * sampleRate), 0.0f);
    makeLogChirp(chirp_, sampleRate, chirpStartHz, chirpEndHz, chirpAmplitude);
    recording_.assign(static_cast<std::size_t>(recordSeconds * sampleRate), 0.0f);
    position_ = 0;
}

bool LatencyProbe::start()
{
    if (recording_.empty() || state_.load(std::memory_order_acquire) != State::idle)
        return false;

    position_ = 0;
    // Release: the audio thread sees position_ == 0 once it sees `running`.
    state_.store(State::running, std::memory_order_release);
    return true;
}

std::optional<DelayEstimate> LatencyProbe::takeResult()
{
    if (state_.load(std::memory_order_acquire) != State::finished)
        return std::nullopt;

    const DelayEstimate estimate = estimateDelay(recording_, chirp_);
    state_.store(State::idle, std::memory_order_release);
    return estimate;
}

bool LatencyProbe::process(const float* input, float* output, int numSamples) noexcept
{
    if (state_.load(std::memory_order_acquire) != State::running)
        return false;

    for (int i = 0; i < numSamples; ++i)
    {
        const std::size_t pos = position_ + static_cast<std::size_t>(i);
        output[i] = pos < chirp_.size() ? chirp_[pos] : 0.0f;
        if (pos < recording_.size())
            recording_[pos] = input != nullptr ? input[i] : 0.0f;
    }

    position_ += static_cast<std::size_t>(std::max(numSamples, 0));
    if (position_ >= recording_.size())
        state_.store(State::finished, std::memory_order_release); // publishes recording_

    return true;
}

} // namespace lb
