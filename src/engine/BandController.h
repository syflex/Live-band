#pragma once

#include "engine/AudioEngine.h"
#include "music/TapTempo.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace lb
{

// The band leader's controls: tap tempo, start and stop, sections and fills, from the
// keyboard, the mouse or MIDI. Turns them into AudioEngine commands.
//
// Starting by tapping: four steady taps, and the band comes in on the fifth, on the beat the
// taps predict, with the output latency taken off so that the downbeat is heard when the
// fifth tap lands. Tapping while the band plays (four taps or more) pulls it onto the
// tapped tempo and beat, gradually.
//
// MIDI: any note-on, or the sustain pedal (CC 64) or CC 80 going down, is a tap; CC 81
// starts and stops, CC 82 goes to the next section, CC 83 plays a fill.
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

    // Taps in the current run, for "tap 2 of 4". Zero after a pause.
    int tapCount() const;

    // Returns true if the message was a band control.
    bool handleMidi(const juce::MidiMessage& message);

    // Call after AudioEngine::setStyle(): the start tempo becomes the style's.
    void styleChanged();

private:
    double samplesPerBeat(double bpm) const;

    AudioEngine& engine_;
    TapTempo taps_;
    double bpm_ = 100.0;
    bool pedalDown_ = false;
};

} // namespace lb
