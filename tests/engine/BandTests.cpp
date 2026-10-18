#include "FakeDevice.h"
#include "common/RtAllocGuard.h"
#include "engine/AudioEngine.h"
#include "engine/AudioFileLoader.h"
#include "engine/BandController.h"
#include "engine/GrooveLoader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

#if defined(__linux__)
#include <time.h>
#endif

using Catch::Approx;
using lbtest::blockSize;
using lbtest::sampleRate;

namespace
{
const juce::File assets(LB_ASSETS_DIR);
const juce::File afrobeats = assets.getChildFile("grooves/afrobeats");
const juce::File worship = assets.getChildFile("grooves/worship-6-8");
const juce::File afroKit = assets.getChildFile("sfz/afro-kit/afro-kit.sfz");
const juce::File synthBass = assets.getChildFile("sfz/bass/synth-bass.sfz");
const juce::File warmPad = assets.getChildFile("sfz/pad/warm-pad.sfz");
const juce::File piano = assets.getChildFile("sfz/keys/piano.sfz");
constexpr int drumsLayer = 0, percLayer = 1, bassLayer = 2, keysLayer = 3, padLayer = 4;

// CPU time used by this thread, so that time the OS gave to other processes is not counted.
// Falls back to wall-clock time where it is not available.
double threadCpuSeconds()
{
#if defined(__linux__)
    timespec ts {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + 1e-9 * static_cast<double>(ts.tv_nsec);
#else
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

// The engine reads this clock; the harness moves it to each callback's start time.
double fakeNow = 0.0;
double fakeClock() { return fakeNow; }

// A duplex device with a steady sample clock whose callbacks start up to 1.5 ms late, as
// real ones do. The output of the callback that computes sample s is heard at
// (s + heardLatency) / sampleRate seconds: the driver's output latency plus one buffer.
struct BandHarness
{
    static constexpr int heardLatency = lbtest::fakeOutputLatency + blockSize;

    explicit BandHarness(bool loadKit = true, bool loadHarmony = false, const juce::File& styleFolder = afrobeats)
    {
        fakeNow = 0.0;
        engine.setClock(&fakeClock);
        if (loadKit)
            REQUIRE(engine.loadDrumKit(afroKit));
        if (loadHarmony)
        {
            REQUIRE(engine.loadBandInstrument(lb::AudioEngine::BandPart::bass, synthBass));
            REQUIRE(engine.loadBandInstrument(lb::AudioEngine::BandPart::pad, warmPad));
            engine.loadBandInstrument(lb::AudioEngine::BandPart::keys, piano); // its samples may not be downloaded
        }
        juce::String error;
        auto style = lb::loadStyle(styleFolder, error);
        REQUIRE(style != nullptr);
        engine.setStyle(std::move(style));
        engine.audioDeviceAboutToStart(&device);
        controller.styleChanged();
    }

    ~BandHarness() { engine.audioDeviceStopped(); }

    // Runs callbacks until the next one would start after `seconds` of sample time.
    void runUntil(double seconds)
    {
        const std::vector<float> mic(blockSize, 0.0f);
        std::vector<float> left(blockSize), right(blockSize);
        while (static_cast<double>(sample + blockSize) / sampleRate <= seconds)
        {
            fakeNow = static_cast<double>(sample) / sampleRate + lateness(random);
            const float* inputs[] = { mic.data() };
            float* outputs[] = { left.data(), right.data() };
            const auto before = std::chrono::steady_clock::now();
            const double cpuBefore = callbackSeconds != nullptr ? threadCpuSeconds() : 0.0;
            engine.audioDeviceIOCallbackWithContext(inputs, 1, outputs, 2, blockSize, {});
            if (callbackSeconds != nullptr)
            {
                callbackSeconds->push_back(static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count()));
                callbackCpuSeconds.push_back(static_cast<float>(threadCpuSeconds() - cpuBefore));
            }
            if (keepOutput)
                output.insert(output.end(), left.begin(), left.end());
            sample += blockSize;
            lb::BeatTick tick;
            while (engine.popBeatTick(tick))
                ticks.push_back(tick);
            // The message thread's work, as the app does it.
            lb::AnalysisFrame frame;
            while (engine.popAnalysisFrame(frame))
                controller.analysisFrame(frame);
            controller.update();
        }
        fakeNow = seconds; // the message thread acts at this time
    }

    // The output sample at which bar `bar` starts, from the beat ticks.
    std::int64_t barStart(int bar) const
    {
        for (const auto& tick : ticks)
            if (tick.bar == bar && tick.beat == 0)
                return tick.sampleTime;
        return -1;
    }

    // The first output sample at or after `from` louder than the threshold.
    std::int64_t firstSoundAfter(std::int64_t from, float threshold = 0.01f) const
    {
        for (auto i = static_cast<std::size_t>(std::max<std::int64_t>(from, 0)); i < output.size(); ++i)
            if (std::abs(output[i]) > threshold)
                return static_cast<std::int64_t>(i);
        return -1;
    }

    static double heardAt(std::int64_t outputSample) { return static_cast<double>(outputSample + heardLatency) / sampleRate; }
    double fakeNowAfterBlock() const { return static_cast<double>(sample + blockSize) / sampleRate; }

    lbtest::FakeDevice device;
    lb::AudioEngine engine;
    lb::BandController controller { engine };
    std::vector<float> output; // left channel
    bool keepOutput = true;
    std::vector<float>* callbackSeconds = nullptr; // the time each callback took, if set
    std::vector<float> callbackCpuSeconds;         // and the CPU time it used
    std::vector<lb::BeatTick> ticks;
    std::int64_t sample = 0;
    std::mt19937 random { 5 };
    std::uniform_real_distribution<double> lateness { 0.0, 0.0015 };
};

// Taps four beats at `bpm` starting at `first`, each moved by up to +-jitter seconds, and
// returns when the fifth beat should be heard.
double tapFour(BandHarness& h, double first, double bpm, double jitter, std::mt19937& random)
{
    std::uniform_real_distribution<double> offset(-jitter, jitter);
    const double period = 60.0 / bpm;
    lb::BandController::TapResult result = lb::BandController::TapResult::ignored;
    for (int i = 0; i < 4; ++i)
    {
        const double at = first + i * period + (jitter > 0.0 ? offset(random) : 0.0);
        h.runUntil(at);
        result = h.controller.tap(at);
        CHECK(result == (i < 3 ? lb::BandController::TapResult::counting : lb::BandController::TapResult::started));
    }
    return first + 4 * period;
}
} // namespace

namespace
{
// The strongest frequency between 30 and 250 Hz in the output from `from`, `seconds` long.
double lowPeakHz(const std::vector<float>& output, std::int64_t from, double seconds)
{
    const auto n = static_cast<std::size_t>(seconds * sampleRate);
    double bestHz = 0.0, bestPower = 0.0;
    for (double hz = 30.0; hz <= 250.0; hz *= std::pow(2.0, 1.0 / 240.0)) // 5-cent steps
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < n && static_cast<std::size_t>(from) + i < output.size(); ++i)
        {
            const double phase = 2.0 * 3.141592653589793 * hz * static_cast<double>(i) / sampleRate;
            re += output[static_cast<std::size_t>(from) + i] * std::cos(phase);
            im += output[static_cast<std::size_t>(from) + i] * std::sin(phase);
        }
        if (re * re + im * im > bestPower)
        {
            bestPower = re * re + im * im;
            bestHz = hz;
        }
    }
    return bestHz;
}

