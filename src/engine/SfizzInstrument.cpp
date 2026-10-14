#include "engine/SfizzInstrument.h"

#include <sfizz.hpp>

namespace lb
{
namespace
{
constexpr int voicesPerInstrument = 64;
}

SfizzInstrument::SfizzInstrument()
    : synth_(std::make_unique<sfz::Sfizz>())
{
    synth_->setNumVoices(voicesPerInstrument);
}

SfizzInstrument::~SfizzInstrument() = default;

bool SfizzInstrument::load(const std::string& sfzPath)
{
    loaded_ = synth_->loadSfzFile(sfzPath);
    return loaded_;
}

void SfizzInstrument::prepare(double sampleRate, int maxBlockSize)
{
    synth_->setSampleRate(static_cast<float>(sampleRate));
    synth_->setSamplesPerBlock(maxBlockSize);
}

void SfizzInstrument::noteOn(int sampleOffset, int note, int velocity) noexcept
{
    synth_->noteOn(sampleOffset, note, velocity);
}

void SfizzInstrument::noteOff(int sampleOffset, int note) noexcept
{
    synth_->noteOff(sampleOffset, note, 0);
}

void SfizzInstrument::allNotesOff() noexcept
{
    synth_->allSoundOff();
}

void SfizzInstrument::render(float* left, float* right, int numSamples) noexcept
{
    float* channels[] = { left, right };
    synth_->renderBlock(channels, static_cast<size_t>(numSamples), 1);
}

int SfizzInstrument::activeVoices() const noexcept
{
    return synth_->getNumActiveVoices();
}

} // namespace lb
