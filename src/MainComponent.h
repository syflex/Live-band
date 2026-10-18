#pragma once

#include "engine/AudioEngine.h"
#include "engine/BandController.h"
#include "ui/BeatLamps.h"
#include "ui/NoteDisplay.h"
#include "ui/PitchView.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <deque>
#include <optional>
#include <vector>

// The app window: audio settings, the band (start by tapping, sections, fills, levels, beat
// lights, the key it follows and the overrides), voice input (mic or a file), the live note
// and pitch view, session recording with an optional click, the latency test, a test
// instrument, and live diagnostics.
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::ChangeListener,
                            private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    // Uses an audio file named on the command line as the voice input.
    void handleCommandLine(const juce::String& commandLine);

    // Dropping an audio file on the window uses it as the voice input.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

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

    // Number keys 1 to 8 play C4 to C5 on the test instrument.
    static constexpr std::array<TestKey, 8> testKeys { { { '1', 60 }, { '2', 62 }, { '3', 64 }, { '4', 65 },
                                                         { '5', 67 }, { '6', 69 }, { '7', 71 }, { '8', 72 } } };

    // Band keys: acted on when pressed, not again while held.
    struct BandKey
    {
        int keyCode;
        bool held = false;
    };
    static constexpr int latencyRunsPerTest = 7;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    void setUpBandControls();
    void loadBand();
    void updateBand();
    juce::String bandStatusText(const lb::AudioEngine::BandStatus& status) const;
    juce::String keyStatusText(const lb::AudioEngine::BandStatus& status) const;
    bool pressBandKey(BandKey& key);
    void enableMidiInputs();
    void applyLatencyCompensation();
    static int countMissingSamples(const juce::File& kitFolder);

    void drainEngineQueues();
    void showAudioSettings();
    void confirmLatencyTest();
    void stepLatencyTest();
    void finishLatencyTest();
    void chooseInstrument();
    void loadInstrument(const juce::File& sfzFile);
    void chooseVoiceFile();
    bool loadVoiceFile(const juce::File& file, bool showErrors);
    void useVoiceFile(const juce::File& file);
    static bool isAudioFile(const juce::File& file);
    void startRecording();
    void stopRecording(const juce::String& reason);
    lb::ClickSettings currentClickSettings() const;
    juce::DynamicObject::Ptr sessionMetadata();
    void updateControls();
    void updateReport();
    juce::String buildReport();
    void saveDeviceState();
    void showMessage(const juce::String& title, const juce::String& message);
    static juce::File findAssetsDirectory();
    static juce::File sessionsDirectory();

    juce::ApplicationProperties properties_;
    juce::AudioDeviceManager deviceManager_;
    lb::AudioEngine engine_;
    lb::BandController band_ { engine_ };
    juce::String deviceError_;

    // Row 1: device and monitoring.
    juce::TextButton settingsButton_ { "Audio settings..." };
    juce::ToggleButton monitorToggle_ { "Hear my voice" };
    juce::Slider monitorGainSlider_;
    juce::TextButton latencyButton_ { "Measure latency" };
    juce::TextButton loadButton_ { "Load SFZ..." };

    // Band.
    juce::Label bandLabel_ { {}, "Band:" };
    juce::TextButton startButton_ { "Start" };
    juce::TextButton tapButton_ { "Tap" };
    juce::Slider bandBpmSlider_;
    juce::ComboBox sectionBox_;
    juce::TextButton nextButton_ { "Next" };
    juce::TextButton fillButton_ { "Fill" };
    juce::ToggleButton drumsToggle_ { "Drums" };
    juce::ToggleButton percToggle_ { "Perc" };
    juce::ToggleButton bassToggle_ { "Bass" };
    juce::ToggleButton keysToggle_ { "Keys" };
    juce::ToggleButton padToggle_ { "Pad" };
    juce::Label bandGainLabel_ { {}, "Band volume" };
    juce::Slider bandGainSlider_;
    BeatLamps beatLamps_;

    // Key.
    juce::Label keyLabel_ { {}, "Key:" };
    juce::Label keyStatus_;
    juce::TextButton lockButton_ { "Lock" };
    juce::TextButton keyDownButton_ { "-1" };
    juce::TextButton keyUpButton_ { "+1" };
    juce::TextButton relativeButton_ { "Relative" };

    // Row 2: voice input.
    juce::Label voiceLabel_ { {}, "Voice:" };
    juce::ToggleButton micButton_ { "Microphone" };
    juce::ToggleButton fileButton_ { "Voice file" };
    juce::TextButton loadVoiceButton_ { "Load voice file..." };
    juce::Label voiceFileLabel_;

    // Row 3: recording.
    juce::Label recordLabel_ { {}, "Record:" };
    juce::Slider bpmSlider_;
    juce::ComboBox meterBox_;
    juce::ComboBox tempoBox_;
    juce::ToggleButton clickToggle_ { "Click" };
    juce::TextButton recordButton_ { "Record" };
    juce::TextButton showRecordingsButton_ { "Show recordings" };
    juce::Label recordStatus_;

    NoteDisplay noteDisplay_;
    PitchView pitchView_;
    juce::TextButton resetButton_ { "Reset peaks" };
    juce::TextButton copyButton_ { "Copy report" };
    juce::TextEditor report_;
    juce::Label hint_;

    float inputLevelDb_ = -100.0f;
    float outputLevelDb_ = -100.0f;
    juce::Rectangle<int> meterArea_;
    int ticksUntilReport_ = 0;

    // Voice file (message thread).
    juce::File voiceFile_;
    double voiceFileRate_ = 0.0;

    // Recording state (message thread).
    bool recording_ = false;
    bool sessionStarted_ = false;
    juce::Time recordStartTime_;
    juce::File sessionFolder_;
    lb::ClickSettings sessionClick_;
    bool sessionClickEnabled_ = false;

    // Latency test state (message thread).
    int latencyRunsLeft_ = 0;
    double nextLatencyRunMs_ = 0.0;
    std::vector<double> latencyResultsMs_;
    int latencyFailedRuns_ = 0;
    juce::String latencySummary_;

    // Band state (message thread).
    juce::String bandProblem_;
    int missingKitSamples_ = 0;
    int missingPianoSamples_ = 0;
    std::deque<lb::BeatTick> pendingTicks_; // sent to the output, not heard yet
    std::optional<lb::BeatTick> heardTick_;
    BandKey spaceKey_ { juce::KeyPress::spaceKey };
    BandKey returnKey_ { juce::KeyPress::returnKey };
    BandKey nextKey_ { 'n' };
    BandKey fillKey_ { 'f' };
    BandKey lockKey_ { 'k' };
    BandKey relativeKey_ { 'm' };
    BandKey upKey_ { juce::KeyPress::upKey };
    BandKey downKey_ { juce::KeyPress::downKey };
    int ticksUntilMidiScan_ = 0;

    std::array<bool, testKeys.size()> testKeyDown_ {};
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::ScopedMessageBox messageBox_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
