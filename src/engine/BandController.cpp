#include "engine/BandController.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lb
{
namespace
{
constexpr double startLeadSeconds = 0.1; // time for the command to reach the audio thread
constexpr double tapRunTimeoutSeconds = 2.0;
} // namespace

double BandController::samplesPerBeat(double bpm) const
{
    return 60.0 * engine_.deviceInfo().sampleRate / bpm;
}

void BandController::styleChanged()
{
    if (const auto* style = engine_.style())
        bpm_ = style->defaultBpm;
    taps_.reset();
}

BandController::TapResult BandController::tap(double seconds)
{
    const auto* style = engine_.style();
    if (style == nullptr || engine_.deviceInfo().sampleRate <= 0.0)
        return TapResult::ignored;

    taps_.tap(seconds);
    const auto estimate = taps_.estimate();
    if (estimate.taps < tapsToStart)
        return TapResult::counting;

    const double bpm = style->foldTempo(estimate.bpm());
    const double spb = samplesPerBeat(bpm);
    const auto status = engine_.bandStatus();
    const int latency = engine_.outputLatencySamples();

    if (status.state == BandState::stopped)
    {
        double downbeat = 0.0;
        if (!engine_.sampleAtTime(estimate.timeOfTap(estimate.taps), downbeat))
            return TapResult::ignored;
        bpm_ = bpm;
        engine_.startBand(std::llround(downbeat) - latency, spb, status.section, 0, sentKey_);
        return TapResult::started;
    }
    if (status.state == BandState::playing)
    {
        double anchor = 0.0;
        if (!engine_.sampleAtTime(estimate.timeOfTap(estimate.taps - 1), anchor))
            return TapResult::ignored;
        bpm_ = bpm;
        engine_.steerBand(std::llround(anchor) - latency, spb);
        return TapResult::steered;
    }
    return TapResult::ignored;
}

void BandController::startStop()
{
    const auto status = engine_.bandStatus();
    if (status.state == BandState::playing || status.state == BandState::waiting)
    {
        engine_.stopBand();
        return;
    }
    if (status.state == BandState::ending || engine_.style() == nullptr)
        return;

    double now = 0.0;
    if (!engine_.sampleAtTime(engine_.now(), now))
        return;
    const double spb = samplesPerBeat(bpm_);
    const double rate = engine_.deviceInfo().sampleRate;
    const double downbeat = now + startLeadSeconds * rate + engine_.style()->beatsPerBar * spb;
    taps_.reset();
    engine_.startBand(std::llround(downbeat), spb, status.section, 1, sentKey_);
}

void BandController::stopNow()
{
    taps_.reset();
    engine_.stopBandNow();
}

void BandController::next()
{
    engine_.nextSection();
}

void BandController::fill()
{
    engine_.fill();
}

void BandController::selectSection(int section)
{
    engine_.selectSection(section);
}

void BandController::setTempoBpm(double bpm)
{
    if (!(bpm > 0.0) || engine_.deviceInfo().sampleRate <= 0.0)
        return;
    bpm_ = bpm;
    engine_.setBandTempo(samplesPerBeat(bpm));
}

void BandController::nudgeTempo(int steps)
{
    // From the latest tempo asked for (taps update it too), so quick nudges add up before
    // the audio thread has caught up; a step down and a step up cancel out.
    setTempoBpm(std::clamp(bpm_ * std::pow(1.0 + nudgeRatio, steps), 30.0, 300.0));
}

int BandController::tapCount() const
{
    const auto estimate = taps_.estimate();
    if (estimate.taps == 0 || engine_.now() - estimate.lastTap > tapRunTimeoutSeconds)
        return 0;
    return estimate.taps;
}

void BandController::analysisFrame(const AnalysisFrame& frame)
{
    const double rate = engine_.deviceInfo().sampleRate;
    if (rate <= 0.0)
        return;
    keys_.process(static_cast<double>(frame.sampleTime) / rate, frame.isVoiced(), frame.midiPitch, frame.rmsDb);
    sendKeyIfLocked();
}

void BandController::update()
{
    const auto state = engine_.bandStatus().state;
    if (state == BandState::stopped && lastState_ != BandState::stopped)
    {
        // The song is over: the next one may be in another key.
        keys_.reset();
        sentKey_ = {};
    }
    lastState_ = state;
}

Key BandController::currentKey() const noexcept
{
    const auto estimate = keys_.estimate();
    if (estimate.state == KeyEstimator::State::locked)
        return estimate.key;
    return estimate.state == KeyEstimator::State::listening ? sentKey_ : estimate.best;
}

void BandController::sendKeyIfLocked()
{
    if (!keys_.isLocked())
        return;
    const Key key = keys_.estimate().key;
    if (key.isValid() && key != sentKey_)
    {
        sentKey_ = key;
        engine_.setBandKey(key);
    }
}

void BandController::lockOrRelisten()
{
    if (keys_.isLocked())
    {
        keys_.reset(); // the band keeps sentKey_ until the estimator locks again
        return;
    }
    if (const auto key = currentKey(); key.isValid())
        setKey(key);
}

void BandController::shiftKey(int semitones)
{
    if (const auto key = currentKey(); key.isValid())
        setKey(key.transposed(semitones));
}

void BandController::toggleRelativeKey()
{
    if (const auto key = currentKey(); key.isValid())
        setKey(key.relative());
}

void BandController::setKey(const Key& key)
{
    if (!key.isValid())
        return;
    keys_.lock(key);
    sendKeyIfLocked();
}

bool BandController::handleMidi(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        tap(message.getTimeStamp());
        return true;
    }
    if (!message.isController())
        return false;

    const bool down = message.getControllerValue() >= 64;
    switch (message.getControllerNumber())
    {
        case 64: // sustain pedal: a tap when it goes down
        {
            const bool wasDown = std::exchange(pedalDown_, down);
            if (down && !wasDown)
                tap(message.getTimeStamp());
            return true;
        }
        case 80:
            if (down)
                tap(message.getTimeStamp());
            return true;
        case 81:
            if (down)
                startStop();
            return true;
        case 82:
            if (down)
                next();
            return true;
        case 83:
            if (down)
                fill();
            return true;
        case 84:
        case 85:
            if (down)
                nudgeTempo(message.getControllerNumber() == 85 ? 1 : -1);
            return true;
        default:
            return false;
    }
}

} // namespace lb