int pitchClassOf(double hz)
{
    return ((static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hz / 440.0))) % 12) + 12) % 12;
}

// A plain sung melody in D major, as a voice file: "Twinkle, twinkle" in two phrases, with a
// gap between the notes and a breath after each phrase. It loops.
std::vector<float> twinkleInD()
{
    const int melody[] = { 62, 62, 69, 69, 71, 71, 69, -1, 67, 67, 66, 66, 64, 64, 62, -1 };
    std::vector<float> voice;
    double phase = 0.0;
    for (const int note : melody)
    {
        const int length = static_cast<int>((note < 0 ? 0.6 : 0.42) * sampleRate);
        const double hz = note < 0 ? 0.0 : 440.0 * std::pow(2.0, (note - 69) / 12.0);
        for (int i = 0; i < length; ++i)
        {
            const bool sounding = note >= 0 && i < length - static_cast<int>(0.06 * sampleRate);
            phase += 2.0 * 3.141592653589793 * hz / sampleRate;
            voice.push_back(sounding ? static_cast<float>(0.3 * std::sin(phase) + 0.1 * std::sin(2.0 * phase) + 0.05 * std::sin(3.0 * phase)) : 0.0f);
        }
    }
    return voice;
}
} // namespace

TEST_CASE("GrooveLoader reads the Afrobeats style")
{
    juce::String error;
    const auto style = lb::loadStyle(afrobeats, error);
    REQUIRE(style != nullptr);
    CHECK(error.isEmpty());
    CHECK(style->name == "Afrobeats");
    CHECK(style->beatsPerBar == 4);
    CHECK(style->defaultBpm == 104.0);
    CHECK(style->swing == Approx(0.54));
    CHECK(style->layers == std::vector<std::string> { "drums", "perc", "bass", "keys", "pad" });
    CHECK(style->pitchedLayers == 0b11100u);
    CHECK(style->fillLayers == 0b00011u);
    REQUIRE(style->sections.size() == 2);
    CHECK(style->sections[0].name == "Verse");
    CHECK(style->sections[0].main.lengthBeats == 16.0);
    CHECK(style->sections[0].mainMinor.lengthBeats == 16.0);
    CHECK(style->sections[0].fill.lengthBeats == 4.0);
    CHECK(style->countIn.size() == 4);
    CHECK(style->entry.size() == 1);
    CHECK(style->ending.size() == 9);

    // Staged entry: pad and piano, then the drums, then the bass; without a key the drums
    // alone, and the harmony on a four-bar boundary once the key is known.
    REQUIRE(style->entryKeyKnown.size() == 3);
    CHECK(style->entryKeyKnown[0].bar == 0);
    CHECK(style->entryKeyKnown[0].layers == 0b11000u);
    CHECK(style->entryKeyKnown[1].bar == 4);
    CHECK(style->entryKeyKnown[1].layers == 0b00011u);
    CHECK(style->entryKeyKnown[2].bar == 8);
    CHECK(style->entryKeyKnown[2].layers == 0b00100u);
    REQUIRE(style->entryKeyUnknown.size() == 1);
    CHECK(style->entryKeyUnknown[0].layers == 0b00011u);
    CHECK(style->afterKeyLockLayers == 0b11100u);
    CHECK(style->afterKeyLockEveryBars == 4);

    // The harmony, in C: the major verse walks C G Am F, the minor one Cm Ab Eb Bb.
    const auto bassAt = [](const lb::Pattern& p, double beat) {
        const auto it = std::find_if(p.notes.begin(), p.notes.end(), [&](const lb::GrooveNote& n) { return n.beat == beat && n.layer == 2; });
        return it == p.notes.end() ? -1 : int { it->note };
    };
    CHECK(bassAt(style->sections[0].main, 0.0) == 36);
    CHECK(bassAt(style->sections[0].main, 4.0) == 43);
    CHECK(bassAt(style->sections[0].main, 8.0) == 33);
    CHECK(bassAt(style->sections[0].mainMinor, 0.0) == 36);
    CHECK(bassAt(style->sections[0].mainMinor, 4.0) == 32);

    // The verse opens with an accented kick and a soft hat on beat 1, and the shaker on the perc layer.
    const auto& notes = style->sections[0].main.notes;
    const auto at = [&](double beat, int note) {
        return std::find_if(notes.begin(), notes.end(), [&](const lb::GrooveNote& n) { return n.beat == beat && n.note == note; });
    };
    REQUIRE(at(0.0, 36) != notes.end());
    CHECK(at(0.0, 36)->velocity == 118);
    CHECK(at(0.0, 36)->layer == 0);
    REQUIRE(at(0.0, 70) != notes.end());
    CHECK(at(0.0, 70)->layer == 1);
    CHECK(at(1.5, 36) != notes.end()); // the "and" of 2
}

