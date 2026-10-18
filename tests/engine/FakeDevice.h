#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace lbtest
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;
constexpr int fakeOutputLatency = 32;

// An audio device that never runs by itself: tests call the engine's callback directly.
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
    int getOutputLatencyInSamples() override { return fakeOutputLatency; }
    int getInputLatencyInSamples() override { return 16; }
};

} // namespace lbtest
