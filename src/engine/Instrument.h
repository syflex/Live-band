#pragma once

namespace lb
{

// A sound source driven by note events. SfizzInstrument is the only implementation today;
// the interface exists so a simpler built-in sampler can replace sfizz if it ever has to.
class Instrument
{
public:
    virtual ~Instrument() = default;

    // Not real-time safe. Called while the audio callback is stopped.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Audio thread. sampleOffset is the position of the event inside the next render() block.
    virtual void noteOn(int sampleOffset, int note, int velocity) noexcept = 0;
    virtual void noteOff(int sampleOffset, int note) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;

    // Audio thread. Overwrites both channels; numSamples must not exceed maxBlockSize.
    virtual void render(float* left, float* right, int numSamples) noexcept = 0;
};

} // namespace lb