TEST_CASE("Each style's harmony stays in its registers in every key")
{
    for (const auto& folder : { afrobeats, worship })
    {
    INFO(folder.getFileName());
    juce::String error;
    const auto style = lb::loadStyle(folder, error);
    REQUIRE(style != nullptr);
    // Bass from D1 to C4, piano from E3 to Eb5 (inside its samples, A2 to E6), pad from Ab2 to Bb4.
    const std::pair<int, std::pair<int, int>> registers[] = { { bassLayer, { 26, 60 } }, { keysLayer, { 52, 75 } }, { padLayer, { 44, 70 } } };
    for (int index = 0; index < 24; ++index)
    {
        const auto key = lb::Key::fromIndex(index);
        const int shift = lb::PatternPlayer::transposition(key);
        for (const auto& section : style->sections)
            for (const bool intense : { false, true })
                for (const auto& note : section.patternFor(key.minor, intense).notes)
                    for (const auto& [layer, range] : registers)
                        if (note.layer == layer)
                        {
                            INFO(key.name() << ", " << section.name << (intense ? " intense" : "") << ", "
                                            << style->layers[static_cast<std::size_t>(layer)] << " note " << int { note.note });
                            CHECK(note.note + shift >= range.first);
                            CHECK(note.note + shift <= range.second);
                        }
    }
    }
}

TEST_CASE("GrooveLoader reads the worship style in 6/8, counted in dotted quarters")
{
    juce::String error;
    const auto style = lb::loadStyle(worship, error);
    REQUIRE(style != nullptr);
    CHECK(error.isEmpty());
    CHECK(style->name == "Worship 6/8");
    CHECK(style->meter == "6/8");
    CHECK(style->beatsPerBar == 2);
    CHECK(style->defaultBpm == 66.0);
    CHECK(style->swing == 0.5);
    REQUIRE(style->sections.size() == 2);
    for (const auto& section : style->sections)
    {
        CHECK(section.main.lengthBeats == 8.0); // four bars of two beats
        CHECK(section.mainMinor.lengthBeats == 8.0);
        CHECK(section.mainIntense.lengthBeats == 8.0);
        CHECK(section.mainIntenseMinor.lengthBeats == 8.0);
        CHECK(section.fill.lengthBeats == 2.0);
    }

    // Six sticks on the eighths: thirds of a beat.
    REQUIRE(style->countIn.size() == 6);
    for (std::size_t i = 0; i < 6; ++i)
        CHECK(style->countIn[i].beat == Approx(static_cast<double>(i) / 3.0).margin(1e-5));

    // The verse: kick on 1, rim on 4 (the second beat), the shaker's eighths; the piano's
    // arpeggio on the eighths.
    const auto& notes = style->sections[0].main.notes;
    const auto has = [&](double beat, int note, int layer) {
        return std::any_of(notes.begin(), notes.end(), [&](const lb::GrooveNote& n) {
            return std::abs(n.beat - beat) < 1e-6 && n.note == note && n.layer == layer;
        });
    };
    CHECK(has(0.0, 36, drumsLayer));
    CHECK(has(1.0, 37, drumsLayer));
    for (int eighth = 0; eighth < 6; ++eighth)
        CHECK(has(eighth / 3.0, 70, percLayer));
    CHECK(std::count_if(notes.begin(), notes.end(), [](const lb::GrooveNote& n) { return n.layer == keysLayer && n.beat < 2.0; }) == 6);

    REQUIRE(style->entryKeyKnown.size() == 3);
    CHECK(style->entryKeyKnown[0].layers == (1u << padLayer | 1u << keysLayer));
    CHECK(style->entryKeyKnown[1].bar == 4);
    CHECK(style->entryKeyKnown[2].layers == 1u << drumsLayer);
}

