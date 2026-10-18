#include "MainComponent.h"

#include "common/RtAllocGuard.h"
#include "engine/AudioDeviceSetup.h"
#include "engine/AudioFileLoader.h"
#include "engine/GrooveLoader.h"

#include <algorithm>
#include <utility>

namespace
{
constexpr auto audioDeviceKey = "audioDevice";
constexpr auto latencyMsKey = "latency.roundTripMs";
constexpr auto latencyDeviceKey = "latency.device";
constexpr auto latencyDateKey = "latency.measuredAt";
constexpr auto voiceFolderKey = "voiceFile.folder";
constexpr auto bandVolumeKey = "band.volumeDb";
constexpr auto bandDrumsKey = "band.drums";
constexpr auto bandPercKey = "band.perc";
constexpr auto bandBassKey = "band.bass";
constexpr auto bandKeysKey = "band.keys";
constexpr auto bandPadKey = "band.pad";

constexpr float meterFloorDb = -60.0f;
constexpr float meterDecayDbPerTick = 1.5f; // at 30 Hz, about 45 dB per second

enum MeterId
{
    fourFour = 1,
    sixEight = 2
};

enum TempoId
{
    steadyTempo = 1,
    speedUp = 2,
    slowDown = 3
};

juce::String formatMs(double ms)
{
    return juce::String(ms, 1) + " ms";
}

juce::String formatDuration(double seconds)
{
    const int total = juce::roundToInt(seconds);
    return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}

void drawMeter(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, float levelDb)
{
    g.setColour(juce::Colours::white);
    g.drawText(label, area.removeFromLeft(48), juce::Justification::centredLeft);

    auto valueArea = area.removeFromRight(70);
    const auto bar = area.reduced(0, 5);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRect(bar);

    const float proportion = juce::jlimit(0.0f, 1.0f, (levelDb - meterFloorDb) / -meterFloorDb);
    g.setColour(levelDb > -3.0f ? juce::Colours::red : levelDb > -12.0f ? juce::Colours::yellow : juce::Colours::limegreen);
    g.fillRect(bar.withWidth(juce::roundToInt(static_cast<float>(bar.getWidth()) * proportion)));

    g.setColour(juce::Colours::white);
    g.drawText(levelDb <= meterFloorDb ? juce::String("silent") : juce::String(levelDb, 1) + " dB",
               valueArea, juce::Justification::centredRight);
}
} // namespace

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "LiveBand";
    options.filenameSuffix = ".settings";
    options.folderName = "LiveBand";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);

    // Load the instruments and the groove before audio starts; loading is not real-time safe.
    engine_.loadInstrument(findAssetsDirectory().getChildFile("sfz/test/sine.sfz"));
    loadBand();

    const auto savedDevice = properties_.getUserSettings()->getXmlValue(audioDeviceKey);
    deviceError_ = lb::openAudioDevice(deviceManager_, savedDevice.get());
    deviceManager_.addAudioCallback(&engine_);
    deviceManager_.addChangeListener(this);
    deviceManager_.addMidiInputDeviceCallback({}, this);
    enableMidiInputs();
    applyLatencyCompensation();
    pitchView_.setSampleRate(engine_.deviceInfo().sampleRate > 0.0 ? engine_.deviceInfo().sampleRate : 48000.0);

    for (auto* button : { &settingsButton_, &latencyButton_, &loadButton_, &loadVoiceButton_, &recordButton_, &showRecordingsButton_,
                          &resetButton_, &copyButton_ })
    {
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(button);
    }
    settingsButton_.onClick = [this] { showAudioSettings(); };
    latencyButton_.onClick = [this] { confirmLatencyTest(); };
    loadButton_.onClick = [this] { chooseInstrument(); };
    loadVoiceButton_.onClick = [this] { chooseVoiceFile(); };
    recordButton_.onClick = [this] { recording_ ? stopRecording({}) : startRecording(); };
    showRecordingsButton_.onClick = [] {
        const auto folder = sessionsDirectory();
        folder.createDirectory();
        folder.startAsProcess();
    };
    resetButton_.onClick = [this] { engine_.callbackStats().resetPeak(); };
    copyButton_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(buildReport()); };

    // Monitoring.
    monitorToggle_.setWantsKeyboardFocus(false);
    monitorToggle_.onClick = [this] { engine_.setMonitorEnabled(monitorToggle_.getToggleState()); };
    addAndMakeVisible(monitorToggle_);

    monitorGainSlider_.setRange(-30.0, 6.0, 0.5);
    monitorGainSlider_.setValue(-3.0, juce::dontSendNotification);
    monitorGainSlider_.setTextValueSuffix(" dB");
    monitorGainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    monitorGainSlider_.setWantsKeyboardFocus(false);
    monitorGainSlider_.onValueChange = [this] {
        engine_.setMonitorGain(juce::Decibels::decibelsToGain(static_cast<float>(monitorGainSlider_.getValue())));
    };
    monitorGainSlider_.onValueChange();
    addAndMakeVisible(monitorGainSlider_);

    // Voice input.
    for (auto* button : { &micButton_, &fileButton_ })
    {
        button->setRadioGroupId(1);
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(button);
    }
    micButton_.setToggleState(true, juce::dontSendNotification);
    micButton_.onClick = [this] { engine_.setInputSource(lb::AudioEngine::InputSource::microphone); updateControls(); };
    fileButton_.onClick = [this] {
        if (!engine_.hasFileInput())
        {
            micButton_.setToggleState(true, juce::dontSendNotification);
            chooseVoiceFile();
            return;
        }
        engine_.setInputSource(lb::AudioEngine::InputSource::file);
        updateControls();
    };
    voiceFileLabel_.setText("no file loaded", juce::dontSendNotification);
    voiceFileLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    for (auto* label : { &voiceLabel_, &recordLabel_, &voiceFileLabel_, &recordStatus_ })
        addAndMakeVisible(label);

    // Recording.
    bpmSlider_.setSliderStyle(juce::Slider::IncDecButtons);
    bpmSlider_.setRange(40.0, 200.0, 1.0);
    bpmSlider_.setValue(100.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 22);
    bpmSlider_.setTooltip("Click tempo. In 6/8 this counts the dotted-quarter beat (two per bar).");
    addAndMakeVisible(bpmSlider_);

    meterBox_.addItem("4/4", fourFour);
    meterBox_.addItem("6/8", sixEight);
    meterBox_.setSelectedId(fourFour, juce::dontSendNotification);
    meterBox_.setWantsKeyboardFocus(false);
    addAndMakeVisible(meterBox_);

    tempoBox_.addItem("Steady tempo", steadyTempo);
    tempoBox_.addItem("Speed up 5%", speedUp);
    tempoBox_.addItem("Slow down 5%", slowDown);
    tempoBox_.setSelectedId(steadyTempo, juce::dontSendNotification);
    tempoBox_.setTooltip("Speed up or slow down: the click changes tempo gradually over 16 bars, for testing tempo following later.");
    tempoBox_.setWantsKeyboardFocus(false);
    addAndMakeVisible(tempoBox_);

    clickToggle_.setToggleState(true, juce::dontSendNotification);
    clickToggle_.setWantsKeyboardFocus(false);
    clickToggle_.setTooltip("Play a click in the headphones while recording. It is not recorded into input.wav.");
    addAndMakeVisible(clickToggle_);

    recordStatus_.setText("Recordings are saved in " + sessionsDirectory().getFullPathName(), juce::dontSendNotification);
    recordStatus_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    // Live analysis.
    addAndMakeVisible(noteDisplay_);
    addAndMakeVisible(pitchView_);

    hint_.setText("Band keys: Space taps the beat (four taps start the band), Enter starts or stops, N next section, F fill, "
                  "Esc stops at once. A MIDI pedal or pad taps too. Key: K locks it (or listens again), Up/Down a semitone, M relative.\n"
                  "Keys 1 to 8 play test notes. Drop a WAV file here to use it as the voice. Use \"Hear my voice\" only with headphones.",
                  juce::dontSendNotification);
    hint_.setJustificationType(juce::Justification::topLeft);
    hint_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(hint_);

    setUpBandControls();

    report_.setMultiLine(true);
    report_.setReadOnly(true);
    report_.setCaretVisible(false);
    report_.setWantsKeyboardFocus(false);
    report_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    addAndMakeVisible(report_);

    setWantsKeyboardFocus(true);
    setSize(960, 960);
    updateControls();
    updateReport();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (recording_)
        stopRecording({});
    deviceManager_.removeMidiInputDeviceCallback({}, this);
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(&engine_);
    properties_.saveIfNeeded();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    auto area = meterArea_;
    drawMeter(g, area.removeFromTop(area.getHeight() / 2), engine_.inputSource() == lb::AudioEngine::InputSource::file ? "File" : "Mic",
              inputLevelDb_);
    drawMeter(g, area, "Out", outputLevelDb_);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);
    const auto gap = [](juce::Rectangle<int>& row, int width) { row.removeFromLeft(width); };

    auto row = area.removeFromTop(30);
    settingsButton_.setBounds(row.removeFromLeft(140));
    gap(row, 16);
    monitorToggle_.setBounds(row.removeFromLeft(120));
    monitorGainSlider_.setBounds(row.removeFromLeft(230));
    gap(row, 16);
    latencyButton_.setBounds(row.removeFromLeft(140));
    gap(row, 8);
    loadButton_.setBounds(row.removeFromLeft(110));
    area.removeFromTop(10);

    row = area.removeFromTop(30);
    bandLabel_.setBounds(row.removeFromLeft(60));
    startButton_.setBounds(row.removeFromLeft(80));
    gap(row, 6);
    tapButton_.setBounds(row.removeFromLeft(80));
    gap(row, 10);
    bandBpmSlider_.setBounds(row.removeFromLeft(150));
    gap(row, 10);
    sectionBox_.setBounds(row.removeFromLeft(110));
    gap(row, 6);
    nextButton_.setBounds(row.removeFromLeft(70));
    gap(row, 6);
    fillButton_.setBounds(row.removeFromLeft(70));
    gap(row, 14);
    drumsToggle_.setBounds(row.removeFromLeft(80));
    percToggle_.setBounds(row.removeFromLeft(70));
    area.removeFromTop(4);

    row = area.removeFromTop(30);
    row.removeFromLeft(60);
    bandGainSlider_.setBounds(row.removeFromRight(220));
    bandGainLabel_.setBounds(row.removeFromRight(90));
    beatLamps_.setBounds(row);
    area.removeFromTop(4);

    row = area.removeFromTop(30);
    keyLabel_.setBounds(row.removeFromLeft(60));
    padToggle_.setBounds(row.removeFromRight(60));
    keysToggle_.setBounds(row.removeFromRight(70));
    bassToggle_.setBounds(row.removeFromRight(70));
    row.removeFromRight(10);
    relativeButton_.setBounds(row.removeFromRight(110));
    row.removeFromRight(6);
    keyUpButton_.setBounds(row.removeFromRight(40));
    row.removeFromRight(4);
    keyDownButton_.setBounds(row.removeFromRight(40));
    row.removeFromRight(6);
    lockButton_.setBounds(row.removeFromRight(80));
    row.removeFromRight(8);
    keyStatus_.setBounds(row);
    area.removeFromTop(10);

    row = area.removeFromTop(30);
    voiceLabel_.setBounds(row.removeFromLeft(60));
    micButton_.setBounds(row.removeFromLeft(110));
    fileButton_.setBounds(row.removeFromLeft(100));
    loadVoiceButton_.setBounds(row.removeFromLeft(140));
    gap(row, 8);
    copyButton_.setBounds(row.removeFromRight(110));
    row.removeFromRight(8);
    resetButton_.setBounds(row.removeFromRight(110));
    voiceFileLabel_.setBounds(row);
    area.removeFromTop(8);

    row = area.removeFromTop(30);
    recordLabel_.setBounds(row.removeFromLeft(60));
    bpmSlider_.setBounds(row.removeFromLeft(150));
    gap(row, 8);
    meterBox_.setBounds(row.removeFromLeft(70));
    gap(row, 8);
    tempoBox_.setBounds(row.removeFromLeft(140));
    gap(row, 8);
    clickToggle_.setBounds(row.removeFromLeft(70));
    recordButton_.setBounds(row.removeFromLeft(100));
    gap(row, 8);
    showRecordingsButton_.setBounds(row.removeFromLeft(140));
    area.removeFromTop(2);
    recordStatus_.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    meterArea_ = area.removeFromTop(50);
    area.removeFromTop(6);

    auto analysisRow = area.removeFromTop(240);
    noteDisplay_.setBounds(analysisRow.removeFromLeft(250));
    analysisRow.removeFromLeft(8);
    pitchView_.setBounds(analysisRow);
    area.removeFromTop(4);

    hint_.setBounds(area.removeFromTop(56));
    area.removeFromTop(4);
    report_.setBounds(area);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        band_.stopNow();
        engine_.allNotesOff();
        testKeyDown_.fill(false);
        return true;
    }

    // Band keys. The tap time is taken now, as close to the key press as the app gets.
    const auto character = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    if (key == juce::KeyPress::spaceKey)
    {
        if (pressBandKey(spaceKey_))
            band_.tap(engine_.now());
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        if (pressBandKey(returnKey_))
            band_.startStop();
        return true;
    }
    if (character == nextKey_.keyCode && !key.getModifiers().isAnyModifierKeyDown())
    {
        if (pressBandKey(nextKey_))
            band_.next();
        return true;
    }
    if (character == fillKey_.keyCode && !key.getModifiers().isAnyModifierKeyDown())
    {
        if (pressBandKey(fillKey_))
            band_.fill();
        return true;
    }
    if (character == lockKey_.keyCode && !key.getModifiers().isAnyModifierKeyDown())
    {
        if (pressBandKey(lockKey_))
            band_.lockOrRelisten();
        return true;
    }
    if (character == relativeKey_.keyCode && !key.getModifiers().isAnyModifierKeyDown())
    {
        if (pressBandKey(relativeKey_))
            band_.toggleRelativeKey();
        return true;
    }
    if (key.getKeyCode() == upKey_.keyCode || key.getKeyCode() == downKey_.keyCode)
    {
        const bool up = key.getKeyCode() == upKey_.keyCode;
        if (pressBandKey(up ? upKey_ : downKey_))
            band_.shiftKey(up ? 1 : -1);
        return true;
    }

    // Notes start and stop in keyStateChanged(); swallow the presses (and auto-repeats) here.
    return std::any_of(testKeys.begin(), testKeys.end(), [&](const TestKey& k) {
        return juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode())) == k.keyCode;
    });
}

