#include "SignalHelpers.h"
#include "audio/YinPitch.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

namespace
{
constexpr double rate = 16000.0;

double centsBetween(double a, double b)
{
    return 1200.0 * std::log2(a / b);
}

// Runs YIN over a signal at every hop and returns the median absolute error in cents
// of the confident frames, and the fraction of frames that were confident.
struct YinRun
{
    double medianErrorCents = 1e9;
    double confidentFraction = 0.0;
};

YinRun runYin(const std::vector<float>& signal, double expectedHz, const lb::YinPitch::Settings& settings = {})
{
    lb::YinPitch yin;
    yin.prepare(rate, settings);
    std::vector<double> errors;
    int frames = 0;
    for (std::size_t start = 0; start + static_cast<std::size_t>(yin.requiredInput()) <= signal.size(); start += 128)
    {
        ++frames;
        const auto estimate = yin.estimate(signal.data() + start);
        if (estimate.confident)
            errors.push_back(std::abs(centsBetween(estimate.frequencyHz, expectedHz)));
    }

    YinRun run;
    run.confidentFraction = frames > 0 ? static_cast<double>(errors.size()) / frames : 0.0;
    if (!errors.empty())
    {
        std::nth_element(errors.begin(), errors.begin() + static_cast<long>(errors.size() / 2), errors.end());
        run.medianErrorCents = errors[errors.size() / 2];
    }
    return run;
}
} // namespace

TEST_CASE("YinPitch is accurate to 10 cents on clean tones from C2 to C6")
{
    const int midi = GENERATE(36, 40, 45, 48, 52, 57, 60, 64, 69, 72, 76, 81, 84);
    const double hz = test::midiToHz(midi);
    INFO("MIDI note " << midi << " (" << hz << " Hz)");

    const auto sineRun = runYin(test::sine(rate, hz, 0.5, 0.5f), hz);
    CHECK(sineRun.confidentFraction > 0.95);
    CHECK(sineRun.medianErrorCents < 10.0);

    const auto toneRun = runYin(test::harmonicTone(rate, hz, 0.5, 0.5f), hz);
    CHECK(toneRun.confidentFraction > 0.95);
    CHECK(toneRun.medianErrorCents < 10.0);

    const auto pulseRun = runYin(test::pulseTrain(rate, hz, 0.5, 0.5f), hz);
    CHECK(pulseRun.confidentFraction > 0.95);
    CHECK(pulseRun.medianErrorCents < 10.0);
}

TEST_CASE("YinPitch stays within 10 cents with noise 20 dB below the voice")
{
    const int midi = GENERATE(40, 52, 60, 69, 79);
    const double hz = test::midiToHz(midi);
    INFO("MIDI note " << midi);

    auto signal = test::harmonicTone(rate, hz, 0.5, 0.3f);
    test::addNoise(signal, test::rmsOf(signal) * 0.1f);

    const auto run = runYin(signal, hz);
    CHECK(run.confidentFraction > 0.9);
    CHECK(run.medianErrorCents < 10.0);
}

TEST_CASE("YinPitch sub-octave check fixes octave-too-high answers when even harmonics dominate")
{
    const double hz = GENERATE(165.0, 220.0);
    INFO(hz << " Hz");
    const auto signal = test::evenHeavyTone(rate, hz, 0.5, 0.1);

    lb::YinPitch::Settings withoutCheck;
    withoutCheck.octaveCheckRatio = 0.0f;
    CHECK(runYin(signal, 2.0 * hz, withoutCheck).medianErrorCents < 10.0); // plain YIN answers an octave high

    const auto run = runYin(signal, hz);
    CHECK(run.confidentFraction > 0.95);
    CHECK(run.medianErrorCents < 10.0);
}

TEST_CASE("YinPitch is not confident on silence or noise")
{
    lb::YinPitch yin;
    yin.prepare(rate);

    const std::vector<float> silence(static_cast<std::size_t>(yin.requiredInput()), 0.0f);
    CHECK_FALSE(yin.estimate(silence.data()).confident);

    std::vector<float> noise(static_cast<std::size_t>(yin.requiredInput()) * 20, 0.0f);
    test::addNoise(noise, 0.1f, 11);
    int confident = 0, frames = 0;
    for (std::size_t start = 0; start + static_cast<std::size_t>(yin.requiredInput()) <= noise.size(); start += 128, ++frames)
        if (yin.estimate(noise.data() + start).confident)
            ++confident;
    CHECK(confident == 0);
}

TEST_CASE("YinPitch window and lag range follow the sample rate")
{
    lb::YinPitch yin;
    yin.prepare(16000.0);
    CHECK(yin.windowSize() == 512);
    CHECK(yin.maxLag() == 248);

    yin.prepare(14700.0);
    CHECK(yin.windowSize() == 470);
    CHECK(yin.maxLag() == 228);
}
