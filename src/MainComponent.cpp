#include "MainComponent.h"

#include "common/RtAllocGuard.h"
#include "engine/AudioDeviceSetup.h"

#include <algorithm>
#include <utility>

namespace
{
constexpr auto audioDeviceKey = "audioDevice";
constexpr auto latencyMsKey = "latency.roundTripMs";
constexpr auto latencyDeviceKey = "latency.device";
constexpr auto latencyDateKey = "latency.measuredAt";

constexpr float meterFloorDb = -60.0f;
constexpr float meterDecayDbPerTick = 1.5f; // at 30 Hz, about 45 dB per second

juce::String formatMs(double ms)
{
    return juce::String(ms, 1) + " ms";
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

    for (auto* button : { &settingsButton_, &latencyButton_, &loadButton_, &resetButton_, &copyButton_ })
    {
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(button);
    }

    settingsButton_.onClick = [this] { showAudioSettings(); };
    latencyButton_.onClick = [this] { confirmLatencyTest(); };
    loadButton_.onClick = [this] { chooseInstrument(); };
    resetButton_.onClick = [this] { engine_.callbackStats().resetPeak(); };
    copyButton_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(buildReport()); };

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

    hint_.setText("Keys A S D F G H J K play test notes (Esc stops them). "
                  "Only turn on \"Hear my voice\" while wearing headphones, or it will howl.",
                  juce::dontSendNotification);
    hint_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hint_);

    report_.setMultiLine(true);
    report_.setReadOnly(true);
    report_.setCaretVisible(false);
    report_.setWantsKeyboardFocus(false);
    report_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
    addAndMakeVisible(report_);

    setWantsKeyboardFocus(true);
    setSize(780, 600);
    updateReport();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(&engine_);
    properties_.saveIfNeeded();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    auto area = meterArea_;
    drawMeter(g, area.removeFromTop(area.getHeight() / 2), "Mic", inputLevelDb_);
    drawMeter(g, area, "Out", outputLevelDb_);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto row = area.removeFromTop(30);
    settingsButton_.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(16);
    monitorToggle_.setBounds(row.removeFromLeft(130));
    monitorGainSlider_.setBounds(row.removeFromLeft(260));
    area.removeFromTop(8);

    row = area.removeFromTop(30);
    for (auto* button : { &latencyButton_, &loadButton_, &resetButton_, &copyButton_ })
    {
        button->setBounds(row.removeFromLeft(140));
        row.removeFromLeft(8);
    }
    area.removeFromTop(8);

    meterArea_ = area.removeFromTop(56);
    area.removeFromTop(4);
    hint_.setBounds(area.removeFromTop(40));
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

    stepLatencyTest();

    if (--ticksUntilReport_ <= 0)
    {
        ticksUntilReport_ = 10;
        updateReport();
    }
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // The device or its settings changed.
    deviceError_.clear();
    saveDeviceState();
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
        messageBox_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                             .withTitle("Could not load the instrument")
                                                             .withMessage(sfzFile.getFullPathName())
                                                             .withButton("OK")
                                                             .withAssociatedComponent(this),
                                                         nullptr);
    updateReport();
}

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

    r << JUCE_APPLICATION_NAME_STRING << " " << JUCE_APPLICATION_VERSION_STRING << " - Phase 0 diagnostics" << juce::newLine
      << juce::newLine;

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

    line("Test instrument:", engine_.instrumentName().isNotEmpty() ? engine_.instrumentName() : juce::String("not loaded"));
    line("Active voices:", juce::String(engine_.activeVoices()));
    if (engine_.droppedNoteEvents() > 0)
        line("Dropped note events:", juce::String(static_cast<juce::int64>(engine_.droppedNoteEvents())));

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