bool MainComponent::keyStateChanged(bool)
{
    for (auto* key : { &spaceKey_, &returnKey_, &nextKey_, &fillKey_, &lockKey_, &relativeKey_, &upKey_, &downKey_ })
        if (key->held && !juce::KeyPress::isKeyCurrentlyDown(key->keyCode))
            key->held = false;

    bool changed = false;
    for (std::size_t i = 0; i < testKeys.size(); ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown(testKeys[i].keyCode);
        if (down == testKeyDown_[i])
            continue;

        testKeyDown_[i] = down;
        if (down)
            engine_.noteOn(testKeys[i].midiNote, 100);
        else
            engine_.noteOff(testKeys[i].midiNote);
        changed = true;
    }
    return changed;
}

void MainComponent::focusLost(FocusChangeType)
{
    for (auto* key : { &spaceKey_, &returnKey_, &nextKey_, &fillKey_, &lockKey_, &relativeKey_, &upKey_, &downKey_ })
        key->held = false;
    for (std::size_t i = 0; i < testKeys.size(); ++i)
        if (std::exchange(testKeyDown_[i], false))
            engine_.noteOff(testKeys[i].midiNote);
}

void MainComponent::mouseDown(const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

void MainComponent::timerCallback()
{
    if (isShowing() && !hasKeyboardFocus(true) && getPeer() != nullptr && getPeer()->isFocused())
        grabKeyboardFocus();

    const auto toDb = [](float peak) { return juce::Decibels::gainToDecibels(peak, meterFloorDb); };
    inputLevelDb_ = std::max(toDb(engine_.takeInputPeak()), inputLevelDb_ - meterDecayDbPerTick);
    outputLevelDb_ = std::max(toDb(engine_.takeOutputPeak()), outputLevelDb_ - meterDecayDbPerTick);
    repaint(meterArea_);

    drainEngineQueues();
    noteDisplay_.repaint();
    pitchView_.repaint();
    band_.update();
    updateBand();

    if (--ticksUntilMidiScan_ <= 0)
    {
        ticksUntilMidiScan_ = 60; // every two seconds: pick up pedals plugged in later
        enableMidiInputs();
    }

    if (recording_)
    {
        if (engine_.sessionStartSample() >= 0)
            sessionStarted_ = true;
        else if (sessionStarted_)
            stopRecording("Recording stopped because the audio device restarted.");
    }

    stepLatencyTest();

    if (--ticksUntilReport_ <= 0)
    {
        ticksUntilReport_ = 10;
        updateControls();
        updateReport();
    }
}

void MainComponent::drainEngineQueues()
{
    lb::AnalysisFrame frame;
    while (engine_.popAnalysisFrame(frame))
    {
        noteDisplay_.addFrame(frame);
        pitchView_.addFrame(frame);
        band_.analysisFrame(frame);
        const auto start = engine_.sessionStartSample();
        if (engine_.isSessionOpen() && start >= 0)
            engine_.recorder().writeFrame(frame, start);
    }

    lb::ClickEvent click;
    while (engine_.popClickEvent(click))
    {
        const auto start = engine_.sessionStartSample();
        if (engine_.isSessionOpen() && start >= 0)
            engine_.recorder().writeClick(click, start);
    }

    lb::BeatTick tick;
    while (engine_.popBeatTick(tick))
    {
        pendingTicks_.push_back(tick);
        const auto start = engine_.sessionStartSample();
        if (engine_.isSessionOpen() && start >= 0)
            engine_.recorder().writeBeat(tick, start);
    }
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // The device or its settings changed.
    deviceError_.clear();
    saveDeviceState();

    const double rate = engine_.deviceInfo().sampleRate;
    pitchView_.setSampleRate(rate > 0.0 ? rate : 48000.0);

    // A loaded voice file has to match the device's sample rate.
    if (voiceFile_.existsAsFile() && rate > 0.0 && std::abs(rate - voiceFileRate_) > 0.5)
        loadVoiceFile(voiceFile_, false);

    // A restart stops the band; the latency measurement only applies to the device it was made on.
    pendingTicks_.clear();
    heardTick_.reset();
    applyLatencyCompensation();

    updateControls();
    updateReport();
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(deviceManager_, 1, 2, 2, 2, false, false, false, false);
    selector->setSize(560, 440);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::showMessage(const juce::String& title, const juce::String& message)
{
    messageBox_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                         .withTitle(title)
                                                         .withMessage(message)
                                                         .withButton("OK")
                                                         .withAssociatedComponent(this),
                                                     nullptr);
}

//==================================================================================================
// Voice file

void MainComponent::chooseVoiceFile()
{
    const auto lastFolder = properties_.getUserSettings()->getValue(voiceFolderKey);
    const juce::File start = lastFolder.isNotEmpty() ? juce::File(lastFolder) : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose a voice recording", start, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file.existsAsFile())
                                      useVoiceFile(file);
                              });
}

