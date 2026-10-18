#pragma once

#include "audio/AnalysisFrame.h"
#include "engine/AudioEngine.h"
#include "music/KeyEstimator.h"
#include "music/TapTempo.h"
#include "music/VocalDynamics.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace lb
{

// The band leader's controls: tap tempo, start and stop, sections and fills, from the
// keyboard, the mouse or MIDI. Turns them into AudioEngine commands.
//
// Starting by tapping: four steady taps, and the band comes in on the fifth, on the beat the
// taps predict, with the output latency taken off so that the downbeat is heard when the
// fifth tap lands. Tapping while the band plays (four taps or more) pulls it onto the
// tapped tempo and beat within a bar, by playing that bar a little faster or slower.
// The nudge keys change the tempo by 1% at a time.
//
// Key: the controller listens to the singer through the analysis frames, and the key the
// estimator locks goes to the band, which changes key on its next downbeat. A band started
// before that plays the drums alone and brings the harmony in once the key is known; a band
// started after it comes in with the style's staged entry. By hand: lock the current guess,
// or listen afresh (the band keeps its key until the new one locks); move the key a semitone;
// swap major and its relative minor. Once the band has stopped, the estimator listens afresh
// for the next song.
//
// Dynamics: the same frames tell how loud the singer is against their own range; the band
// plays harder or softer, and turns intense after two bars of loud singing (VocalDynamics).
//
// MIDI: any note-on, or the sustain pedal (CC 64) or CC 80 going down, is a tap; CC 81
// starts and stops, CC 82 goes to the next section, CC 83 plays a fill, CC 84 and CC 85
// nudge the tempo down and up.
//
// Message thread only.
class BandController
{
public:
    static constexpr int tapsToStart = 4;

    enum class TapResult
    {
        ignored,  // no style, no audio, or the band is starting or ending
        counting, // not enough taps yet
        started,
        steered
    };

    explicit BandController(AudioEngine& engine) : engine_(engine) {}

    // A tap at `seconds` on the engine's clock (AudioEngine::now(), or a MIDI timestamp).
    TapResult tap(double seconds);
    void startStop(); // a one-bar count-in at the current tempo, or a graceful stop
    void stopNow();
    void next();
    void fill();
    void selectSection(int section);

    // The tempo the band starts at without tapping, and plays at if it is running.
    void setTempoBpm(double bpm);
    double tempoBpm() const noexcept { return bpm_; }
    static constexpr double nudgeRatio = 0.01;
    void nudgeTempo(int steps); // +1: 1% faster, -1: back down by the same ratio

    // Taps in the current run, for "tap 2 of 4". Zero after a pause.
    int tapCount() const;

    // Returns true if the message was a band control.
    bool handleMidi(const juce::MidiMessage& message);

    // Call after AudioEngine::setStyle(): the start tempo becomes the style's.
    void styleChanged();

    // Key. analysisFrame() takes each frame from AudioEngine::popAnalysisFrame(); update()
    // is called regularly (the UI timer) to notice the band stopping.
    void analysisFrame(const AnalysisFrame& frame);
    void update();
    KeyEstimator::Estimate keyEstimate() const noexcept { return keys_.estimate(); }
    Key bandKey() const noexcept { return sentKey_; } // the last key given to the band
    void lockOrRelisten();         // lock the current guess, or, when locked, listen afresh
    void shiftKey(int semitones);  // the current key or guess, moved, and locked
    void toggleRelativeKey();      // C major <-> A minor, locked
    void setKey(const Key& key);   // a key by hand, locked
    void setKeySettings(const KeyEstimator::Settings& settings) { keys_.setSettings(settings); }

    // Following the singer's loudness; off, the band plays at its written velocities.
    void setDynamicsEnabled(bool enabled);
    bool dynamicsEnabled() const noexcept { return dynamicsEnabled_; }
    const VocalDynamics& dynamics() const noexcept { return dynamics_; }

private:
    double samplesPerBeat(double bpm) const;
    Key currentKey() const noexcept; // the locked key, or the best guess, or none
    void sendKeyIfLocked();
    void sendDynamics(bool force);

    AudioEngine& engine_;
    TapTempo taps_;
    KeyEstimator keys_;
    Key sentKey_;
    VocalDynamics dynamics_;
    bool dynamicsEnabled_ = true;
    float sentScale_ = 1.0f;
    bool sentIntense_ = false;
    BandState lastState_ = BandState::stopped;
    double bpm_ = 100.0;
    bool pedalDown_ = false;
};

} // namespace lb
