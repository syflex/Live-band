#include "music/VocalDynamics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;

namespace
{
constexpr double hop = 0.008;

// Sings at `levelDb` (with a gentle +-1 dB wobble) for `seconds`, with a breath of `gapSeconds`
// every `phraseSeconds` if given. Returns the time after the last frame.
double sing(lb::VocalDynamics& dynamics, double start, double seconds, double levelDb, double phraseSeconds = 0.0, double gapSeconds = 0.0)
{
    double t = start;
    for (; t < start + seconds; t += hop)
    {
        const bool breathing = phraseSeconds > 0.0 && std::fmod(t - start, phraseSeconds) > phraseSeconds - gapSeconds;
        const double wobble = std::sin(2.0 * 3.141592653589793 * 0.7 * t);
        dynamics.process(t, !breathing, static_cast<float>(levelDb + wobble));
    }
    return t;
}

double silence(lb::VocalDynamics& dynamics, double start, double seconds)
{
    double t = start;
    for (; t < start + seconds; t += hop)
        dynamics.process(t, false, -80.0f);
    return t;
}
} // namespace

TEST_CASE("VocalDynamics stays neutral until it has heard enough singing")
{
    lb::VocalDynamics dynamics;
    sing(dynamics, 0.0, 5.0, -10.0);
    CHECK(dynamics.percentile() == Approx(0.5));
    CHECK(dynamics.velocityScale() == Approx(1.0));
    CHECK_FALSE(dynamics.intense());
}

TEST_CASE("VocalDynamics: the singer's usual level is neutral; louder and quieter move the velocities")
{
    lb::VocalDynamics dynamics;
    double t = sing(dynamics, 0.0, 40.0, -20.0);
    CHECK(dynamics.percentile() == Approx(0.5).margin(0.2));
    CHECK(dynamics.velocityScale() == Approx(1.0).margin(0.07));

    t = sing(dynamics, t, 4.0, -12.0); // a louder passage
    CHECK(dynamics.percentile() > 0.9);
    CHECK(dynamics.velocityScale() > 1.12);
    CHECK(dynamics.velocityScale() <= 1.15);

    t = sing(dynamics, t, 6.0, -28.0); // a quiet one
    CHECK(dynamics.percentile() < 0.1);
    CHECK(dynamics.velocityScale() < 0.84);
    CHECK(dynamics.velocityScale() >= 0.8);
}

TEST_CASE("VocalDynamics turns intense after two bars of loud singing, breaths included, and calms down after two quiet bars")
{
    lb::VocalDynamics dynamics;
    dynamics.setHoldSeconds(4.0);
    double t = sing(dynamics, 0.0, 40.0, -20.0, 3.0, 0.5);
    CHECK_FALSE(dynamics.intense());

    // Loud, with a half-second breath every 3 s: not after 3 s, but after the hold time.
    t = sing(dynamics, t, 3.0, -11.0, 3.0, 0.5);
    CHECK_FALSE(dynamics.intense());
    t = sing(dynamics, t, 5.0, -11.0, 3.0, 0.5);
    CHECK(dynamics.intense());

    // Back to the usual level: it stays intense for a little while, then calms down.
    t = sing(dynamics, t, 2.0, -20.0, 3.0, 0.5);
    CHECK(dynamics.intense());
    t = sing(dynamics, t, 6.0, -20.0, 3.0, 0.5);
    CHECK_FALSE(dynamics.intense());
}

TEST_CASE("VocalDynamics relaxes to neutral and calms down when the singing stops")
{
    lb::VocalDynamics dynamics;
    dynamics.setHoldSeconds(4.0);
    double t = sing(dynamics, 0.0, 40.0, -20.0);
    t = sing(dynamics, t, 8.0, -10.0);
    REQUIRE(dynamics.intense());
    REQUIRE(dynamics.velocityScale() > 1.1);

    t = silence(dynamics, t, 15.0);
    CHECK_FALSE(dynamics.intense());
    CHECK(dynamics.velocityScale() == Approx(1.0).margin(0.02));
}