void MainComponent::useVoiceFile(const juce::File& file)
{
    if (recording_)
    {
        showMessage("Recording in progress", "Stop the recording before changing the voice file.");
        return;
    }
    if (loadVoiceFile(file, true))
    {
        properties_.getUserSettings()->setValue(voiceFolderKey, file.getParentDirectory().getFullPathName());
        fileButton_.setToggleState(true, juce::dontSendNotification);
        engine_.setInputSource(lb::AudioEngine::InputSource::file);
    }
    updateControls();
}

bool MainComponent::isAudioFile(const juce::File& file)
{
    return file.hasFileExtension("wav;aif;aiff;flac;ogg");
}

void MainComponent::handleCommandLine(const juce::String& commandLine)
{
    for (const auto& argument : juce::StringArray::fromTokens(commandLine, true))
    {
        const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(argument.unquoted());
        if (isAudioFile(file) && file.existsAsFile())
        {
            useVoiceFile(file);
            return;
        }
    }
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && isAudioFile(juce::File(files[0]));
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() == 1)
        useVoiceFile(juce::File(files[0]));
}

bool MainComponent::loadVoiceFile(const juce::File& file, bool showErrors)
{
    const double rate = engine_.deviceInfo().sampleRate;
    if (rate <= 0.0)
    {
        if (showErrors)
            showMessage("No audio device", "Open an audio device in Audio settings first.");
        return false;
    }

    juce::String error;
    auto samples = lb::loadMonoAudio(file, rate, error);
    if (!samples)
    {
        if (showErrors)
            showMessage("Could not load the voice file", error);
        return false;
    }

    // Swapping the buffer is not real-time safe, so pause the callback briefly.
    deviceManager_.removeAudioCallback(&engine_);
    engine_.setFileInput(std::move(*samples), file.getFileName());
    deviceManager_.addAudioCallback(&engine_);

    voiceFile_ = file;
    voiceFileRate_ = rate;
    return true;
}

