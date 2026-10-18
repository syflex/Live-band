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
const juce::File afroKit = assets.getChildFile("sfz/afro-kit/afro-kit.sfz");

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

    explicit BandHarness(bool loadKit = true)
    {
        fakeNow = 0.0;
        engine.setClock(&fakeClock);
        if (loadKit)
            REQUIRE(engine.loadDrumKit(afroKit));
        juce::String error;
        auto style = lb::loadStyle(afrobeats, error);
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
        }
        fakeNow = seconds; // the message thread acts at this time
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
    CHECK(style->layers == std::vector<std::string> { "drums", "perc" });
    REQUIRE(style->sections.size() == 2);
    CHECK(style->sections[0].name == "Verse");
    CHECK(style->sections[0].main.lengthBeats == 8.0);
    CHECK(style->sections[0].fill.lengthBeats == 4.0);
    CHECK(style->countIn.size() == 4);
    CHECK(style->entry.size() == 1);

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

TEST_CASE("GrooveLoader explains what is wrong with a broken style")
{
    juce::String error;
    CHECK(lb::loadStyle(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("no-such-style"), error) == nullptr);
    CHECK(error.contains("style.json"));

    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lb-style-" + juce::Uuid().toString());
    REQUIRE(folder.createDirectory());
    for (const auto* name : { "verse.mid", "chorus.mid", "fill-verse.mid", "fill-chorus.mid" })
        REQUIRE(afrobeats.getChildFile(name).copyFileTo(folder.getChildFile(name)));
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

TEST_CASE("Band: the callback does not allocate while the band plays, and the limiter holds the peaks")
{
    REQUIRE(lb::rt::isGuardActive());
    BandHarness h;
    h.engine.setDrumsGain(4.0f); // +12 dB, enough to hit the limiter
    h.runUntil(0.5);
    h.controller.setTempoBpm(110.0);
    h.controller.startStop();
    h.runUntil(3.0); // sfizz has started its voices

    const auto before = lb::rt::allocationCount();
    h.controller.next();
    h.runUntil(8.0);
    h.controller.fill();
    h.runUntil(12.0);
    h.controller.startStop();
    h.runUntil(16.0);
    CHECK(lb::rt::allocationCount() == before);

    const float peak = *std::max_element(h.output.begin(), h.output.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    CHECK(std::abs(peak) <= juce::Decibels::decibelsToGain(-1.0f) + 1e-6f);
    CHECK(std::abs(peak) > 0.5f);
    CHECK(h.engine.droppedCommands() == 0);
    CHECK(h.engine.droppedBeatTicks() == 0);
}

// Not run by default (about 30 minutes of audio; under a minute in Release):
//   liveband_engine_tests "[soak]"
// Measures how long each callback takes with the full kit, the band changing section and
// filling every 30 seconds, and the voice analysis running on a voice file, as fast as the
// machine allows. A callback slower than its 2.7 ms of audio would be an overrun.
//
// It checks the engine's sustained cost and reports, without failing on them, the rare
// callbacks over budget: on a shared or virtual machine even a loop doing identical work
// each time has a few of those, in CPU time as well as wall-clock time. The overrun count
// that matters is the app's own, on the laptop.
TEST_CASE("Band soak: 30 minutes of band, voice file and analysis", "[.soak]")
{
    BandHarness h;
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
    }
    REQUIRE(h.engine.bandStatus().state == lb::BandState::playing);

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
