#include "common/RtAllocGuard.h"
#include "engine/AudioEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;

class FakeDevice final : public juce::AudioIODevice
{
public:
    FakeDevice() : juce::AudioIODevice("Fake device", "Fake") {}

    juce::StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    juce::StringArray getInputChannelNames() override { return { "Mic" }; }
    juce::Array<double> getAvailableSampleRates() override { return { sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { blockSize }; }
    int getDefaultBufferSize() override { return blockSize; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 1; }
    int getOutputLatencyInSamples() override { return 32; }
    int getInputLatencyInSamples() override { return 16; }
};

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
    CHECK(h.engine.droppedNoteEvents() == 0);
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
}