//==================================================================================================
// Recording

lb::ClickSettings MainComponent::currentClickSettings() const
{
    lb::ClickSettings click;
    click.bpm = bpmSlider_.getValue();
    if (meterBox_.getSelectedId() == sixEight)
    {
        click.beatsPerBar = 2;
        click.clicksPerBeat = 3;
    }
    click.rampPercent = tempoBox_.getSelectedId() == speedUp ? 5.0 : tempoBox_.getSelectedId() == slowDown ? -5.0 : 0.0;
    click.rampBars = 16;
    click.countInBars = 1;
    return click;
}

void MainComponent::startRecording()
{
    if (recording_)
        return;

    sessionClick_ = currentClickSettings();
    sessionClickEnabled_ = clickToggle_.getToggleState();
    const auto folder = sessionsDirectory().getChildFile(juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S"));

    juce::String error;
    if (!sessionsDirectory().createDirectory() || !engine_.startSession(folder, sessionClickEnabled_, sessionClick_, error))
    {
        showMessage("Could not start recording", error.isNotEmpty() ? error : "Cannot create " + sessionsDirectory().getFullPathName());
        return;
    }

    recording_ = true;
    sessionStarted_ = false;
    recordStartTime_ = juce::Time::getCurrentTime();
    sessionFolder_ = folder;
    updateControls();
}

void MainComponent::stopRecording(const juce::String& reason)
{
    if (!recording_)
        return;

    drainEngineQueues(); // write the last frames and clicks before the files close
    engine_.stopSession(sessionMetadata());
    recording_ = false;
    sessionStarted_ = false;

    recordStatus_.setText((reason.isNotEmpty() ? reason + " " : juce::String()) + "Saved to " + sessionFolder_.getFullPathName(),
                          juce::dontSendNotification);
    updateControls();
}

juce::DynamicObject::Ptr MainComponent::sessionMetadata()
{
    juce::DynamicObject::Ptr meta = new juce::DynamicObject();
    meta->setProperty("app", JUCE_APPLICATION_NAME_STRING);
    meta->setProperty("version", JUCE_APPLICATION_VERSION_STRING);
    meta->setProperty("startedAt", recordStartTime_.toISO8601(true));
    meta->setProperty("durationSeconds", (juce::Time::getCurrentTime() - recordStartTime_).inSeconds());

    const auto setup = deviceManager_.getAudioDeviceSetup();
    const auto info = engine_.deviceInfo();
    meta->setProperty("driver", deviceManager_.getCurrentAudioDeviceType());
    meta->setProperty("inputDevice", setup.inputDeviceName);
    meta->setProperty("outputDevice", setup.outputDeviceName);
    meta->setProperty("bufferSize", info.bufferSize);

    const bool fromFile = engine_.inputSource() == lb::AudioEngine::InputSource::file;
    meta->setProperty("voiceSource", fromFile ? "file" : "microphone");
    if (fromFile)
        meta->setProperty("voiceFile", engine_.fileInputName());

    juce::DynamicObject::Ptr click = new juce::DynamicObject();
    click->setProperty("enabled", sessionClickEnabled_);
    click->setProperty("bpm", sessionClick_.bpm);
    click->setProperty("meter", sessionClick_.clicksPerBeat == 3 ? "6/8" : "4/4");
    click->setProperty("countInBars", sessionClick_.countInBars);
    click->setProperty("rampPercent", sessionClick_.rampPercent);
    click->setProperty("rampBars", sessionClick_.rampBars);
    meta->setProperty("click", juce::var(click.get()));

    juce::DynamicObject::Ptr bandInfo = new juce::DynamicObject();
    bandInfo->setProperty("style", engine_.style() != nullptr ? juce::String(engine_.style()->name) : juce::String());
    bandInfo->setProperty("kit", engine_.drumKitName());
    if (const auto* style = engine_.style())
        for (std::size_t i = 0; i < style->layers.size(); ++i)
            bandInfo->setProperty(juce::Identifier(style->layers[i]), engine_.isLayerEnabled(static_cast<int>(i)));
    const auto estimate = band_.keyEstimate();
    const char* keyStates[] = { "listening", "guessing", "confident", "locked" };
    bandInfo->setProperty("key", juce::String(band_.bandKey().name()));
    bandInfo->setProperty("keyEstimate", juce::String(estimate.best.name()));
    bandInfo->setProperty("keyState", keyStates[static_cast<int>(estimate.state)]);
    bandInfo->setProperty("singerTuningCents", std::round(estimate.tuningCents));
    bandInfo->setProperty("volumeDb", bandGainSlider_.getValue());
    bandInfo->setProperty("outputLatencySamples", engine_.outputLatencySamples());
    meta->setProperty("band", juce::var(bandInfo.get()));

    auto* settings = properties_.getUserSettings();
    meta->setProperty("measuredRoundTripMs", settings->containsKey(latencyMsKey) ? juce::var(settings->getDoubleValue(latencyMsKey)) : juce::var());
    meta->setProperty("droppedAnalysisFrames", static_cast<juce::int64>(engine_.droppedAnalysisFrames()));
    return meta;
}

void MainComponent::updateControls()
{
    const bool haveDevice = engine_.deviceInfo().sampleRate > 0.0;
    // Loading files pauses the audio callback, which would end a recording.
    loadButton_.setEnabled(!recording_);
    loadVoiceButton_.setEnabled(!recording_ && haveDevice);
    settingsButton_.setEnabled(!recording_);
    recordButton_.setEnabled(haveDevice || recording_);
    recordButton_.setButtonText(recording_ ? "Stop" : "Record");
    recordButton_.setColour(juce::TextButton::buttonColourId, recording_ ? juce::Colours::darkred : getLookAndFeel().findColour(juce::TextButton::buttonColourId));
    for (juce::Component* c : { static_cast<juce::Component*>(&bpmSlider_), static_cast<juce::Component*>(&meterBox_),
                                static_cast<juce::Component*>(&tempoBox_), static_cast<juce::Component*>(&clickToggle_) })
        c->setEnabled(!recording_);

    if (engine_.hasFileInput())
        voiceFileLabel_.setText(engine_.fileInputName() + (engine_.inputSource() == lb::AudioEngine::InputSource::file ? " (playing in a loop)" : ""),
                                juce::dontSendNotification);

    if (recording_)
        recordStatus_.setText("Recording " + formatDuration((juce::Time::getCurrentTime() - recordStartTime_).inSeconds()) + " to "
                                  + sessionFolder_.getFullPathName(),
                              juce::dontSendNotification);
}

//==================================================================================================
// Latency test

void MainComponent::confirmLatencyTest()
{
    if (latencyRunsLeft_ > 0 || engine_.latencyProbe().isBusy())
        return;

    if (deviceManager_.getCurrentAudioDevice() == nullptr)
    {
        latencySummary_ = "no audio device is open";
        updateReport();
        return;
    }

    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::InfoIcon)
                             .withTitle("Measure latency")
                             .withMessage("The app will play " + juce::String(latencyRunsPerTest)
                                          + " short chirps and listen for them.\n\n"
                                            "Headset: take it off and hold the mic against one ear cup.\n"
                                            "Audio interface: connect an output to an input with a cable.\n\n"
                                            "Do not wear the headset during the test, and keep the room quiet.")
                             .withButton("Start")
                             .withButton("Cancel")
                             .withAssociatedComponent(this);

    messageBox_ = juce::AlertWindow::showScopedAsync(options, [this](int result) {
        if (result != 1)
            return;

        band_.stopNow(); // the chirps need quiet
        latencyResultsMs_.clear();
        latencyFailedRuns_ = 0;
        latencyRunsLeft_ = latencyRunsPerTest;
        nextLatencyRunMs_ = juce::Time::getMillisecondCounterHiRes() + 500.0;
        latencySummary_ = "running...";
        updateReport();
    });
}

