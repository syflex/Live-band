#include "SignalHelpers.h"
#include "audio/Biquad.h"
#include "audio/Decimator.h"
#include "audio/Fft.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using Catch::Approx;

namespace
{
std::vector<float> decimate(lb::Decimator& decimator, const std::vector<float>& input)
{
    std::vector<float> out;
    for (float x : input)
    {
        float y = 0.0f;
        if (decimator.push(x, y))
            out.push_back(y);
    }
    return out;
}

float steadyRms(const std::vector<float>& signal)
{
    // Skip the first and last quarter to stay clear of filter start-up.
    const std::vector<float> middle(signal.begin() + static_cast<long>(signal.size() / 4),
                                    signal.end() - static_cast<long>(signal.size() / 4));
    return test::rmsOf(middle);
}
} // namespace

TEST_CASE("Decimator by 3 keeps the pass band and removes what would alias")
{
    lb::Decimator decimator;
    decimator.prepare(3);
    CHECK(decimator.groupDelay() == 48);

    const double rate = 48000.0;
    const float inRms = test::rmsOf(test::sine(rate, 1000.0, 1.0, 0.5f));

    decimator.reset();
    const auto passBand = decimate(decimator, test::sine(rate, 1000.0, 1.0, 0.5f));
    CHECK(passBand.size() == 16000);
    CHECK(steadyRms(passBand) == Approx(inRms).epsilon(0.02));

    decimator.reset();
    const auto edge = decimate(decimator, test::sine(rate, 6000.0, 1.0, 0.5f));
    CHECK(steadyRms(edge) > 0.8f * inRms); // 6 kHz is still well inside the pass band

    decimator.reset();
    const auto stopBand = decimate(decimator, test::sine(rate, 12000.0, 1.0, 0.5f));
    CHECK(20.0f * std::log10(steadyRms(stopBand) / inRms) < -50.0f);
}

TEST_CASE("Decimator with factor 1 passes samples through")
{
    lb::Decimator decimator;
    decimator.prepare(1);
    const std::vector<float> input { 0.1f, -0.2f, 0.3f };
    CHECK(decimate(decimator, input) == input);
    CHECK(decimator.groupDelay() == 0);
}

TEST_CASE("Fft matches a direct DFT")
{
    const int n = 64;
    lb::Fft fft;
    fft.prepare(n);

    std::mt19937 rng(3);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<std::complex<float>> data(n);
    for (auto& x : data)
        x = { dist(rng), dist(rng) };

    std::vector<std::complex<double>> expected(n);
    for (int k = 0; k < n; ++k)
        for (int t = 0; t < n; ++t)
            expected[static_cast<std::size_t>(k)] += std::complex<double>(data[static_cast<std::size_t>(t)])
                                                     * std::polar(1.0, -2.0 * std::numbers::pi * k * t / n);

    fft.forward(data.data());
    for (int k = 0; k < n; ++k)
    {
        CHECK(data[static_cast<std::size_t>(k)].real() == Approx(expected[static_cast<std::size_t>(k)].real()).margin(1e-4));
        CHECK(data[static_cast<std::size_t>(k)].imag() == Approx(expected[static_cast<std::size_t>(k)].imag()).margin(1e-4));
    }
}

TEST_CASE("Biquad high-pass removes DC and keeps the voice range")
{
    lb::Biquad filter;
    filter.setHighPass(16000.0, 50.0);

    float y = 0.0f;
    for (int i = 0; i < 16000; ++i)
        y = filter.process(1.0f);
    CHECK(std::abs(y) < 1e-3f);

    filter.reset();
    auto tone = test::sine(16000.0, 200.0, 1.0, 0.5f);
    for (auto& x : tone)
        x = filter.process(x);
    CHECK(steadyRms(tone) == Approx(0.5f / std::numbers::sqrt2_v<float>).epsilon(0.03));
}
