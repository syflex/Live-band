#include "FakeDevice.h"
#include "common/RtAllocGuard.h"
#include "engine/AudioEngine.h"
#include "engine/AudioFileLoader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <optional>
#include <vector>

namespace
{
using lbtest::blockSize;
using lbtest::FakeDevice;
using lbtest::sampleRate;

// Drives an AudioEngine one callback at a time, the way a duplex driver does.
struct Harness
{
    explicit Harness(bool loadTestInstrument = true)
    {
        if (loadTestInstrument)
            REQUIRE(engine.loadInstrument(juce::File(LB_ASSETS_DIR "/sfz/test/sine.sfz")));
        engine.audioDeviceAboutToStart(&device);
    }

    ~Harness() { engine.audioDeviceStopped(); }

    void run(const float* mic)
    {
        const float* inputs[] = { mic };
        float* outputs[] = { left.data(), right.data() };
        engine.audioDeviceIOCallbackWithContext(inputs, 1, outputs, 2, blockSize, {});
    }

    void runSilence(int numBlocks)
    {
        const std::vector<float> silence(blockSize, 0.0f);
        for (int i = 0; i < numBlocks; ++i)
            run(silence.data());
    }

    FakeDevice device;
    lb::AudioEngine engine;
    std::vector<float> left = std::vector<float>(blockSize, 0.0f);
    std::vector<float> right = std::vector<float>(blockSize, 0.0f);
};

std::vector<float> sineBlock(float amplitude, double frequency, int blockIndex)
{
    std::vector<float> block(blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
        const double t = (blockIndex * blockSize + i) / sampleRate;
        block[static_cast<std::size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * frequency * t));
    }
    return block;
}

float peakOf(const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (float x : samples)
        peak = std::max(peak, std::abs(x));
    return peak;
}
} // namespace

TEST_CASE("AudioEngine reports the device format")
{
    Harness h;
    const auto info = h.engine.deviceInfo();
    CHECK(info.sampleRate == sampleRate);
    CHECK(info.bufferSize == blockSize);
    CHECK(info.inputLatencySamples == 16);
    CHECK(info.outputLatencySamples == 32);
}

TEST_CASE("AudioEngine keeps the voice out of the speakers while monitoring is off")
{
    Harness h;
    for (int b = 0; b < 10; ++b)
    {
        h.run(sineBlock(0.5f, 220.0, b).data());
        CHECK(peakOf(h.left) == 0.0f);
        CHECK(peakOf(h.right) == 0.0f);
    }
    CHECK(h.engine.takeInputPeak() == Catch::Approx(0.5f).margin(0.01));
    CHECK(h.engine.takeInputPeak() == 0.0f); // reading resets it
}

TEST_CASE("AudioEngine passes the voice through when monitoring is on")
{
    Harness h(false);
    h.engine.setMonitorEnabled(true);
    h.engine.setMonitorGain(0.5f);

    h.run(sineBlock(0.8f, 220.0, 0).data()); // the gain ramps up during this block

    for (int b = 1; b < 5; ++b)
    {
        const auto mic = sineBlock(0.8f, 220.0, b);
        h.run(mic.data());
        for (int i = 0; i < blockSize; ++i)
        {
            REQUIRE(h.left[static_cast<std::size_t>(i)] == Catch::Approx(0.5f * mic[static_cast<std::size_t>(i)]).margin(1e-6));
            REQUIRE(h.right[static_cast<std::size_t>(i)] == Catch::Approx(0.5f * mic[static_cast<std::size_t>(i)]).margin(1e-6));
        }
    }

    // Switching off ramps down within one block, then stays silent.
    h.engine.setMonitorEnabled(false);
    h.run(sineBlock(0.8f, 220.0, 5).data());
    h.run(sineBlock(0.8f, 220.0, 6).data());
    CHECK(peakOf(h.left) == 0.0f);
}