void MainComponent::stepLatencyTest()
{
    if (latencyRunsLeft_ <= 0)
        return;

    auto& probe = engine_.latencyProbe();
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (const auto result = probe.takeResult())
    {
        const double sampleRate = engine_.deviceInfo().sampleRate;
        if (result->lagSamples >= 0 && result->correlation >= 0.3f && sampleRate > 0.0)
            latencyResultsMs_.push_back(1000.0 * static_cast<double>(result->lagSamples) / sampleRate);
        else
            ++latencyFailedRuns_;

        nextLatencyRunMs_ = now + 300.0;
        if (--latencyRunsLeft_ == 0)
            finishLatencyTest();
        return;
    }

    if (probe.isBusy())
    {
        if (now > nextLatencyRunMs_ + 3000.0)
        {
            latencyRunsLeft_ = 0;
            latencySummary_ = "timed out (did the audio device stop?)";
        }
        return;
    }

    if (now >= nextLatencyRunMs_ && !probe.start())
    {
        latencyRunsLeft_ = 0;
        latencySummary_ = "could not start (is the audio device running?)";
    }
}

void MainComponent::finishLatencyTest()
{
    if (latencyResultsMs_.empty())
    {
        latencySummary_ = "failed: the chirps were not heard. Hold the mic closer to the ear cup and turn the volume up.";
        updateReport();
        return;
    }

    std::sort(latencyResultsMs_.begin(), latencyResultsMs_.end());
    const auto n = latencyResultsMs_.size();
    const double median = n % 2 == 1 ? latencyResultsMs_[n / 2]
                                     : 0.5 * (latencyResultsMs_[n / 2 - 1] + latencyResultsMs_[n / 2]);

    latencySummary_ = formatMs(median) + " (median of " + juce::String(static_cast<int>(n)) + ", range "
                      + juce::String(latencyResultsMs_.front(), 1) + " to " + juce::String(latencyResultsMs_.back(), 1) + " ms";
    if (latencyFailedRuns_ > 0)
        latencySummary_ << ", " << latencyFailedRuns_ << " not heard";
    latencySummary_ << ")";

    const auto info = engine_.deviceInfo();
    const auto setup = deviceManager_.getAudioDeviceSetup();
    auto* settings = properties_.getUserSettings();
    settings->setValue(latencyMsKey, median);
    settings->setValue(latencyDeviceKey, deviceManager_.getCurrentAudioDeviceType() + " / " + setup.outputDeviceName + " / "
                                             + juce::String(info.bufferSize) + " samples");
    settings->setValue(latencyDateKey, juce::Time::getCurrentTime().toISO8601(true));
    properties_.saveIfNeeded();
    applyLatencyCompensation();
    updateReport();
}

//==================================================================================================
// Test instrument

void MainComponent::chooseInstrument()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose an SFZ instrument", findAssetsDirectory().getChildFile("sfz"), "*.sfz");
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file.existsAsFile())
                                      loadInstrument(file);
                              });
}

void MainComponent::loadInstrument(const juce::File& sfzFile)
{
    testKeyDown_.fill(false);

    // Loading reads files, so the audio callback must not run meanwhile.
    deviceManager_.removeAudioCallback(&engine_);
    const bool ok = engine_.loadInstrument(sfzFile);
    deviceManager_.addAudioCallback(&engine_);

    if (!ok)
        showMessage("Could not load the instrument", sfzFile.getFullPathName());
    updateReport();
}

//==================================================================================================
// Report

void MainComponent::updateReport()
{
    const auto text = buildReport();
    if (text != report_.getText())
        report_.setText(text, false);
}