TEST_CASE("GrooveLoader explains what is wrong with a broken style")
{
    juce::String error;
    CHECK(lb::loadStyle(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("no-such-style"), error) == nullptr);
    CHECK(error.contains("style.json"));

    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lb-style-" + juce::Uuid().toString());
    REQUIRE(folder.createDirectory());
    for (const auto& midi : afrobeats.findChildFiles(juce::File::findFiles, false, "*.mid"))
        REQUIRE(midi.copyFileTo(folder.getChildFile(midi.getFileName())));
    auto json = afrobeats.getChildFile("style.json").loadFileAsString();

    folder.getChildFile("style.json").replaceWithText(json.replace("\"perc\"", "\"percussion\""));
    error.clear();
    CHECK(lb::loadStyle(folder, error) == nullptr);
    CHECK(error.contains("\"perc\" is not one of the style's layers"));

    folder.getChildFile("style.json").replaceWithText(json.replace("fill-chorus.mid", "chorus.mid"));
    error.clear();
    CHECK(lb::loadStyle(folder, error) == nullptr);
    CHECK(error.contains("fill must be one bar"));
    folder.deleteRecursively();
}

TEST_CASE("Band: four taps bring the drums in on the fifth beat (Phase 2 exit criterion)")
{
    BandHarness h;
    h.runUntil(1.0); // the time map settles on the punctual callbacks

    std::mt19937 random(1);
    const double downbeat = tapFour(h, 1.0, 104.0, 0.0, random);
    h.runUntil(downbeat + 2.0);

    const auto status = h.engine.bandStatus();
    CHECK(status.state == lb::BandState::playing);
    const double tempoError = status.bpm / 104.0 - 1.0;
    CHECK(std::abs(tempoError) < 0.005);

    const auto onset = h.firstSoundAfter(0);
    REQUIRE(onset >= 0);
    const double entryError = BandHarness::heardAt(onset) - downbeat;
    CHECK(std::abs(entryError) < 0.010);
    WARN("entry error " << entryError * 1000.0 << " ms, tempo error " << tempoError * 100.0 << " %");

    // The first beat tick is the downbeat of bar 0, in the verse, starting the section.
    REQUIRE(!h.ticks.empty());
    CHECK(h.ticks.front().bar == 0);
    CHECK(h.ticks.front().beat == 0);
    CHECK((h.ticks.front().flags & lb::BeatTick::sectionStart) != 0);
    CHECK(std::abs(BandHarness::heardAt(h.ticks.front().sampleTime) - downbeat) < 0.010);
}

TEST_CASE("Band: with human timing in the taps, the entry stays close to the beat")
{
    std::mt19937 random(9);
    double worstEntry = 0.0, worstTempo = 0.0, sumEntry = 0.0;
    constexpr int trials = 20;
    for (int trial = 0; trial < trials; ++trial)
    {
        BandHarness h(false);
        h.runUntil(0.5);
        const double bpm = 92.0 + trial;
        const double downbeat = tapFour(h, 0.5, bpm, 0.005, random); // taps within 5 ms
        h.runUntil(downbeat + 0.5);
        REQUIRE(!h.ticks.empty());
        const double entry = std::abs(BandHarness::heardAt(h.ticks.front().sampleTime) - downbeat);
        worstEntry = std::max(worstEntry, entry);
        sumEntry += entry;
        worstTempo = std::max(worstTempo, std::abs(h.engine.bandStatus().bpm / bpm - 1.0));
    }
    WARN("entry error: mean " << sumEntry / trials * 1000.0 << " ms, worst " << worstEntry * 1000.0
                                       << " ms; worst tempo error " << worstTempo * 100.0 << " %");
    CHECK(sumEntry / trials < 0.005);
    CHECK(worstEntry < 0.012);
    CHECK(worstTempo < 0.0075);
}

TEST_CASE("Band: tapping while it plays pulls it onto the new tempo and beat")
{
    BandHarness h(false);
    h.runUntil(0.5);
    std::mt19937 random(2);
    tapFour(h, 0.5, 104.0, 0.0, random);
    h.runUntil(6.0);

    // The singer settles at 98 BPM, on a beat 120 ms away from the band's.
    const double period = 60.0 / 98.0;
    const double first = 6.12;
    double lastTap = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        lastTap = first + i * period;
        h.runUntil(lastTap);
        const auto result = h.controller.tap(lastTap);
        CHECK(result == (i < 3 ? lb::BandController::TapResult::counting : lb::BandController::TapResult::steered));
    }
    h.ticks.clear();
    h.runUntil(lastTap + 20 * period);

    CHECK(h.engine.bandStatus().bpm == Approx(98.0).epsilon(0.002));
    // The beats are now heard on the tapped beat grid.
    REQUIRE(h.ticks.size() > 4);
    const double heard = BandHarness::heardAt(h.ticks.back().sampleTime);
    const double offGrid = std::remainder(heard - first, period);
    WARN("20 beats after the taps, the band's beats are " << offGrid * 1000.0 << " ms from the tapped beat grid");
    CHECK(std::abs(offGrid) < 0.003);
}

