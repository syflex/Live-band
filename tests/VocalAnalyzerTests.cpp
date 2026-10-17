#include "SignalHelpers.h"
#include "audio/VocalAnalyzer.h"
#include "common/RtAllocGuard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
struct Analysis
{
    std::vector<lb::AnalysisFrame> frames;
    std::vector<double> onsetSeconds;
};

Analysis analyse(double rate, const std::vector<float>& signal, int blockSize = 128, std::int64_t startSample = 0)
{
    lb::VocalAnalyzer analyzer;
    analyzer.prepare(rate);
    analyzer.reset(startSample);

    Analysis result;
    for (std::size_t offset = 0; offset < signal.size(); offset += static_cast<std::size_t>(blockSize))
    {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize), signal.size() - offset));
        analyzer.process(signal.data() + offset, n, [&](const lb::AnalysisFrame& frame) {
            result.frames.push_back(frame);
            if (frame.hasOnset())
                result.onsetSeconds.push_back(static_cast<double>(frame.onsetSampleTime - startSample) / rate);
        });
    }
    return result;
}

double fractionVoicedBetween(const Analysis& a, double rate, double from, double to)
{
    int voiced = 0, total = 0;
    for (const auto& f : a.frames)
    {
        const double t = static_cast<double>(f.sampleTime) / rate;
        if (t < from || t > to)
            continue;
        ++total;
        voiced += f.isVoiced() ? 1 : 0;
    }
    return total > 0 ? static_cast<double>(voiced) / total : 0.0;
}
} // namespace

TEST_CASE("VocalAnalyzer runs at about 16 kHz for common device rates")
{
    lb::VocalAnalyzer analyzer;

    analyzer.prepare(48000.0);
    CHECK(analyzer.decimationFactor() == 3);
    CHECK(analyzer.analysisRate() == 16000.0);
    CHECK(analyzer.hopSize() == 128);

    analyzer.prepare(44100.0);
    CHECK(analyzer.decimationFactor() == 3);
    CHECK(analyzer.analysisRate() == 14700.0);

    analyzer.prepare(96000.0);
    CHECK(analyzer.decimationFactor() == 6);
}

TEST_CASE("VocalAnalyzer reports a steady sung note to within 10 cents")
{
    const double rate = GENERATE(48000.0, 44100.0);
    const double midi = GENERATE(45.0, 57.3, 64.0, 76.0);
    INFO("rate " << rate << ", MIDI " << midi);

    const auto analysis = analyse(rate, test::harmonicTone(rate, test::midiToHz(midi), 1.0, 0.3f));

    int checked = 0;
    for (const auto& f : analysis.frames)
    {
        const double t = static_cast<double>(f.sampleTime) / rate;
        if (t < 0.1 || t > 0.9)
            continue;
        REQUIRE(f.isVoiced());
        CHECK(f.midiPitch == Approx(midi).margin(0.1));
        ++checked;
    }
    CHECK(checked > 90);
}

TEST_CASE("VocalAnalyzer frames advance by one hop and carry their time")
{
    const double rate = 48000.0;
    const auto analysis = analyse(rate, std::vector<float>(48000, 0.0f), 100, 1000000);

    REQUIRE(analysis.frames.size() > 10);
    for (std::size_t i = 1; i < analysis.frames.size(); ++i)
        CHECK(analysis.frames[i].sampleTime - analysis.frames[i - 1].sampleTime == 384);

    lb::VocalAnalyzer analyzer;
    analyzer.prepare(rate);
    // The first frame completes after 384 input samples; its centre lags by the frame delay.
    CHECK(analysis.frames.front().sampleTime == 1000000 + 383 - analyzer.frameDelaySamples());
}

