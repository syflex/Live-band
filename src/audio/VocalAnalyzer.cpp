#include "audio/VocalAnalyzer.h"

#include "common/Pitch.h"

#include <algorithm>
#include <cmath>

namespace lb
{

void VocalAnalyzer::prepare(double inputSampleRate, const Settings& settings)
{
    settings_ = settings;
    inputRate_ = inputSampleRate;

    const int factor = std::max(1, static_cast<int>(std::lround(inputSampleRate / settings.targetAnalysisRate)));
    decimator_.prepare(factor);
    analysisRate_ = inputSampleRate / factor;
    hop_ = std::max(1, static_cast<int>(std::lround(settings.hopSeconds * analysisRate_)));

    yin_.prepare(analysisRate_, settings.pitch);
    onsetDetector_.prepare(analysisRate_, settings.fftSize, hop_, settings.onsets);
    highPass_.setHighPass(analysisRate_, settings.highPassHz);

    // Both detectors look at windows centred on the same sample of the history.
    historyLength_ = std::max(yin_.requiredInput(), settings.fftSize);
    const int centre = historyLength_ / 2;
    fftOffset_ = centre - settings.fftSize / 2;
    yinOffset_ = std::max(0, centre - yin_.requiredInput() / 2);
    yinOffset_ = std::min(yinOffset_, historyLength_ - yin_.requiredInput());

    centreDelayInput_ = decimator_.groupDelay() + static_cast<std::int64_t>(historyLength_ - 1 - centre) * factor;
    hopInput_ = static_cast<std::int64_t>(hop_) * factor;
    minInterOnsetInput_ = static_cast<std::int64_t>(std::lround(settings.onsets.minInterOnsetSeconds * inputSampleRate));

    history_.assign(static_cast<std::size_t>(historyLength_), 0.0f);
    pending_.assign(static_cast<std::size_t>(hop_), 0.0f);
    recentMidi_.assign(static_cast<std::size_t>(std::max(1, settings.medianFrames)), 0.0f);
    medianScratch_.assign(recentMidi_.size(), 0.0f);

    reset();
}

void VocalAnalyzer::reset(std::int64_t startSampleTime) noexcept
{
    decimator_.reset();
    highPass_.reset();
    onsetDetector_.reset();
    std::fill(history_.begin(), history_.end(), 0.0f);
    pendingCount_ = 0;

    startSampleTime_ = startSampleTime;
    inputSamplesSeen_ = 0;

    voiced_ = false;
    std::fill(recentMidi_.begin(), recentMidi_.end(), 0.0f);
    recentWrite_ = 0;

    inNote_ = false;
    noteMidi_ = 0.0f;
    voicedRun_ = 0;
    unvoicedRun_ = 0;
    changeRun_ = 0;
    runStartTime_ = 0;
    changeStartTime_ = 0;

    previousRmsDb_ = -120.0f;
    lastOnsetTime_ = 0;
    anyOnset_ = false;
}

AnalysisFrame VocalAnalyzer::analyseFrame() noexcept
{
    // Slide the new hop into the history.
    std::move(history_.begin() + hop_, history_.end(), history_.begin());
    std::copy(pending_.begin(), pending_.end(), history_.end() - hop_);

    AnalysisFrame frame;
    const std::int64_t newestInput = startSampleTime_ + inputSamplesSeen_ - 1;
    frame.sampleTime = newestInput - centreDelayInput_;

    // Level of the region both detectors look at.
    const float* centreRegion = history_.data() + fftOffset_;
    double energy = 0.0;
    for (int i = 0; i < settings_.fftSize; ++i)
        energy += static_cast<double>(centreRegion[i]) * centreRegion[i];
    const double rms = std::sqrt(energy / settings_.fftSize);
    frame.rmsDb = rms > 1e-6 ? static_cast<float>(20.0 * std::log10(rms)) : -120.0f;

    // Pitch, with hysteresis on the voicing decision.
    const PitchEstimate estimate = yin_.estimate(history_.data() + yinOffset_);
    const bool loudEnough = frame.rmsDb > settings_.gateDb;
    const float cmnd = 1.0f - estimate.periodicity;
    voiced_ = loudEnough && (estimate.confident || (voiced_ && cmnd < settings_.voicingHoldCmnd));

    frame.periodicity = estimate.periodicity;
    if (voiced_)
    {
        frame.f0Hz = estimate.frequencyHz;
        frame.flags |= AnalysisFrame::voiced;
    }
    frame.midiPitch = smoothedMidi(voiced_ ? frequencyToMidi(estimate.frequencyHz) : 0.0f, voiced_);

    // Spectral onsets. A detected peak belongs to the previous frame.
    const auto onset = onsetDetector_.process(centreRegion);
    frame.odf = onset.odf;
    if (onset.onset && std::max(previousRmsDb_, frame.rmsDb) > settings_.gateDb)
        reportOnset(frame, frame.sampleTime - hopInput_, onset.strength, AnalysisFrame::energyOnset);

    // Pitch onsets: the voice starting, or a held change to a new note.
    if (voiced_)
    {
        unvoicedRun_ = 0;
        if (voicedRun_++ == 0)
            runStartTime_ = frame.sampleTime;

        if (!inNote_)
        {
            if (voicedRun_ >= settings_.noteChangeFrames)
            {
                inNote_ = true;
                noteMidi_ = frame.midiPitch;
                changeRun_ = 0;
                reportOnset(frame, runStartTime_, std::max(0.01f, frame.periodicity), AnalysisFrame::pitchOnset);
            }
        }
        else
        {
            const float difference = frame.midiPitch - noteMidi_;
            if (std::abs(difference) > settings_.noteChangeSemitones)
            {
                if (changeRun_++ == 0)
                    changeStartTime_ = frame.sampleTime;
                if (changeRun_ >= settings_.noteChangeFrames)
                {
                    noteMidi_ = frame.midiPitch;
                    changeRun_ = 0;
                    reportOnset(frame, changeStartTime_, std::max(0.01f, frame.periodicity), AnalysisFrame::pitchOnset);
                }
            }
            else
            {
                changeRun_ = 0;
                noteMidi_ += 0.1f * difference; // follow slow drift, not vibrato
            }
        }
    }
    else
    {
        voicedRun_ = 0;
        changeRun_ = 0;
        if (++unvoicedRun_ >= settings_.noteGapFrames)
            inNote_ = false;
    }

    previousRmsDb_ = frame.rmsDb;
    return frame;
}

float VocalAnalyzer::smoothedMidi(float midi, bool voiced) noexcept
{
    recentMidi_[static_cast<std::size_t>(recentWrite_)] = voiced ? midi : 0.0f;
    recentWrite_ = (recentWrite_ + 1) % static_cast<int>(recentMidi_.size());
    if (!voiced)
        return 0.0f;

    // Median of the voiced frames among the recent ones.
    int count = 0;
    for (float value : recentMidi_)
        if (value > 0.0f)
            medianScratch_[static_cast<std::size_t>(count++)] = value;

    const auto middle = medianScratch_.begin() + count / 2;
    std::nth_element(medianScratch_.begin(), middle, medianScratch_.begin() + count);

    // Only replace outliers (octave jumps, glitches) with the median. A plain median lags
    // behind vibrato and glides, so in-range frames keep their own, more precise value.
    return std::abs(midi - *middle) > settings_.outlierSemitones ? *middle : midi;
}

void VocalAnalyzer::reportOnset(AnalysisFrame& frame, std::int64_t time, float strength, std::uint8_t flag) noexcept
{
    // One onset per frame, and never two within the minimum spacing. The spectral and
    // pitch detectors often both see the same note; whichever reports first wins.
    if (frame.hasOnset() || (anyOnset_ && time - lastOnsetTime_ < minInterOnsetInput_))
        return;

    frame.onsetStrength = strength;
    frame.onsetSampleTime = time;
    frame.flags |= flag;
    lastOnsetTime_ = time;
    anyOnset_ = true;
}

} // namespace lb