juce::String MainComponent::buildReport()
{
    juce::String r;
    const auto line = [&r](const juce::String& label, const juce::String& value) {
        r << label.paddedRight(' ', 30) << value << juce::newLine;
    };

    r << JUCE_APPLICATION_NAME_STRING << " " << JUCE_APPLICATION_VERSION_STRING << " - diagnostics" << juce::newLine << juce::newLine;

    if (deviceError_.isNotEmpty())
        line("Audio device error:", deviceError_);

    auto* device = deviceManager_.getCurrentAudioDevice();
    const auto setup = deviceManager_.getAudioDeviceSetup();
    const auto info = engine_.deviceInfo();

    if (device == nullptr)
    {
        line("Audio device:", "none open - choose one in Audio settings");
    }
    else
    {
        const double sampleRate = info.sampleRate > 0.0 ? info.sampleRate : device->getCurrentSampleRate();
        const auto toMs = [sampleRate](int samples) { return sampleRate > 0.0 ? 1000.0 * samples / sampleRate : 0.0; };

        line("Driver type:", deviceManager_.getCurrentAudioDeviceType());
        line("Output device:", setup.outputDeviceName);
        line("Input device:", setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : juce::String("none"));
        line("Sample rate:", juce::String(sampleRate, 0) + " Hz");
        line("Buffer:", juce::String(info.bufferSize) + " samples (" + formatMs(toMs(info.bufferSize)) + ")");
        line("Driver-reported latency:", "in " + juce::String(info.inputLatencySamples) + " + out "
                                             + juce::String(info.outputLatencySamples) + " samples = "
                                             + formatMs(toMs(info.inputLatencySamples + info.outputLatencySamples)));

        const int xruns = device->getXRunCount();
        line("Driver-reported dropouts:", xruns >= 0 ? juce::String(xruns) : juce::String("not reported by this driver"));
    }

    line("Measured round trip:", latencySummary_.isNotEmpty() ? latencySummary_ : juce::String("not measured yet"));

    auto* settings = properties_.getUserSettings();
    if (settings->containsKey(latencyMsKey))
        line("Saved measurement:", formatMs(settings->getDoubleValue(latencyMsKey)) + " with "
                                       + settings->getValue(latencyDeviceKey) + " on " + settings->getValue(latencyDateKey));

    r << juce::newLine;

    const auto stats = engine_.callbackStats().snapshot();
    const auto percent = [](float load) { return juce::String(100.0f * load, 1) + " %"; };
    line("Audio callback load:", "now " + percent(stats.lastLoad) + ", average " + percent(stats.averageLoad) + ", peak "
                                     + percent(stats.peakLoad));
    line("JUCE CPU estimate:", percent(static_cast<float>(deviceManager_.getCpuUsage())));
    line("Slow callbacks (>80 %):", juce::String(static_cast<juce::int64>(stats.slowCallbacks)));
    line("Overruns (>100 %):", juce::String(static_cast<juce::int64>(stats.overruns)));
    line("Callbacks:", juce::String(static_cast<juce::int64>(stats.callbacks)));
    line("Real-time allocations:", lb::rt::isGuardActive()
                                       ? juce::String(static_cast<juce::int64>(lb::rt::allocationCount()))
                                       : juce::String("not checked in Release builds"));

    r << juce::newLine;

    const bool fromFile = engine_.inputSource() == lb::AudioEngine::InputSource::file;
    line("Voice input:", fromFile ? "file " + engine_.fileInputName() : juce::String("microphone"));
    line("Analysis frames dropped:", juce::String(static_cast<juce::int64>(engine_.droppedAnalysisFrames())));
    if (recording_)
        line("Recording dropouts:", juce::String(static_cast<juce::int64>(engine_.recorder().droppedAudioBlocks())));
    const auto band = engine_.bandStatus();
    const char* states[] = { "stopped", "starting", "playing", "ending" };
    line("Band:", juce::String(states[static_cast<int>(band.state)]) + (band.bpm > 0.0 ? ", " + juce::String(band.bpm, 1) + " BPM" : juce::String()));
    line("Groove:", engine_.style() != nullptr ? juce::String(engine_.style()->name) : juce::String("not loaded"));
    line("Drum kit:", engine_.drumKitName().isEmpty() ? juce::String("not loaded")
                      : engine_.drumKitName() + (missingKitSamples_ > 0 ? " (" + juce::String(missingKitSamples_) + " samples missing)" : juce::String()));
    for (const auto part : { lb::AudioEngine::BandPart::bass, lb::AudioEngine::BandPart::keys, lb::AudioEngine::BandPart::pad })
    {
        auto name = engine_.bandInstrumentName(part);
        if (part == lb::AudioEngine::BandPart::keys && missingPianoSamples_ > 0)
            name << " (" << missingPianoSamples_ << " samples missing)";
        line(juce::String(lb::AudioEngine::partName(part)).toUpperCase().substring(0, 1) + juce::String(lb::AudioEngine::partName(part)).substring(1) + ":",
             name.isEmpty() ? juce::String("not loaded") : name);
    }
    {
        const auto estimate = band_.keyEstimate();
        const char* keyStates[] = { "listening", "guessing", "confident", "locked" };
        line("Key:", juce::String("band ") + band_.bandKey().name() + "; estimate " + estimate.best.name() + " (" + keyStates[static_cast<int>(estimate.state)]
                         + ", lead " + juce::String(estimate.margin, 3) + " of " + juce::String(estimate.requiredMargin, 3) + ", "
                         + juce::String(estimate.notes) + " notes, " + juce::String(estimate.voicedSeconds, 1) + " s of voice)");
        line("Singer's tuning:", juce::String(juce::roundToInt(estimate.tuningCents)) + " cents from A440");
    }
    if (info.sampleRate > 0.0)
        line("Tap timing allows for:", formatMs(1000.0 * engine_.outputLatencySamples() / info.sampleRate) + " of output latency");
    juce::StringArray midiNames;
    for (const auto& midiDevice : juce::MidiInput::getAvailableDevices())
        if (deviceManager_.isMidiInputDeviceEnabled(midiDevice.identifier))
            midiNames.add(midiDevice.name);
    line("MIDI inputs:", midiNames.isEmpty() ? juce::String("none") : midiNames.joinIntoString(", "));
    line("Test instrument:", engine_.instrumentName().isNotEmpty() ? engine_.instrumentName() : juce::String("not loaded"));
    line("Active voices:", juce::String(engine_.activeVoices()));
    if (engine_.droppedCommands() > 0)
        line("Dropped commands:", juce::String(static_cast<juce::int64>(engine_.droppedCommands())));

    return r;
}

void MainComponent::saveDeviceState()
{
    if (auto xml = deviceManager_.createStateXml())
    {
        properties_.getUserSettings()->setValue(audioDeviceKey, xml.get());
        properties_.saveIfNeeded();
    }
}

juce::File MainComponent::findAssetsDirectory()
{
#ifdef LB_ASSETS_DIR
    const juce::File sourceTreeAssets(LB_ASSETS_DIR);
    if (sourceTreeAssets.isDirectory())
        return sourceTreeAssets;
#endif
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getChildFile("assets");
}

juce::File MainComponent::sessionsDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LiveBand").getChildFile("Sessions");
}

//==================================================================================================
// Band

void MainComponent::loadBand()
{
    const auto assets = findAssetsDirectory();
    const auto kitFile = assets.getChildFile("sfz/afro-kit/afro-kit.sfz");
    if (!engine_.loadDrumKit(kitFile))
        bandProblem_ = "Could not load the drum kit " + kitFile.getFullPathName();
    missingKitSamples_ = countMissingSamples(kitFile.getParentDirectory());

    using Part = lb::AudioEngine::BandPart;
    const std::pair<Part, const char*> instruments[] = { { Part::bass, "sfz/bass/synth-bass.sfz" },
                                                         { Part::keys, "sfz/keys/piano.sfz" },
                                                         { Part::pad, "sfz/pad/warm-pad.sfz" } };
    for (const auto& [part, path] : instruments)
        if (!engine_.loadBandInstrument(part, assets.getChildFile(path)) && bandProblem_.isEmpty())
            bandProblem_ = "Could not load " + assets.getChildFile(path).getFullPathName();
    missingPianoSamples_ = countMissingSamples(assets.getChildFile("sfz/keys"));

    juce::String error;
    auto style = lb::loadStyle(assets.getChildFile("grooves/afrobeats"), error);
    if (style == nullptr)
    {
        bandProblem_ = "Could not load the groove: " + error;
        return;
    }
    engine_.setStyle(std::move(style));
    band_.styleChanged();
}

