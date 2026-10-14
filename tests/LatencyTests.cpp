#include "audio/LatencyAnalysis.h"
#include "audio/LatencyProbe.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

// Simulates a device: whatever goes out comes back `delay` samples later, scaled,
// optionally inverted, with background noise.
class FakeLoopback
{
public:
    FakeLoopback(std::size_t delay, float gain, float noiseLevel)
        : line_(delay + 1, 0.0f), gain_(gain), noise_(0.0f, noiseLevel)
    {
    }

    void run(const float* out, float* in, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            line_[writeIndex_] = out[i];
            writeIndex_ = (writeIndex_ + 1) % line_.size();
            in[i] = gain_ * line_[writeIndex_] + noise_(rng_);
        }
    }

private:
    std::vector<float> line_;
    std::size_t writeIndex_ = 0;
    float gain_;
    std::mt19937 rng_ { 1234 };
    std::normal_distribution<float> noise_;
};
} // namespace

TEST_CASE("makeLogChirp stays within its amplitude and fades in and out")
{
    std::vector<float> chirp(2400);
    lb::makeLogChirp(chirp, sampleRate, 500.0, 6000.0, 0.3f);

    const float peak = std::abs(*std::max_element(chirp.begin(), chirp.end(),
                                                  [](float a, float b) { return std::abs(a) < std::abs(b); }));
    CHECK(peak <= 0.3f + 1e-6f);
    CHECK(peak > 0.25f);
    CHECK(std::abs(chirp.front()) < 1e-6f);
    CHECK(std::abs(chirp.back()) < 0.01f);
}

TEST_CASE("estimateDelay finds a chirp hidden in noise")
{
    std::vector<float> chirp(2400);
    lb::makeLogChirp(chirp, sampleRate, 500.0, 6000.0, 0.3f);

    const std::size_t delay = GENERATE(0, 1, 480, 1234, 9000);
    std::vector<float> recorded(24000, 0.0f);
    std::mt19937 rng(42);
    std::normal_distribution<float> noise(0.0f, 0.01f);
    for (auto& x : recorded)
        x = noise(rng);
    for (std::size_t i = 0; i < chirp.size(); ++i)
        recorded[delay + i] -= 0.2f * chirp[i]; // inverted and quieter, as a real path may be

    const auto estimate = lb::estimateDelay(recorded, chirp);
    CHECK(estimate.lagSamples == static_cast<long>(delay));
    CHECK(estimate.correlation > 0.8f);
}

TEST_CASE("estimateDelay reports no match for silence or bad input")
{
    std::vector<float> chirp(240);
    lb::makeLogChirp(chirp, sampleRate, 500.0, 6000.0, 0.3f);

    std::vector<float> silence(2000, 0.0f);
    CHECK(lb::estimateDelay(silence, chirp).lagSamples == -1);

    std::vector<float> tooShort(100, 0.1f);
    CHECK(lb::estimateDelay(tooShort, chirp).lagSamples == -1);
}

TEST_CASE("LatencyProbe measures the round trip of a simulated device")
{
    const std::size_t delay = GENERATE(0, 300, 731);
    const int blockSize = GENERATE(64, 128, 100);

    lb::LatencyProbe probe;
    probe.prepare(sampleRate);
    FakeLoopback device(delay, 0.5f, 0.005f);

    CHECK_FALSE(probe.takeResult().has_value());
    REQUIRE(probe.start());
    CHECK_FALSE(probe.start()); // already running

    std::vector<float> in(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> out(static_cast<std::size_t>(blockSize), 0.0f);
    std::optional<lb::DelayEstimate> result;

    // Like a duplex callback: each callback receives the input captured during the previous
    // block, so the simulated device adds one block on top of its own delay.
    for (int blocks = 0; blocks < 1000 && !result; ++blocks)
    {
        if (!probe.process(in.data(), out.data(), blockSize))
            std::fill(out.begin(), out.end(), 0.0f);
        device.run(out.data(), in.data(), blockSize);
        result = probe.takeResult();
    }

    REQUIRE(result.has_value());
    CHECK(result->lagSamples == static_cast<long>(delay) + blockSize);
    CHECK(result->correlation > 0.8f);
    CHECK_FALSE(probe.isBusy());
    CHECK_FALSE(probe.process(in.data(), out.data(), blockSize)); // idle again
}
