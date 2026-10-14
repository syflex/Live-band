#include "engine/AudioDeviceSetup.h"

namespace lb
{
namespace
{
constexpr int numInputChannels = 1; // the headset mic; more can be enabled in the settings dialog
constexpr int numOutputChannels = 2;

void requestPreferredFormat(juce::AudioDeviceManager& deviceManager)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = preferredSampleRate;
    setup.bufferSize = preferredBufferSize;
    deviceManager.setAudioDeviceSetup(setup, true);
}

bool tryDeviceType(juce::AudioDeviceManager& deviceManager, const juce::String& typeName)
{
    bool available = false;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        available = available || type->getTypeName() == typeName;

    if (!available)
        return false;

    deviceManager.setCurrentAudioDeviceType(typeName, true);
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return false;

    requestPreferredFormat(deviceManager);
    return deviceManager.getCurrentAudioDevice() != nullptr;
}
} // namespace

juce::String openAudioDevice(juce::AudioDeviceManager& deviceManager, const juce::XmlElement* savedState)
{
    if (savedState != nullptr)
    {
        const auto error = deviceManager.initialise(numInputChannels, numOutputChannels, savedState, false);
        if (error.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
            return {};
    }

    deviceManager.initialiseWithDefaultDevices(numInputChannels, numOutputChannels);

    for (const auto* typeName : { "Windows Audio (Exclusive Mode)", "Windows Audio (Low Latency Mode)" })
        if (tryDeviceType(deviceManager, typeName))
            return {};

    // Nothing better opened (or this is not Windows): go back to the system default.
    const auto error = deviceManager.initialiseWithDefaultDevices(numInputChannels, numOutputChannels);
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return error.isNotEmpty() ? error : juce::String("No audio device could be opened.");

    requestPreferredFormat(deviceManager);
    return {};
}

} // namespace lb