int MainComponent::countMissingSamples(const juce::File& kitFolder)
{
    const auto manifest = juce::JSON::parse(kitFolder.getChildFile("samples.json"));
    int missing = 0;
    if (auto* files = manifest.getProperty("files", juce::var()).getDynamicObject())
        for (const auto& entry : files->getProperties())
            if (!kitFolder.getChildFile(entry.name.toString()).existsAsFile())
                ++missing;
    return missing;
}

void MainComponent::setUpBandControls()
{
    auto* settings = properties_.getUserSettings();
    const auto* style = engine_.style();

    for (auto* button : { &startButton_, &tapButton_, &nextButton_, &fillButton_ })
    {
        button->setWantsKeyboardFocus(false);
        button->setEnabled(style != nullptr);
        addAndMakeVisible(button);
    }
    startButton_.setTooltip("Start with a one-bar count-in, or stop with a fill and a final hit (Enter).");
    startButton_.onClick = [this] { band_.startStop(); };
    tapButton_.setTooltip("Tap the beat: four taps start the band on the next one; while it plays, taps move it (Space).");
    tapButton_.setTriggeredOnMouseDown(true); // the tap is the press, not the release
    tapButton_.onClick = [this] { band_.tap(engine_.now()); };
    nextButton_.setTooltip("Play a fill, then the next section from the next bar (N).");
    nextButton_.onClick = [this] { band_.next(); };
    fillButton_.setTooltip("Play a fill to the end of the bar (F).");
    fillButton_.onClick = [this] { band_.fill(); };

    bandBpmSlider_.setSliderStyle(juce::Slider::IncDecButtons);
    bandBpmSlider_.setRange(60.0, 160.0, 1.0);
    bandBpmSlider_.setValue(band_.tempoBpm(), juce::dontSendNotification);
    bandBpmSlider_.setTextValueSuffix(" BPM");
    bandBpmSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 22);
    bandBpmSlider_.setTooltip("The band's tempo. Tapping sets it too.");
    bandBpmSlider_.setWantsKeyboardFocus(false);
    bandBpmSlider_.onValueChange = [this] { band_.setTempoBpm(bandBpmSlider_.getValue()); };
    addAndMakeVisible(bandBpmSlider_);

    if (style != nullptr)
        for (std::size_t i = 0; i < style->sections.size(); ++i)
            sectionBox_.addItem(juce::String(style->sections[i].name), static_cast<int>(i) + 1);
    sectionBox_.setSelectedId(1, juce::dontSendNotification);
    sectionBox_.setTooltip("The section the band starts with or goes to next.");
    sectionBox_.setWantsKeyboardFocus(false);
    sectionBox_.onChange = [this] { band_.selectSection(sectionBox_.getSelectedId() - 1); };
    addAndMakeVisible(sectionBox_);

    const auto setUpLayer = [&](juce::ToggleButton& toggle, const char* name, const char* key) {
        const int layer = style != nullptr ? style->layerIndex(name) : -1;
        toggle.setToggleState(settings->getBoolValue(key, true), juce::dontSendNotification);
        toggle.setWantsKeyboardFocus(false);
        toggle.setEnabled(layer >= 0);
        toggle.onClick = [this, &toggle, layer, key] {
            engine_.setLayerEnabled(layer, toggle.getToggleState());
            properties_.getUserSettings()->setValue(key, toggle.getToggleState());
        };
        if (layer >= 0)
            engine_.setLayerEnabled(layer, toggle.getToggleState());
        addAndMakeVisible(toggle);
    };
    setUpLayer(drumsToggle_, "drums", bandDrumsKey);
    setUpLayer(percToggle_, "perc", bandPercKey);
    setUpLayer(bassToggle_, "bass", bandBassKey);
    setUpLayer(keysToggle_, "keys", bandKeysKey);
    setUpLayer(padToggle_, "pad", bandPadKey);
    drumsToggle_.setTooltip("Kick, snare, hi-hat, toms and cymbals.");
    percToggle_.setTooltip("Shaker, congas, tambourine and bells.");
    bassToggle_.setTooltip("The synth bass, in the key.");
    keysToggle_.setTooltip("The piano, in the key.");
    padToggle_.setTooltip("The pad, in the key.");

    // Key: what the band follows, and the overrides.
    keyStatus_.setTooltip("The key the band plays in, and what the app hears in the singing.");
    lockButton_.setTooltip("Lock the key the app hears now; when locked, listen again (K). The band keeps its key until the new one locks.");
    lockButton_.onClick = [this] { band_.lockOrRelisten(); };
    keyDownButton_.setTooltip("The key a semitone down (Down arrow).");
    keyDownButton_.onClick = [this] { band_.shiftKey(-1); };
    keyUpButton_.setTooltip("The key a semitone up (Up arrow).");
    keyUpButton_.onClick = [this] { band_.shiftKey(1); };
    relativeButton_.setTooltip("Swap major and its relative minor, e.g. C major and A minor (M).");
    relativeButton_.onClick = [this] { band_.toggleRelativeKey(); };
    for (auto* button : { &lockButton_, &keyDownButton_, &keyUpButton_, &relativeButton_ })
    {
        button->setWantsKeyboardFocus(false);
        button->setEnabled(style != nullptr);
        addAndMakeVisible(button);
    }
    addAndMakeVisible(keyLabel_);
    addAndMakeVisible(keyStatus_);

    bandGainSlider_.setRange(-30.0, 6.0, 0.5);
    bandGainSlider_.setValue(settings->getDoubleValue(bandVolumeKey, 0.0), juce::dontSendNotification);
    bandGainSlider_.setTextValueSuffix(" dB");
    bandGainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
    bandGainSlider_.setWantsKeyboardFocus(false);
    bandGainSlider_.onValueChange = [this] {
        const float gain = juce::Decibels::decibelsToGain(static_cast<float>(bandGainSlider_.getValue()));
        for (int part = 0; part < lb::AudioEngine::numBandParts; ++part)
            engine_.setPartGain(static_cast<lb::AudioEngine::BandPart>(part), gain);
        properties_.getUserSettings()->setValue(bandVolumeKey, bandGainSlider_.getValue());
    };
    bandGainSlider_.onValueChange();
    addAndMakeVisible(bandGainSlider_);

    bandGainLabel_.setJustificationType(juce::Justification::centredRight);
    for (auto* label : { &bandLabel_, &bandGainLabel_ })
        addAndMakeVisible(label);
    if (style != nullptr)
        beatLamps_.setBeatsPerBar(style->beatsPerBar);
    addAndMakeVisible(beatLamps_);
}

bool MainComponent::pressBandKey(BandKey& key)
{
    return !std::exchange(key.held, true); // false for the auto-repeats of a held key
}

