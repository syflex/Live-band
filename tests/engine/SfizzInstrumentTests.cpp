#include "engine/SfizzInstrument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;

struct Rendered
{
    std::vector<float> left;
    std::vector<float> right;
};

Rendered renderBlocks(lb::SfizzInstrument& instrument, int numBlocks)
{
    Rendered out;
    std::vector<float> l(blockSize), r(blockSize);
    for (int b = 0; b < numBlocks; ++b)
    {
        instrument.render(l.data(), r.data(), blockSize);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
}

float peakOf(const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (float x : samples)
        peak = std::max(peak, std::abs(x));
    return peak;
}

// Frequency estimate from upward zero crossings.
double estimateFrequency(const std::vector<float>& samples)
{
    int first = -1, last = -1, crossings = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
        {
            if (first < 0)
                first = static_cast<int>(i);
            last = static_cast<int>(i);
            ++crossings;
        }
    }
    if (crossings < 2)
        return 0.0;
    return (crossings - 1) * sampleRate / (last - first);
}
} // namespace

TEST_CASE("SfizzInstrument plays the generated test sine and releases it")
{
    lb::SfizzInstrument instrument;
    REQUIRE(instrument.load(LB_ASSETS_DIR "/sfz/test/sine.sfz"));
    instrument.prepare(sampleRate, blockSize);

    // Silent before any note.
    CHECK(peakOf(renderBlocks(instrument, 4).left) == 0.0f);

    instrument.noteOn(0, 69, 100); // A4
    const auto held = renderBlocks(instrument, 200); // about 0.5 s
    CHECK(instrument.activeVoices() == 1);
    CHECK(peakOf(held.left) > 0.02f);
    CHECK(peakOf(held.right) > 0.02f);

    // Skip the attack, then check the pitch.
    const std::vector<float> steady(held.left.begin() + 4800, held.left.end());
    CHECK(estimateFrequency(steady) == Catch::Approx(440.0).epsilon(0.01));

    instrument.noteOff(0, 69);
    renderBlocks(instrument, 200); // release is 0.25 s
    CHECK(peakOf(renderBlocks(instrument, 4).left) < 1e-4f);
    CHECK(instrument.activeVoices() == 0);
}

TEST_CASE("SfizzInstrument starts a note at its sample offset")
{
    lb::SfizzInstrument instrument;
    REQUIRE(instrument.load(LB_ASSETS_DIR "/sfz/test/sine.sfz"));
    instrument.prepare(sampleRate, blockSize);

    instrument.noteOn(100, 60, 127);
    const auto block = renderBlocks(instrument, 1);

    const auto firstSound = std::find_if(block.left.begin(), block.left.end(), [](float x) { return x != 0.0f; });
    REQUIRE(firstSound != block.left.end());
    CHECK(std::distance(block.left.begin(), firstSound) >= 100);
}

TEST_CASE("SfizzInstrument reports a missing file")
{
    lb::SfizzInstrument instrument;
    CHECK_FALSE(instrument.load(LB_ASSETS_DIR "/sfz/test/does-not-exist.sfz"));
    CHECK_FALSE(instrument.isLoaded());
}
