#include "MainComponent.h"

#include "common/RtAllocGuard.h"
#include "engine/AudioDeviceSetup.h"
#include "engine/AudioFileLoader.h"

#include <algorithm>
#include <utility>

namespace
{
constexpr auto audioDeviceKey = "audioDevice";
constexpr auto latencyMsKey = "latency.roundTripMs";
constexpr auto latencyDeviceKey = "latency.device";
constexpr auto latencyDateKey = "latency.measuredAt";
constexpr auto voiceFolderKey = "voiceFile.folder";

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

    // Load the test instrument before audio starts; loading is not real-time safe.
    engine_.loadInstrument(findAssetsDirectory().getChildFile("sfz/test/sine.sfz"));

    const auto savedDevice = properties_.getUserSettings()->getXmlValue(audioDeviceKey);
    deviceError_ = lb::openAudioDevice(deviceManager_, savedDevice.get());
    deviceManager_.addAudioCallback(&engine_);
    deviceManager_.addChangeListener(this);
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

    hint_.setText("Keys A S D F G H J K play test notes (Esc stops them). Drop a WAV file here to use it as the voice. "
                  "Only turn on \"Hear my voice\" while wearing headphones, or it will howl.",
                  juce::dontSendNotification);
    hint_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hint_);

    report_.setMultiLine(true);
    report_.setReadOnly(true);
    report_.setCaretVisible(false);
    report_.setWantsKeyboardFocus(false);
    report_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    addAndMakeVisible(report_);

    setWantsKeyboardFocus(true);
    setSize(960, 820);
    updateControls();
    updateReport();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (recording_)
        stopRecording({});
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
    area.removeFromTop(8);

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

    hint_.setBounds(area.removeFromTop(30));
    area.removeFromTop(4);
    report_.setBounds(area);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        engine_.allNotesOff();
        testKeyDown_.fill(false);
        return true;
    }

    // Notes start and stop in keyStateChanged(); swallow the presses (and auto-repeats) here.
    return std::any_of(testKeys.begin(), testKeys.end(), [&](const TestKey& k) {
        return juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode())) == k.keyCode;
    });
}

bool MainComponent::keyStateChanged(bool)
{
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