void MainComponent::updateBand()
{
    const auto status = engine_.bandStatus();
    const double rate = engine_.deviceInfo().sampleRate;

    // Light the beat being heard now: sent to the output one output latency ago.
    double now = 0.0;
    double secondsSinceBeat = 0.0;
    if (rate > 0.0 && engine_.sampleAtTime(engine_.now(), now))
    {
        const double heard = now - engine_.outputLatencySamples();
        while (!pendingTicks_.empty() && static_cast<double>(pendingTicks_.front().sampleTime) <= heard)
        {
            heardTick_ = pendingTicks_.front();
            pendingTicks_.pop_front();
        }
        if (heardTick_)
        {
            secondsSinceBeat = (heard - static_cast<double>(heardTick_->sampleTime)) / rate;
            const double beatSeconds = heardTick_->bpm > 0.0f ? 60.0 / heardTick_->bpm : 1.0;
            if (status.state == lb::BandState::stopped && pendingTicks_.empty() && secondsSinceBeat > beatSeconds)
                heardTick_.reset();
        }
    }
    beatLamps_.setBeat(heardTick_ ? &*heardTick_ : nullptr, secondsSinceBeat);
    beatLamps_.setStatus(bandStatusText(status));
    keyStatus_.setText(keyStatusText(status), juce::dontSendNotification);
    const bool locked = band_.keyEstimate().state == lb::KeyEstimator::State::locked;
    lockButton_.setButtonText(locked ? "Listen again" : "Lock");
    relativeButton_.setButtonText(band_.bandKey().isValid() && band_.bandKey().minor ? "Relative major" : "Relative minor");

    const bool running = status.state != lb::BandState::stopped;
    startButton_.setButtonText(running ? "Stop" : "Start");
    if (!bandBpmSlider_.isMouseButtonDown())
    {
        const double bpm = running && status.bpm > 0.0 ? status.bpm : band_.tempoBpm();
        if (std::abs(bandBpmSlider_.getValue() - bpm) >= 0.5)
            bandBpmSlider_.setValue(std::round(bpm), juce::dontSendNotification);
    }
    const int shownSection = status.pendingSection >= 0 ? status.pendingSection : status.section;
    if (!sectionBox_.isPopupActive() && sectionBox_.getSelectedId() != shownSection + 1)
        sectionBox_.setSelectedId(shownSection + 1, juce::dontSendNotification);
}

juce::String MainComponent::bandStatusText(const lb::AudioEngine::BandStatus& status) const
{
    if (bandProblem_.isNotEmpty())
        return bandProblem_;
    const auto* style = engine_.style();
    if (style == nullptr)
        return "No groove loaded.";
    if (engine_.deviceInfo().sampleRate <= 0.0)
        return "Open an audio device in Audio settings to play.";

    const auto sectionName = [&](int index) {
        return index >= 0 && index < static_cast<int>(style->sections.size()) ? juce::String(style->sections[static_cast<std::size_t>(index)].name)
                                                                               : juce::String();
    };
    switch (status.state)
    {
        case lb::BandState::stopped:
        {
            const int taps = band_.tapCount();
            if (taps > 0)
                return "Tap " + juce::String(taps) + " of " + juce::String(lb::BandController::tapsToStart) + "...";
            if (missingKitSamples_ > 0)
                return "Only the kick is installed: run tools/python/fetch_assets.py for the rest of the kit.";
            if (missingPianoSamples_ > 0)
                return "The piano's samples are not installed: run tools/python/fetch_assets.py.";
            return "Ready. Tap the beat four times (Space), or press Enter for a count-in.";
        }
        case lb::BandState::waiting:
            return "Coming in...";
        case lb::BandState::ending:
            return "Ending";
        case lb::BandState::playing:
            break;
    }

    if (heardTick_ && (heardTick_->flags & lb::BeatTick::countIn) != 0)
        return "Count-in, " + juce::String(juce::roundToInt(status.bpm)) + " BPM";
    juce::String text = sectionName(status.section);
    if (heardTick_ && heardTick_->bar >= 0)
        text << ", bar " << (heardTick_->bar + 1);
    if (status.pendingSection >= 0)
        text << "  -  fill, then " << sectionName(status.pendingSection);
    else if (status.fillScheduled)
        text << "  -  fill";
    text << "  -  " << juce::String(status.bpm, 1) << " BPM";
    return text;
}

juce::String MainComponent::keyStatusText(const lb::AudioEngine::BandStatus& status) const
{
    using State = lb::KeyEstimator::State;
    const auto estimate = band_.keyEstimate();
    const auto percent = [](double share) { return juce::String(juce::roundToInt(100.0 * std::clamp(share, 0.0, 1.0))) + " %"; };
    const auto tuning = [&] {
        const int cents = juce::roundToInt(estimate.tuningCents);
        return estimate.notes > 0 && std::abs(cents) >= 5 ? "  -  singer " + juce::String(cents > 0 ? "+" : "") + juce::String(cents) + " cents"
                                                           : juce::String();
    };

    juce::String text;
    const auto band = band_.bandKey();
    switch (estimate.state)
    {
        case State::listening:
            text = band.isValid() ? juce::String(band.name()) + "  -  listening again" : juce::String("Listening: sing a phrase or two");
            break;
        case State::guessing:
        case State::confident:
            text = band.isValid() ? juce::String(band.name()) + "  -  now hearing " : juce::String("Hearing ");
            text << estimate.best.name() << " (" << percent(estimate.confidence) << " sure)";
            break;
        case State::locked:
            text = juce::String(estimate.key.name()) + "  -  locked";
            if (estimate.best.isValid() && estimate.best != estimate.key)
                text << ", hearing " << estimate.best.name();
            break;
    }
    // The band changes key on its next downbeat.
    if (status.state == lb::BandState::playing && band.isValid() && status.key.isValid() && status.key != band)
        text << "  -  " << band.name() << " from the next bar";
    return text + tuning();
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    // MIDI thread. The timestamp is on the same clock as AudioEngine::now(), so the tap keeps
    // its timing however long the message thread takes to get to it.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this), message] {
        if (safe != nullptr)
            safe->band_.handleMidi(message);
    });
}

void MainComponent::enableMidiInputs()
{
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (!deviceManager_.isMidiInputDeviceEnabled(device.identifier))
            deviceManager_.setMidiInputDeviceEnabled(device.identifier, true);
}

void MainComponent::applyLatencyCompensation()
{
    // A saved round-trip measurement applies only to the driver, device and buffer it was made with.
    auto* settings = properties_.getUserSettings();
    const auto info = engine_.deviceInfo();
    const auto setup = deviceManager_.getAudioDeviceSetup();
    const auto current = deviceManager_.getCurrentAudioDeviceType() + " / " + setup.outputDeviceName + " / " + juce::String(info.bufferSize) + " samples";
    const bool applies = info.sampleRate > 0.0 && settings->containsKey(latencyMsKey) && settings->getValue(latencyDeviceKey) == current;
    engine_.setMeasuredRoundTripLatency(applies ? juce::roundToInt(settings->getDoubleValue(latencyMsKey) * info.sampleRate / 1000.0) : 0);
}
