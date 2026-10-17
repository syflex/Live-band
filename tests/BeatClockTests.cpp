#include "music/BeatClock.h"
#include "music/Groove.h"
#include "music/TapTempo.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using Catch::Approx;

TEST_CASE("swingBeat is the identity when straight and delays the off-sixteenths when swung")
{
    for (double b = 0.0; b < 4.0; b += 0.0625)
        CHECK(lb::swingBeat(b, 0.5) == b);

    // At 0.6, the first sixteenth of an eighth takes 60% of it: the second starts 0.05
    // beats late. Eighth notes themselves do not move.
    CHECK(lb::swingBeat(0.25, 0.6) == Approx(0.30));
    CHECK(lb::swingBeat(1.75, 0.6) == Approx(1.80));
    CHECK(lb::swingBeat(0.5, 0.6) == Approx(0.5));
    CHECK(lb::swingBeat(3.0, 0.6) == Approx(3.0));

    double previous = -1.0;
    for (double b = 0.0; b < 4.0; b += 0.01)
    {
        const double s = lb::swingBeat(b, 0.66);
        CHECK(s > previous);
        previous = s;
    }
}

TEST_CASE("Style::foldTempo moves a tapped tempo into the style's range by octaves")
{
    lb::Style style;
    style.minBpm = 90.0;
    style.defaultBpm = 104.0;
    style.maxBpm = 125.0;
    CHECK(style.foldTempo(104.0) == Approx(104.0));
    CHECK(style.foldTempo(55.0) == Approx(110.0));  // half time
    CHECK(style.foldTempo(220.0) == Approx(110.0)); // double time
    CHECK(style.foldTempo(30.0) == Approx(120.0));
    CHECK(style.foldTempo(126.0) == Approx(125.0)); // neither 126 nor 63 fits: the nearest edge
    CHECK(style.foldTempo(70.0) == Approx(125.0));  // 140 is nearer to the range than 70
}

TEST_CASE("BeatClock keeps time over 30 minutes of uneven blocks")
{
    constexpr double spb = 48000.0 * 60.0 / 104.0; // not a whole number of samples
    lb::BeatClock clock;
    clock.start(12345, 0.0, spb);

    std::mt19937 random(7);
    std::uniform_int_distribution<int> blockSize(1, 1024);
    std::int64_t t = 12345;
    const std::int64_t end = 12345 + static_cast<std::int64_t>(30 * 60 * 48000.0);
    double previous = -1.0;
    while (t < end)
    {
        const int n = blockSize(random);
        const auto span = clock.advance(t, n);
        REQUIRE(span.startBeat >= previous);
        CHECK(span.beatsPerSample == Approx(1.0 / spb).epsilon(1e-9)); // only rounding corrections
        previous = span.startBeat;
        t += n;
    }
    const double expected = static_cast<double>(t - 12345) / spb;
    CHECK(std::abs(clock.beatAt(t) - expected) < 1e-6); // under a microsecond of drift
}

TEST_CASE("BeatClock changes tempo without moving the current beat")
{
    lb::BeatClock clock;
    clock.start(0, 0.0, 24000.0);
    std::int64_t t = 0;
    for (; t < 48000; t += 480)
        clock.advance(t, 480);
    CHECK(clock.beatAt(t) == Approx(2.0));

    clock.setTempo(t, 12000.0);
    const auto span = clock.advance(t, 480);
    CHECK(span.startBeat == Approx(2.0));
    CHECK(span.beatsPerSample == Approx(1.0 / 12000.0));
    t += 480;
    for (; t < 48000 + 24000; t += 480)
        clock.advance(t, 480);
    CHECK(clock.beatAt(t) == Approx(4.0)); // two beats in the next half second
}

TEST_CASE("BeatClock steers onto a new phase gradually and never runs backwards")
{
    constexpr double spb = 24000.0;
    lb::BeatClock clock;
    clock.start(0, 0.0, spb);

    // The band is 0.3 beats late against the new target line.
    clock.steer(0, 0.3, spb);
    double previous = -1.0;
    std::int64_t t = 0;
    double maxDeviation = 0.0;
    for (; t < static_cast<std::int64_t>(12 * spb); t += 256)
    {
        const auto span = clock.advance(t, 256);
        REQUIRE(span.startBeat > previous);
        previous = span.startBeat;
        maxDeviation = std::max(maxDeviation, std::abs(span.beatsPerSample * spb - 1.0));
    }
    CHECK(maxDeviation <= 0.08 + 1e-12);
    CHECK(std::abs(clock.beatAt(t) - clock.targetBeatAt(t)) < 0.01); // caught up within 12 beats
}

TEST_CASE("TapTempo: four steady taps give the tempo and predict the fifth exactly")
{
    lb::TapTempo taps;
    const double period = 60.0 / 104.0;
    for (int i = 0; i < 4; ++i)
        taps.tap(10.0 + i * period);

    const auto e = taps.estimate();
    CHECK(e.taps == 4);
    CHECK(e.period == Approx(period).epsilon(1e-12));
    CHECK(e.bpm() == Approx(104.0));
    CHECK(e.timeOfTap(4) == Approx(10.0 + 4 * period).epsilon(1e-12));
}

TEST_CASE("TapTempo: least squares keeps jittery taps close to the true beat")
{
    std::mt19937 random(3);
    std::uniform_real_distribution<double> jitter(-0.010, 0.010); // a human within 10 ms
    const double period = 60.0 / 100.0;

    double worstTempo = 0.0, worstPrediction = 0.0;
    for (int trial = 0; trial < 200; ++trial)
    {
        lb::TapTempo taps;
        for (int i = 0; i < 8; ++i)
            taps.tap(i * period + jitter(random));
        const auto e = taps.estimate();
        REQUIRE(e.taps == 8);
        worstTempo = std::max(worstTempo, std::abs(e.period / period - 1.0));
        worstPrediction = std::max(worstPrediction, std::abs(e.timeOfTap(8) - 8 * period));
    }
    CHECK(worstTempo < 0.01);       // under 1% with eight taps
    CHECK(worstPrediction < 0.015); // the next beat within 15 ms
}

TEST_CASE("TapTempo starts a new run after a pause or an off-tempo tap, and ignores bounces")
{
    lb::TapTempo taps;
    CHECK(taps.tap(0.0) == 1);
    CHECK(taps.tap(0.6) == 2);
    CHECK(taps.tap(0.62) == 2); // a key bounce 20 ms later
    CHECK(taps.tap(1.2) == 3);
    CHECK(taps.tap(4.0) == 1);  // 2.8 s later: a new run
    CHECK(taps.tap(4.5) == 2);
    CHECK(taps.tap(5.0) == 3);
    CHECK(taps.tap(5.9) == 1);  // 0.9 s against a 0.5 s period: a new run
    CHECK(taps.estimate().period == 0.0);
}

TEST_CASE("TapTempo keeps the latest eight taps but counts the phase from the run's first tap")
{
    lb::TapTempo taps;
    const double period = 0.5;
    for (int i = 0; i < 12; ++i)
        taps.tap(3.0 + i * period);
    const auto e = taps.estimate();
    CHECK(e.taps == 12);
    CHECK(e.period == Approx(period));
    CHECK(e.phase == Approx(3.0));
    CHECK(e.timeOfTap(12) == Approx(3.0 + 12 * period));
}
