#include "music/ClickTrack.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using Catch::Approx;

namespace
{
constexpr double rate = 48000.0;

std::vector<lb::ClickEvent> runClicks(lb::ClickTrack& track, double seconds, int blockSize, std::vector<float>* audio = nullptr)
{
    std::vector<lb::ClickEvent> events;
    std::vector<float> block(static_cast<std::size_t>(blockSize));
    const auto total = static_cast<std::int64_t>(seconds * rate);
    for (std::int64_t start = 0; start < total; start += blockSize)
    {
        std::fill(block.begin(), block.end(), 0.0f);
        track.process(block.data(), blockSize, start, [&](const lb::ClickEvent& e) { events.push_back(e); });
        if (audio != nullptr)
            audio->insert(audio->end(), block.begin(), block.end());
    }
    return events;
}
} // namespace

TEST_CASE("ClickTrack clicks on time at a steady tempo, with a count-in")
{
    lb::ClickTrack track;
    track.prepare(rate);
    lb::ClickSettings settings;
    settings.bpm = 120.0;
    track.start(settings, 1000);

    std::vector<float> audio;
    const auto events = runClicks(track, 4.0, 100, &audio);

    REQUIRE(events.size() == 8); // 2 clicks per second for 4 s
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        CHECK(events[i].sampleTime == 1000 + static_cast<std::int64_t>(i) * 24000);
        CHECK(events[i].beat == static_cast<int>(i % 4));
        CHECK(events[i].bar == static_cast<int>(i / 4) - 1); // bar -1 is the count-in
    }

    // The sound starts exactly at the click time.
    CHECK(audio[999] == 0.0f);
    CHECK(audio[1000 + 30] != 0.0f);
    CHECK(audio[1000 + 24000 - 1] == 0.0f);
}

TEST_CASE("ClickTrack 6/8 clicks every eighth note, two beats per bar")
{
    lb::ClickTrack track;
    track.prepare(rate);
    lb::ClickSettings settings;
    settings.bpm = 60.0;   // dotted-quarter beats
    settings.beatsPerBar = 2;
    settings.clicksPerBeat = 3;
    settings.countInBars = 0;
    track.start(settings, 0);

    const auto events = runClicks(track, 2.0, 128);
    REQUIRE(events.size() == 6);
    CHECK(events[1].sampleTime - events[0].sampleTime == 16000);
    CHECK(events[3].beat == 1);
    CHECK(events[3].subdivision == 0);
    CHECK(events[4].subdivision == 1);
}

TEST_CASE("ClickTrack ramps the tempo after the count-in")
{
    lb::ClickTrack track;
    track.prepare(rate);
    lb::ClickSettings settings;
    settings.bpm = 100.0;
    settings.rampPercent = 5.0;
    settings.rampBars = 4;
    track.start(settings, 0);

    CHECK(track.bpmAtClick(0) == Approx(100.0));  // count-in
    CHECK(track.bpmAtClick(4) == Approx(100.0));  // first bar after the count-in
    CHECK(track.bpmAtClick(12) == Approx(102.5)); // halfway through the ramp
    CHECK(track.bpmAtClick(20) == Approx(105.0)); // ramp done
    CHECK(track.bpmAtClick(400) == Approx(105.0));

    const auto events = runClicks(track, 20.0, 256);
    REQUIRE(events.size() > 30);
    const double firstGap = static_cast<double>(events[1].sampleTime - events[0].sampleTime);
    const double lastGap = static_cast<double>(events.back().sampleTime - events[events.size() - 2].sampleTime);
    CHECK(firstGap / lastGap == Approx(1.05).epsilon(0.002));
}

TEST_CASE("ClickTrack is silent when stopped")
{
    lb::ClickTrack track;
    track.prepare(rate);
    std::vector<float> audio;
    CHECK(runClicks(track, 1.0, 128, &audio).empty());
    CHECK(std::all_of(audio.begin(), audio.end(), [](float x) { return x == 0.0f; }));
}
