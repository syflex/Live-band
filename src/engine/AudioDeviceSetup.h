#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace lb
{

inline constexpr double preferredSampleRate = 48000.0;
inline constexpr int preferredBufferSize = 128;

// Opens an audio device. Saved settings are restored when they still work. Otherwise the
// lowest-latency Windows driver that opens is used: WASAPI exclusive mode first, then WASAPI
// low-latency shared mode, then the system default. ASIO can be chosen in the settings dialog.
// Asks for 48 kHz and a 128-sample buffer; the driver may pick the nearest values it supports.
// Returns an error message, or an empty string on success.
juce::String openAudioDevice(juce::AudioDeviceManager& deviceManager, const juce::XmlElement* savedState);

} // namespace lb