TEST_CASE("AudioEngine plays and stops test notes")
{
    Harness h;
    h.engine.noteOn(69, 100);

    float peak = 0.0f;
    for (int b = 0; b < 50; ++b)
    {
        h.runSilence(1);
        peak = std::max(peak, peakOf(h.left));
    }
    CHECK(peak > 0.02f);
    CHECK(h.engine.activeVoices() == 1);
    CHECK(h.engine.takeOutputPeak() > 0.02f);

    h.engine.allNotesOff();
    h.runSilence(2);
    CHECK(peakOf(h.left) == 0.0f);
    CHECK(h.engine.activeVoices() == 0);
    CHECK(h.engine.droppedCommands() == 0);
}

TEST_CASE("AudioEngine measures the round trip of a simulated loopback")
{
    Harness h;
    const std::size_t delay = 333;
    std::deque<float> line(delay, 0.0f); // what the "device" has played but not yet returned
    std::vector<float> mic(blockSize, 0.0f);

    REQUIRE(h.engine.latencyProbe().start());

    std::optional<lb::DelayEstimate> result;
    for (int b = 0; b < 1000 && !result; ++b)
    {
        h.run(mic.data());
        for (int i = 0; i < blockSize; ++i)
        {
            line.push_back(h.left[static_cast<std::size_t>(i)]);
            mic[static_cast<std::size_t>(i)] = 0.5f * line.front();
            line.pop_front();
        }
        result = h.engine.latencyProbe().takeResult();
    }

    REQUIRE(result.has_value());
    // One extra block: each callback receives the input captured during the previous block.
    CHECK(result->lagSamples == static_cast<long>(delay) + blockSize);
    CHECK(result->correlation > 0.8f);
}

TEST_CASE("AudioEngine callback does not allocate memory")
{
    REQUIRE(lb::rt::isGuardActive()); // otherwise this test would prove nothing

    Harness h;
    h.engine.setMonitorEnabled(true);

    // Warm up: let sfizz and the engine settle after preparation.
    h.engine.noteOn(60, 100);
    h.runSilence(10);
    h.engine.allNotesOff();
    h.runSilence(10);

    // Record a session with the click running, so writing to disk is covered too.
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lb-alloc-test-" + juce::Uuid().toString());
    juce::String error;
    REQUIRE(h.engine.startSession(folder, true, lb::ClickSettings {}, error));
    h.runSilence(2);

    const auto before = lb::rt::allocationCount();

    for (int round = 0; round < 5; ++round)
    {
        h.engine.noteOn(60 + round, 90);
        h.engine.noteOn(64 + round, 90);
        for (int b = 0; b < 20; ++b)
            h.run(sineBlock(0.3f, 330.0, b).data());
        h.engine.noteOff(60 + round);
        h.engine.noteOff(64 + round);
        h.runSilence(5);
    }

    REQUIRE(h.engine.latencyProbe().start());
    for (int b = 0; b < 200 && h.engine.latencyProbe().isBusy(); ++b)
        h.run(sineBlock(0.1f, 500.0, b).data());

    CHECK(lb::rt::allocationCount() == before);
    CHECK(h.engine.callbackStats().snapshot().callbacks > 100);

    h.engine.stopSession(new juce::DynamicObject());
    folder.deleteRecursively();
}

namespace
{
// Drains the engine's analysis queue, as the UI does.
std::vector<lb::AnalysisFrame> drainFrames(lb::AudioEngine& engine)
{
    std::vector<lb::AnalysisFrame> frames;
    lb::AnalysisFrame frame;
    while (engine.popAnalysisFrame(frame))
        frames.push_back(frame);
    return frames;
}

float medianVoicedMidi(const std::vector<lb::AnalysisFrame>& frames)
{
    std::vector<float> midi;
    for (const auto& f : frames)
        if (f.isVoiced())
            midi.push_back(f.midiPitch);
    if (midi.empty())
        return 0.0f;
    std::nth_element(midi.begin(), midi.begin() + static_cast<long>(midi.size() / 2), midi.end());
    return midi[midi.size() / 2];
}

std::unique_ptr<juce::AudioFormatReader> openWav(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}

juce::File makeTempFolder(const juce::String& name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + "-" + juce::Uuid().toString());
}
} // namespace