TEST_CASE("Band: four taps while it plays re-lock it within one bar, without a jump (Phase 4 exit criterion)")
{
    // The singer taps four beats at `bpm`, the first at `first`; the band plays at 104 BPM.
    // Returns the worst distance from the tapped grid of the beats heard from one bar after
    // the fourth tap, and checks that no beat comes more than 15% early or late against the
    // tempo it is heading for (a steady catch-up, not a jump).
    const auto relock = [](double bpm, double first) {
        BandHarness h(false);
        h.runUntil(0.5);
        std::mt19937 random(3);
        tapFour(h, 0.5, 104.0, 0.0, random);
        h.runUntil(first - 0.5);
        const double period = 60.0 / bpm;
        double lastTap = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            lastTap = first + i * period;
            h.runUntil(lastTap);
            CHECK(h.controller.tap(lastTap) == (i < 3 ? lb::BandController::TapResult::counting : lb::BandController::TapResult::steered));
        }
        h.ticks.clear();
        h.runUntil(lastTap + 12 * period);
        CHECK(h.engine.bandStatus().bpm == Approx(bpm).epsilon(0.001));

        double worst = 0.0;
        double previous = -1.0;
        for (const auto& tick : h.ticks)
        {
            const double heard = BandHarness::heardAt(tick.sampleTime);
            if (previous > 0.0)
            {
                const double interval = heard - previous;
                CHECK(interval > period / 1.15 - 0.001);
                CHECK(interval < period / 0.85 + 0.001);
            }
            previous = heard;
            if (heard >= lastTap + 4 * period)
                worst = std::max(worst, std::abs(std::remainder(heard - first, period)));
        }
        return worst;
    };

    SECTION("the singer slows to 98 BPM, 120 ms off the band's beat")
    {
        const double worst = relock(98.0, 6.12);
        WARN("from one bar after the fourth tap, the beats are at most " << worst * 1000.0 << " ms from the tapped grid");
        CHECK(worst < 0.005);
    }
    SECTION("the singer is almost half a beat off the band, at the same tempo")
    {
        const double worst = relock(104.0, 0.5 + 9 * 60.0 / 104.0 + 0.45 * 60.0 / 104.0);
        WARN("from one bar after the fourth tap, the beats are at most " << worst * 1000.0 << " ms from the tapped grid");
        CHECK(worst < 0.005);
    }
}

TEST_CASE("Band: the worship style plays in 6/8: taps on the dotted quarters, the staged entry, the bass in the key")
{
    BandHarness h(true, true, worship);
    h.runUntil(0.5);
    h.controller.setKey(lb::Key { 7, false }); // G major, known before the band starts
    std::mt19937 random(4);
    const double period = 60.0 / 66.0;
    const double fifth = tapFour(h, 0.5, 66.0, 0.0, random);
    h.runUntil(fifth + 9 * 2 * period + 0.1); // bars 0 to 8, two beats each

    // Beats every dotted quarter, two to a bar, the first one on the fifth tap.
    REQUIRE(h.ticks.size() >= 18);
    CHECK(BandHarness::heardAt(h.ticks.front().sampleTime) == Approx(fifth).margin(0.002));
    for (std::size_t i = 0; i < h.ticks.size(); ++i)
    {
        CHECK(h.ticks[i].bar == static_cast<int>(i / 2));
        CHECK(h.ticks[i].beat == static_cast<int>(i % 2));
        if (i > 0)
            CHECK(static_cast<double>(h.ticks[i].sampleTime - h.ticks[i - 1].sampleTime) / sampleRate == Approx(period).margin(0.0002));
    }

    // Pad and piano from bar 0, the bass and shaker from bar 4, the drums from bar 8.
    const auto layersOf = [&](int bar) {
        for (const auto& tick : h.ticks)
            if (tick.bar == bar)
                return tick.layers;
        return 0u;
    };
    CHECK(layersOf(0) == (1u << keysLayer | 1u << padLayer));
    CHECK(layersOf(4) == (1u << keysLayer | 1u << padLayer | 1u << bassLayer | 1u << percLayer));
    CHECK(layersOf(8) == 0b11111u);

    // Bar 4 starts the verse progression again: the bass holds G for the first beat.
    const auto bar4 = h.barStart(4);
    REQUIRE(bar4 > 0);
    CHECK(pitchClassOf(lowPeakHz(h.output, bar4 + 400, 0.6)) == 7);
}

