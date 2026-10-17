#pragma once

#include <vector>

namespace lb
{

// Lowers the sample rate by an integer factor: a windowed-sinc low-pass FIR, evaluated
// only at the output rate. Used to bring the mic down to about 16 kHz for analysis.
class Decimator
{
public:
    // Not real-time safe. factor 1 passes samples straight through.
    void prepare(int factor);
    void reset() noexcept;

    int factor() const noexcept { return factor_; }

    // Delay of the filter, in input samples.
    int groupDelay() const noexcept { return static_cast<int>(taps_.size() - 1) / 2; }

    // Real-time safe. Feeds one input sample; returns true and sets `out` when an
    // output sample is ready (every `factor` inputs).
    bool push(float input, float& out) noexcept
    {
        const int n = static_cast<int>(taps_.size());
        history_[static_cast<std::size_t>(writePos_)] = input;
        history_[static_cast<std::size_t>(writePos_ + n)] = input;
        writePos_ = writePos_ + 1 == n ? 0 : writePos_ + 1;

        if (++phase_ < factor_)
            return false;
        phase_ = 0;

        // history_[writePos_ .. writePos_ + n) holds the last n inputs, oldest first.
        // The taps are symmetric, so no reversal is needed.
        const float* window = history_.data() + writePos_;
        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
            sum += taps_[static_cast<std::size_t>(i)] * window[i];
        out = sum;
        return true;
    }

private:
    int factor_ = 1;
    std::vector<float> taps_ { 1.0f };
    std::vector<float> history_ = std::vector<float>(2, 0.0f); // each input stored twice, so any window is contiguous
    int writePos_ = 0;
    int phase_ = 0;
};

} // namespace lb