TEST_CASE("AudioEngine analyses the voice and queues frames for the UI")
{
    Harness h;
    for (int b = 0; b < 375; ++b) // 1 s
        h.run(sineBlock(0.3f, 220.0, b).data());

    const auto frames = drainFrames(h.engine);
    CHECK(frames.size() > 110);
    CHECK(medianVoicedMidi(frames) == Catch::Approx(57.0f).margin(0.1)); // A3
    CHECK(h.engine.droppedAnalysisFrames() == 0);

    // Frame times follow the engine's sample counter.
    CHECK(h.engine.currentSampleTime() == 375 * blockSize);
    CHECK(frames.back().sampleTime < h.engine.currentSampleTime());
    CHECK(frames.back().sampleTime > h.engine.currentSampleTime() - 4800);
}

TEST_CASE("AudioEngine can take the voice from a file instead of the mic")
{
    Harness h(false);
    h.engine.audioDeviceStopped();
    std::vector<float> file(48000);
    for (std::size_t i = 0; i < file.size(); ++i)
        file[i] = 0.3f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 330.0 * static_cast<double>(i) / sampleRate));
    h.engine.setFileInput(file, "test.wav");
    h.engine.audioDeviceAboutToStart(&h.device);

    h.engine.setInputSource(lb::AudioEngine::InputSource::file);
    h.engine.setMonitorEnabled(true);
    h.engine.setMonitorGain(1.0f);

    // The mic hears 220 Hz, the file plays 330 Hz: analysis, meter and monitoring follow the file.
    for (int b = 0; b < 375; ++b)
        h.run(sineBlock(0.1f, 220.0, b).data());
    CHECK(medianVoicedMidi(drainFrames(h.engine)) == Catch::Approx(64.02f).margin(0.1)); // E4
    CHECK(h.engine.takeInputPeak() == Catch::Approx(0.3f).margin(0.01));
    CHECK(peakOf(h.left) == Catch::Approx(0.3f).margin(0.01));

    h.engine.setInputSource(lb::AudioEngine::InputSource::microphone);
    drainFrames(h.engine);
    for (int b = 0; b < 375; ++b)
        h.run(sineBlock(0.3f, 220.0, b).data());
    CHECK(medianVoicedMidi(drainFrames(h.engine)) == Catch::Approx(57.0f).margin(0.1));
}