TEST_CASE("Band: louder singing makes the band play harder and turn intense; off, it plays as written")
{
    // A voice that sings a scale at its usual level for 24 s, then 12 dB louder.
    std::vector<float> voice;
    double phase = 0.0;
    for (int i = 0; i < static_cast<int>(40.0 * sampleRate); ++i)
    {
        const double t = i / sampleRate;
        const int degree = static_cast<int>(t / 0.5) % 8;
        const int scale[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        phase += 2.0 * 3.141592653589793 * 220.0 * std::pow(2.0, scale[degree] / 12.0) / sampleRate;
        const double amplitude = t < 24.0 ? 0.05 : 0.2;
        voice.push_back(static_cast<float>(amplitude * (std::sin(phase) + 0.3 * std::sin(2.0 * phase))));
    }
    BandHarness h(true);
    h.keepOutput = false;
    h.engine.setFileInput(std::move(voice), "louder voice");
    h.engine.setInputSource(lb::AudioEngine::InputSource::file);
    h.runUntil(0.5);
    h.controller.setTempoBpm(104.0);
    h.controller.startStop();

    h.runUntil(23.5);
    auto status = h.engine.bandStatus();
    CHECK(status.velocityScale == Approx(1.0f).margin(0.07f));
    CHECK_FALSE(status.intense);

    h.ticks.clear();
    h.runUntil(36.0); // 12 s of loud singing: more than two bars
    status = h.engine.bandStatus();
    CHECK(status.velocityScale > 1.1f);
    CHECK(status.intense);
    const auto intenseTick = std::find_if(h.ticks.begin(), h.ticks.end(), [](const lb::BeatTick& tick) { return (tick.flags & lb::BeatTick::intense) != 0; });
    REQUIRE(intenseTick != h.ticks.end());
    CHECK(intenseTick->beat == 0); // the intense variation starts on a downbeat
    const double startedAfter = static_cast<double>(intenseTick->sampleTime) / sampleRate - 24.0;
    WARN("the band turned intense " << startedAfter << " s after the singer got louder");
    CHECK(startedAfter > 4.6);  // not before two bars of loud singing
    CHECK(startedAfter < 10.0);

    h.controller.setDynamicsEnabled(false);
    h.runUntil(39.0);
    status = h.engine.bandStatus();
    CHECK(status.velocityScale == 1.0f);
    CHECK_FALSE(status.intense);

    // Another voice is judged against its own range, from scratch: as written until the
    // follower has heard enough of it.
    h.controller.setDynamicsEnabled(true);
    h.controller.voiceChanged();
    h.runUntil(42.0);
    status = h.engine.bandStatus();
    CHECK(status.velocityScale == 1.0f);
    CHECK_FALSE(status.intense);
    CHECK(h.controller.dynamics().voicedSeconds() < 4.0);
}

TEST_CASE("Band: the nudge keys change the tempo by 1% without moving the beat")
{
    BandHarness h(false);
    h.runUntil(0.5);
    h.controller.setTempoBpm(100.0);
    h.controller.nudgeTempo(-1); // while stopped: the tempo it will start at
    CHECK(h.controller.tempoBpm() == Approx(100.0 / 1.01));
    h.controller.nudgeTempo(1);
    CHECK(h.controller.tempoBpm() == Approx(100.0)); // a step down and a step up cancel out
    h.controller.setTempoBpm(100.0);
    h.controller.startStop();
    h.runUntil(5.0);

    h.ticks.clear();
    h.controller.nudgeTempo(1);
    CHECK(h.controller.handleMidi(juce::MidiMessage::controllerEvent(1, 85, 127))); // MIDI: once more
    h.runUntil(9.0);
    CHECK(h.engine.bandStatus().bpm == Approx(102.01).epsilon(0.0005));

    // The beats move on at the new tempo straight away, without a jump.
    REQUIRE(h.ticks.size() > 4);
    const double period = 60.0 / 102.01;
    for (std::size_t i = 2; i < h.ticks.size(); ++i)
        CHECK(static_cast<double>(h.ticks[i].sampleTime - h.ticks[i - 1].sampleTime) / sampleRate == Approx(period).margin(0.0005));
}

TEST_CASE("Band: Enter starts with a count-in and stops with a fill and the final hits")
{
    BandHarness h(false);
    h.runUntil(0.5);
    h.controller.setTempoBpm(120.0);
    h.controller.startStop();
    h.runUntil(1.0);
    CHECK(h.engine.bandStatus().state == lb::BandState::playing);
    h.runUntil(6.0);

    // A bar of count-in, then the band.
    REQUIRE(h.ticks.size() >= 8);
    for (int i = 0; i < 4; ++i)
    {
        CHECK(h.ticks[static_cast<std::size_t>(i)].bar == -1);
        CHECK((h.ticks[static_cast<std::size_t>(i)].flags & lb::BeatTick::countIn) != 0);
    }
    CHECK(h.ticks[4].bar == 0);
    CHECK(h.ticks[5].sampleTime - h.ticks[4].sampleTime == 24000); // 120 BPM

    h.controller.next();
    h.runUntil(10.0);
    CHECK(h.engine.bandStatus().section == 1); // the chorus

    h.controller.startStop();
    h.runUntil(16.0);
    CHECK(h.engine.bandStatus().state == lb::BandState::stopped);
    CHECK((h.ticks.back().flags & lb::BeatTick::ending) != 0);
}

TEST_CASE("Band: MIDI taps and controls work like the keys")
{
    BandHarness h(false);
    h.runUntil(0.5);
    const double period = 60.0 / 100.0;
    for (int i = 0; i < 4; ++i)
    {
        const double at = 0.5 + i * period;
        h.runUntil(at);
        auto pedal = juce::MidiMessage::controllerEvent(1, 64, 127);
        pedal.setTimeStamp(at);
        CHECK(h.controller.handleMidi(pedal));
        auto release = juce::MidiMessage::controllerEvent(1, 64, 0);
        release.setTimeStamp(at + 0.1);
        CHECK(h.controller.handleMidi(release)); // a release is not a tap
    }
    h.runUntil(0.5 + 5 * period);
    CHECK(h.engine.bandStatus().state == lb::BandState::playing);
    CHECK(h.engine.bandStatus().bpm == Approx(100.0).epsilon(0.001));

    CHECK(h.controller.handleMidi(juce::MidiMessage::controllerEvent(1, 82, 127))); // next section
    h.runUntil(0.5 + 12 * period);
    CHECK(h.engine.bandStatus().section == 1);
    CHECK(!h.controller.handleMidi(juce::MidiMessage::controllerEvent(1, 7, 100))); // volume: not ours
}


TEST_CASE("Band: a sung melody locks the key, and the band comes in on it with the staged entry")
{
    BandHarness h(true, true);
    h.engine.setFileInput(twinkleInD(), "twinkle in D");
    h.engine.setInputSource(lb::AudioEngine::InputSource::file);
    h.keepOutput = false;

    double lockedAt = -1.0;
    for (double t = 0.5; t <= 20.0 && lockedAt < 0.0; t += 0.1)
    {
        h.runUntil(t);
        if (h.controller.keyEstimate().state == lb::KeyEstimator::State::locked)
            lockedAt = t;
    }
    REQUIRE(lockedAt > 0.0);
    WARN("the key locked after " << lockedAt << " s of the melody");
    CHECK(h.controller.keyEstimate().key == lb::Key { 2, false });
    CHECK(h.controller.bandKey() == lb::Key { 2, false });

    // Started now, the band knows the key: pad and piano first, the drums from bar 4, the bass from bar 8.
    h.controller.startStop();
    const auto layersAt = [&](int bar) {
        while (h.barStart(bar) < 0)
            h.runUntil(h.fakeNowAfterBlock());
        h.runUntil(h.fakeNowAfterBlock()); // the status after that downbeat
        return h.engine.bandStatus().layers;
    };
    CHECK(layersAt(0) == (1u << keysLayer | 1u << padLayer));
    CHECK(h.engine.bandStatus().key == lb::Key { 2, false });
    CHECK(layersAt(4) == (1u << keysLayer | 1u << padLayer | 1u << drumsLayer | 1u << percLayer));
    CHECK(layersAt(8) == 0b11111u);

    // Once the band has stopped, the next song starts by listening again.
    h.controller.stopNow();
    h.runUntil(h.fakeNowAfterBlock() + 0.05);
    CHECK(h.engine.bandStatus().state == lb::BandState::stopped);
    CHECK_FALSE(h.controller.bandKey().isValid());
    CHECK(h.controller.keyEstimate().state != lb::KeyEstimator::State::locked);
}

TEST_CASE("Band: without a key the drums play alone; the bass comes in on the key, a semitone up by hand")
{
    BandHarness h(true, true);
    for (const int layer : { drumsLayer, percLayer, keysLayer, padLayer })
        h.engine.setLayerEnabled(layer, false); // listen to the bass alone
    h.runUntil(0.5);
    h.controller.setTempoBpm(104.0);
    h.controller.startStop();
    while (h.barStart(1) < 0)
        h.runUntil(h.fakeNowAfterBlock());
    CHECK(h.engine.bandStatus().layers == 0u); // the drums alone would play, and they are muted here

    // G major, given by hand in bar 1: the bass comes in on the next four-bar boundary.
    h.controller.setKey(lb::Key { 7, false });
    while (h.barStart(6) < 0)
        h.runUntil(h.fakeNowAfterBlock());
    const auto bar4 = h.barStart(4);
    const auto bassIn = h.firstSoundAfter(h.barStart(1), 0.001f); // after the count-in's sticks
    CHECK(bassIn >= bar4);
    CHECK(bassIn < bar4 + 64);
    // Bar 4 is the progression's first bar: the bass holds the tonic for its first beat.
    CHECK(pitchClassOf(lowPeakHz(h.output, bar4 + 400, 0.45)) == 7);
    CHECK(h.engine.bandStatus().key == lb::Key { 7, false });

    // A semitone up, to Ab: from the next downbeat; bar 8 starts the progression again.
    h.controller.shiftKey(1);
    CHECK(h.controller.bandKey() == lb::Key { 8, false });
    while (h.barStart(9) < 0)
        h.runUntil(h.fakeNowAfterBlock());
    CHECK(pitchClassOf(lowPeakHz(h.output, h.barStart(8) + 400, 0.45)) == 8);

    // Relative minor of Ab major: F minor.
    h.controller.toggleRelativeKey();
    CHECK(h.controller.bandKey() == lb::Key { 5, true });
    h.controller.stopNow();
    h.runUntil(h.fakeNowAfterBlock() + 0.5);
    CHECK(h.engine.activeVoices() == 0);
}

TEST_CASE("Band: the callback does not allocate while the band plays, and the limiter holds the peaks")
{
    REQUIRE(lb::rt::isGuardActive());
    BandHarness h(true, true);
    h.engine.setDrumsGain(4.0f); // +12 dB, enough to hit the limiter
    h.runUntil(0.5);
    h.controller.setTempoBpm(110.0);
    h.controller.startStop();
    h.runUntil(3.0); // sfizz has started its voices

    // The key arrives in bar 0, so the harmony comes in at bar 4 (about 11.5 s), changes key,
    // carries on through a fill and ends with the band.
    const auto before = lb::rt::allocationCount();
    h.controller.setKey(lb::Key { 9, true });
    h.controller.next();
    h.runUntil(13.0);
    CHECK(h.engine.bandStatus().layers == 0b11111u);
    CHECK(h.engine.bandStatus().key == lb::Key { 9, true });
    h.controller.shiftKey(-2);
    h.runUntil(16.0);
    h.controller.fill();
    h.runUntil(19.0);
    h.controller.startStop();
    h.runUntil(24.0);
    CHECK(lb::rt::allocationCount() == before);
    CHECK(h.engine.bandStatus().state == lb::BandState::stopped);

    const float peak = *std::max_element(h.output.begin(), h.output.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    CHECK(std::abs(peak) <= juce::Decibels::decibelsToGain(-1.0f) + 1e-6f);
    CHECK(std::abs(peak) > 0.5f);
    CHECK(h.engine.droppedCommands() == 0);
    CHECK(h.engine.droppedBeatTicks() == 0);
}

// Not run by default (about 30 minutes of audio; under a minute in Release):
//   liveband_engine_tests "[soak]"
// Measures how long each callback takes with the full band (drums, percussion, bass, piano
// and pad), the band changing section and filling every 30 seconds and key every 5 minutes,
// and the voice analysis running on a voice file, as fast as the machine allows. A callback
// slower than its 2.7 ms of audio would be an overrun.
//
// It checks the engine's sustained cost and reports, without failing on them, the rare
// callbacks over budget: on a shared or virtual machine even a loop doing identical work
// each time has a few of those, in CPU time as well as wall-clock time. The overrun count
// that matters is the app's own, on the laptop.
TEST_CASE("Band soak: 30 minutes of band, voice file and analysis", "[.soak]")
{
    BandHarness h(true, true);
    h.keepOutput = false;
    std::vector<float> seconds;
    seconds.reserve(700000);

    const auto clip = assets.getParentDirectory().getChildFile("testdata/external/vocadito/Audio/vocadito_17.wav");
    juce::String error;
    std::vector<float> voice;
    if (auto loaded = lb::loadMonoAudio(clip, sampleRate, error))
        voice = std::move(*loaded);
    else
        for (int i = 0; i < static_cast<int>(sampleRate) * 10; ++i) // a stand-in voice: a 220 Hz tone
            voice.push_back(0.3f * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 220.0 * i / sampleRate)));
    h.engine.setFileInput(std::move(voice), "soak voice");
    h.engine.setInputSource(lb::AudioEngine::InputSource::file);

    h.runUntil(0.5);
    h.controller.setTempoBpm(104.0);
    h.controller.setKey(lb::Key { 7, false });
    h.controller.startStop();
    h.callbackSeconds = &seconds;
    constexpr double minutes = 30.0;
    for (double t = 30.0; t <= minutes * 60.0; t += 30.0)
    {
        h.runUntil(t);
        if (static_cast<int>(t / 30.0) % 2 == 1)
            h.controller.next();
        else
            h.controller.fill();
        if (static_cast<int>(t) % 300 == 0)
            h.controller.shiftKey(5);
    }
    REQUIRE(h.engine.bandStatus().state == lb::BandState::playing);
    CHECK(h.engine.bandStatus().layers == 0b11111u);

    // Where in the music the slowest callbacks fell, for diagnosis.
    const double budget = blockSize / sampleRate;
    const auto firstTimed = static_cast<double>(h.sample) / blockSize - static_cast<double>(seconds.size());
    juce::String slowest;
    for (std::size_t i = 0; i < seconds.size(); ++i)
        if (seconds[i] > budget)
            slowest << juce::String((firstTimed + static_cast<double>(i)) * blockSize / sampleRate, 2) << " s (wall " << juce::String(seconds[i] * 1000.0f, 1)
                    << " ms, CPU " << juce::String(h.callbackCpuSeconds[i] * 1000.0f, 2) << " ms); ";

    // Percent of the time budget at quantile q.
    const auto summary = [&](std::vector<float> times, const char* what) {
        std::sort(times.begin(), times.end());
        const auto at = [&](double q) { return times[static_cast<std::size_t>(q * static_cast<double>(times.size() - 1))] / budget * 100.0; };
        const auto over = times.end() - std::upper_bound(times.begin(), times.end(), static_cast<float>(budget));
        WARN(what << ", " << times.size() << " callbacks: median " << at(0.5) << " %, 99.9th percentile " << at(0.999) << " %, worst "
                  << at(1.0) << " % of the time budget; " << over << " over budget");
        return at(0.999);
    };
    summary(seconds, "Wall-clock time");
    const double cpu999 = summary(h.callbackCpuSeconds, "CPU time");
    if (slowest.isNotEmpty())
        WARN("Callbacks slower than real time (wall clock): " << slowest);
    CHECK(cpu999 < 25.0); // 999 callbacks in 1000 need under a quarter of their time
    CHECK(h.engine.droppedCommands() == 0);
    CHECK(h.engine.droppedBeatTicks() == 0);
}

