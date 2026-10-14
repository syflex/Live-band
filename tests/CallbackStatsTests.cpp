#include "common/CallbackStats.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using lb::CallbackStats;

TEST_CASE("CallbackStats counts slow callbacks and overruns")
{
    CallbackStats stats;
    const double budget = 128.0 / 48000.0;

    stats.record(0.10 * budget, budget);
    stats.record(0.85 * budget, budget);
    stats.record(1.20 * budget, budget);

    const auto s = stats.snapshot();
    CHECK(s.callbacks == 3);
    CHECK(s.slowCallbacks == 2);
    CHECK(s.overruns == 1);
    CHECK(s.lastLoad == Approx(1.2f));
    CHECK(s.peakLoad == Approx(1.2f));
}

TEST_CASE("CallbackStats peak resets on the next callback after resetPeak")
{
    CallbackStats stats;
    const double budget = 0.001;

    stats.record(0.9 * budget, budget);
    stats.resetPeak();
    CHECK(stats.snapshot().peakLoad == Approx(0.9f)); // not cleared until the audio thread runs

    stats.record(0.2 * budget, budget);
    CHECK(stats.snapshot().peakLoad == Approx(0.2f));
}

TEST_CASE("CallbackStats average converges to a steady load within about a second")
{
    CallbackStats stats;
    const double budget = 128.0 / 48000.0;
    const int callbacksPerSecond = static_cast<int>(1.0 / budget);

    for (int i = 0; i < 3 * callbacksPerSecond; ++i)
        stats.record(0.25 * budget, budget);

    CHECK(stats.snapshot().averageLoad == Approx(0.25f).margin(0.02));
}

TEST_CASE("CallbackStats ignores a zero budget")
{
    CallbackStats stats;
    stats.record(1.0, 0.0);
    CHECK(stats.snapshot().callbacks == 0);
}