TEST_CASE("AudioEngine records a session with a click")
{
    Harness h;
    const auto folder = makeTempFolder("lb-session-test");

    lb::ClickSettings click;
    click.bpm = 120.0;
    juce::String error;
    REQUIRE(h.engine.startSession(folder, true, click, error));
    CHECK(h.engine.isSessionOpen());
    CHECK(h.engine.sessionStartSample() == -1); // the audio thread has not started it yet

    // About 2 s of a 220 Hz voice, drained like the UI does.
    const int blocks = 750;
    int clicks = 0;
    for (int b = 0; b < blocks; ++b)
    {
        h.run(sineBlock(0.25f, 220.0, b).data());
        const auto start = h.engine.sessionStartSample();
        REQUIRE(start == 0);
        lb::AnalysisFrame frame;
        while (h.engine.popAnalysisFrame(frame))
            h.engine.recorder().writeFrame(frame, start);
        lb::ClickEvent event;
        while (h.engine.popClickEvent(event))
        {
            h.engine.recorder().writeClick(event, start);
            ++clicks;
        }
    }
    CHECK(clicks == 4); // 120 BPM for 2 s

    juce::DynamicObject::Ptr meta = new juce::DynamicObject();
    meta->setProperty("note", "test");
    h.engine.stopSession(meta);
    CHECK_FALSE(h.engine.isSessionOpen());
    h.runSilence(1);
    CHECK(h.engine.sessionStartSample() == -1);

    // input.wav holds the voice only (no click); output.wav has the click.
    auto input = openWav(folder.getChildFile("input.wav"));
    auto output = openWav(folder.getChildFile("output.wav"));
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    CHECK(input->numChannels == 1);
    CHECK(output->numChannels == 2);
    CHECK(input->sampleRate == sampleRate);
    CHECK(input->lengthInSamples == blocks * blockSize);
    CHECK(output->lengthInSamples == blocks * blockSize);

    juce::AudioBuffer<float> inputAudio(1, blocks * blockSize);
    input->read(&inputAudio, 0, blocks * blockSize, 0, true, false);
    CHECK(inputAudio.getMagnitude(0, 0, blocks * blockSize) == Catch::Approx(0.25f).margin(0.01));
    juce::AudioBuffer<float> outputAudio(2, 2000);
    output->read(&outputAudio, 0, 2000, 0, true, true);
    CHECK(outputAudio.getMagnitude(0, 0, 2000) > 0.1f); // first click at sample 0

    // CSVs: header plus rows, times from the start of input.wav.
    juce::StringArray analysisLines, clickLines;
    analysisLines.addLines(folder.getChildFile("analysis.csv").loadFileAsString().trim());
    clickLines.addLines(folder.getChildFile("clicks.csv").loadFileAsString().trim());
    CHECK(analysisLines[0] == "time_s,f0_hz,midi,periodicity,rms_db,odf,voiced,onset");
    CHECK(analysisLines.size() > 200);
    REQUIRE(clickLines.size() == 5);
    CHECK(clickLines[1].startsWith("0.00000,-1,0,0,120.000")); // count-in bar
    CHECK(clickLines[2].startsWith("0.50000,-1,1,0"));

    const auto json = juce::JSON::parse(folder.getChildFile("session.json"));
    CHECK(json["note"].toString() == "test");
    CHECK(static_cast<double>(json["sampleRate"]) == sampleRate);
    CHECK(static_cast<int>(json["clicks"]) == 4);
    CHECK(static_cast<int>(json["droppedAudioBlocks"]) == 0);

    folder.deleteRecursively();
}

TEST_CASE("AudioEngine ends a session when the device restarts")
{
    Harness h;
    const auto folder = makeTempFolder("lb-restart-test");
    juce::String error;
    REQUIRE(h.engine.startSession(folder, false, lb::ClickSettings {}, error));
    h.runSilence(3);
    CHECK(h.engine.sessionStartSample() >= 0);

    h.engine.audioDeviceStopped();
    h.engine.audioDeviceAboutToStart(&h.device);
    CHECK(h.engine.sessionStartSample() == -1); // the UI sees this and closes the files
    h.engine.stopSession(new juce::DynamicObject());
    CHECK(folder.getChildFile("session.json").existsAsFile());
    folder.deleteRecursively();
}

TEST_CASE("loadMonoAudio mixes to mono and resamples")
{
    const auto folder = makeTempFolder("lb-loader-test");
    REQUIRE(folder.createDirectory());
    const auto file = folder.getChildFile("stereo.wav");

    {
        // 1 s of stereo at 44.1 kHz: 441 Hz on the left, silence on the right.
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(44100.0).withNumChannels(2).withBitsPerSample(24));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> buffer(2, 44100);
        buffer.clear();
        for (int i = 0; i < 44100; ++i)
            buffer.setSample(0, i, 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 441.0 * i / 44100.0)));
        writer->writeFromAudioSampleBuffer(buffer, 0, 44100);
    }

    juce::String error;
    const auto samples = lb::loadMonoAudio(file, 48000.0, error);
    REQUIRE(samples.has_value());
    CHECK(samples->size() == 48000);
    const float peak = std::abs(*std::max_element(samples->begin(), samples->end(), [](float a, float b) { return std::abs(a) < std::abs(b); }));
    CHECK(peak == Catch::Approx(0.25f).margin(0.01)); // half of the left channel's 0.5

    CHECK_FALSE(lb::loadMonoAudio(folder.getChildFile("missing.wav"), 48000.0, error).has_value());
    CHECK(error.isNotEmpty());
    folder.deleteRecursively();
}
