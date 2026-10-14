#pragma once

#include "engine/Instrument.h"

#include <memory>
#include <string>

namespace sfz
{
class Sfizz;
}

namespace lb
{

// One SFZ instrument played by sfizz.
class SfizzInstrument final : public Instrument
{
public:
    SfizzInstrument();
    ~SfizzInstrument() override;

    // Not real-time safe: reads files. Call while the audio callback is stopped.
    bool load(const std::string& sfzPath);
    bool isLoaded() const noexcept { return loaded_; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void noteOn(int sampleOffset, int note, int velocity) noexcept override;
    void noteOff(int sampleOffset, int note) noexcept override;
    void allNotesOff() noexcept override;
    void render(float* left, float* right, int numSamples) noexcept override;

    int activeVoices() const noexcept;

private:
    std::unique_ptr<sfz::Sfizz> synth_;
    bool loaded_ = false;
};

} // namespace lb
