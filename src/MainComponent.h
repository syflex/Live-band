#pragma once

#include "engine/AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <vector>

// Phase 0 window: audio device settings, voice pass-through, a test instrument,
// the latency test, and live diagnostics (load, dropouts, levels).
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType cause) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    struct TestKey
    {
        int keyCode;
        int midiNote;
    };

    // Computer keys a s d f g h j k play C4 to C5 on the test instrument.
    static constexpr std::array<TestKey, 8> testKeys { { { 'a', 60 }, { 's', 62 }, { 'd', 64 }, { 'f', 65 },
                                                         { 'g', 67 }, { 'h', 69 }, { 'j', 71 }, { 'k', 72 } } };
    static constexpr int latencyRunsPerTest = 7;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void showAudioSettings();
    void confirmLatencyTest();
    void stepLatencyTest();
    void finishLatencyTest();
    void chooseInstrument();
    void loadInstrument(const juce::File& sfzFile);
    void updateReport();
    juce::String buildReport();
    void saveDeviceState();
    static juce::File findAssetsDirectory();

    juce::ApplicationProperties properties_;
    juce::AudioDeviceManager deviceManager_;
    lb::AudioEngine engine_;
    juce::String deviceError_;

    juce::TextButton settingsButton_ { "Audio settings..." };
    juce::ToggleButton monitorToggle_ { "Hear my voice" };
    juce::Slider monitorGainSlider_;
    juce::TextButton latencyButton_ { "Measure latency" };
    juce::TextButton loadButton_ { "Load SFZ..." };
    juce::TextButton resetButton_ { "Reset peaks" };
    juce::TextButton copyButton_ { "Copy report" };
    juce::TextEditor report_;
    juce::Label hint_;

    float inputLevelDb_ = -100.0f;
    float outputLevelDb_ = -100.0f;
    juce::Rectangle<int> meterArea_;
    int ticksUntilReport_ = 0;

    // Latency test state (message thread).
    int latencyRunsLeft_ = 0;
    double nextLatencyRunMs_ = 0.0;
    std::vector<double> latencyResultsMs_;
    int latencyFailedRuns_ = 0;
    juce::String latencySummary_;

    std::array<bool, testKeys.size()> testKeyDown_ {};
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::ScopedMessageBox messageBox_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
