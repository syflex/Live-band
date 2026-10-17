#include "engine/AudioFileLoader.h"

#include <cmath>

namespace lb
{

std::optional<std::vector<float>> loadMonoAudio(const juce::File& file, double targetSampleRate, juce::String& error, double maxSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "This file could not be read as audio: " + file.getFileName();
        return std::nullopt;
    }
    if (reader->sampleRate <= 0.0 || targetSampleRate <= 0.0)
    {
        error = "Unknown sample rate.";
        return std::nullopt;
    }
    if (static_cast<double>(reader->lengthInSamples) / reader->sampleRate > maxSeconds)
    {
        error = "The file is longer than " + juce::String(static_cast<int>(maxSeconds / 60.0)) + " minutes.";
        return std::nullopt;
    }

    const auto length = static_cast<int>(reader->lengthInSamples);
    const auto channels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(channels, length);
    reader->read(&buffer, 0, length, 0, true, true);

    std::vector<float> mono(static_cast<std::size_t>(length), 0.0f);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::addWithMultiply(mono.data(), buffer.getReadPointer(ch), 1.0f / static_cast<float>(channels), length);

    if (std::abs(reader->sampleRate - targetSampleRate) < 0.5)
        return mono;

    const double ratio = reader->sampleRate / targetSampleRate; // input samples per output sample
    const auto outputLength = static_cast<int>(std::floor(length / ratio));
    std::vector<float> resampled(static_cast<std::size_t>(outputLength), 0.0f);
    juce::WindowedSincInterpolator interpolator;
    interpolator.process(ratio, mono.data(), resampled.data(), outputLength, length, 0);
    return resampled;
}

} // namespace lb