TEST_CASE("VocalAnalyzer stays unvoiced on silence, noise and very quiet tones")
{
    const double rate = 48000.0;

    const auto silence = analyse(rate, std::vector<float>(48000, 0.0f));
    for (const auto& f : silence.frames)
        CHECK_FALSE(f.isVoiced());
    CHECK(silence.onsetSeconds.empty());

    std::vector<float> noise(48000, 0.0f);
    test::addNoise(noise, 0.05f);
    CHECK(fractionVoicedBetween(analyse(rate, noise), rate, 0.1, 0.9) < 0.05);

    // About -66 dBFS RMS: below the -50 dB gate.
    const auto quiet = analyse(rate, test::harmonicTone(rate, 220.0, 1.0, 0.0005f));
    CHECK(fractionVoicedBetween(quiet, rate, 0.1, 0.9) == 0.0);
}

TEST_CASE("VocalAnalyzer finds separated notes on time")
{
    const double rate = GENERATE(48000.0, 44100.0);
    INFO("rate " << rate);

    std::vector<test::Note> notes;
    std::vector<double> starts;
    const double midis[] = { 57, 60, 62, 64, 62, 60, 57, 55, 57, 64 };
    double t = 0.3;
    for (std::size_t i = 0; i < std::size(midis); ++i)
    {
        const double duration = 0.25 + 0.05 * static_cast<double>(i % 3);
        notes.push_back({ t, duration, midis[i] });
        starts.push_back(t);
        t += duration + 0.12;
    }

    const auto analysis = analyse(rate, test::renderNotes(rate, notes, t + 0.3, 0.3f));
    const auto score = test::scoreOnsets(starts, analysis.onsetSeconds, 0.05);
    INFO("hits " << score.hits << ", misses " << score.misses << ", false alarms " << score.falseAlarms);
    CHECK(score.fMeasure() > 0.95);
    CHECK(score.meanAbsErrorSeconds < 0.02);
}

TEST_CASE("VocalAnalyzer finds legato note changes from the pitch")
{
    const double rate = 48000.0;
    // Notes that run straight into each other: no gap, no level change.
    const std::vector<test::Note> notes { { 0.3, 0.5, 57 }, { 0.8, 0.5, 60 }, { 1.3, 0.5, 64 }, { 1.8, 0.5, 62 } };
    const auto analysis = analyse(rate, test::renderNotes(rate, notes, 2.6, 0.3f));

    const auto score = test::scoreOnsets({ 0.3, 0.8, 1.3, 1.8 }, analysis.onsetSeconds, 0.05);
    INFO("hits " << score.hits << ", misses " << score.misses << ", false alarms " << score.falseAlarms);
    CHECK(score.hits == 4);
    CHECK(score.falseAlarms == 0);
}

TEST_CASE("VocalAnalyzer does not treat vibrato as new notes")
{
    const double rate = 48000.0;
    const std::vector<test::Note> notes { { 0.3, 2.0, 60 } };
    const auto analysis = analyse(rate, test::renderNotes(rate, notes, 2.6, 0.3f, 60.0)); // +-60 cents at 5.5 Hz

    const auto score = test::scoreOnsets({ 0.3 }, analysis.onsetSeconds, 0.05);
    INFO("onsets found: " << analysis.onsetSeconds.size());
    CHECK(score.hits == 1);
    CHECK(score.falseAlarms == 0);
    CHECK(fractionVoicedBetween(analysis, rate, 0.4, 2.2) > 0.98);
}

TEST_CASE("VocalAnalyzer::process does not allocate")
{
    lb::VocalAnalyzer analyzer;
    analyzer.prepare(48000.0);
    const auto signal = test::renderNotes(48000.0, { { 0.1, 0.4, 60 }, { 0.6, 0.4, 67 } }, 1.2, 0.3f);

    int frames = 0;
    const auto before = lb::rt::allocationCount();
    {
        lb::rt::ScopedRealtimeSection section;
        for (std::size_t offset = 0; offset + 128 <= signal.size(); offset += 128)
            analyzer.process(signal.data() + offset, 128, [&](const lb::AnalysisFrame&) { ++frames; });
    }
    CHECK(lb::rt::allocationCount() == before);
    CHECK(frames > 100);
}