// The control for the soak: identical work in every "callback", about as much as a busy
// band callback, timed the same way. Whatever spikes it shows come from the machine, not
// from LiveBand. Run it with the soak:  liveband_engine_tests "[soak]"
TEST_CASE("Band soak control: identical work every callback", "[.soak]")
{
    constexpr std::size_t callbacks = 674813; // as many as 30 minutes of 128-sample callbacks
    const double budget = blockSize / sampleRate;
    std::vector<float> data(4096, 0.5f), wall(callbacks), cpu(callbacks);
    volatile float sink = 0.0f;
    for (std::size_t i = 0; i < callbacks; ++i)
    {
        const auto before = std::chrono::steady_clock::now();
        const double cpuBefore = threadCpuSeconds();
        float sum = 0.0f;
        for (int pass = 0; pass < 12; ++pass)
            for (float x : data)
                sum += std::sin(x * static_cast<float>(pass + 1)) * 0.001f;
        sink = sink + sum;
        cpu[i] = static_cast<float>(threadCpuSeconds() - cpuBefore);
        wall[i] = static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count());
    }
    for (auto* times : { &wall, &cpu })
    {
        std::sort(times->begin(), times->end());
        const auto at = [&](double q) { return (*times)[static_cast<std::size_t>(q * static_cast<double>(callbacks - 1))] / budget * 100.0; };
        const auto over = times->end() - std::upper_bound(times->begin(), times->end(), static_cast<float>(budget));
        WARN((times == &wall ? "Control, wall-clock time" : "Control, CPU time") << ": median " << at(0.5) << " %, 99.9th percentile "
             << at(0.999) << " %, worst " << at(1.0) << " % of the time budget; " << over << " over budget");
    }
    SUCCEED();
}
