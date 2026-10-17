#pragma once

#include "audio/Fft.h"

#include <complex>
#include <vector>

namespace lb
{

// Detects note starts from the spectrum. The onset detection function (ODF) is
// log-compressed spectral flux, compared against a frequency-max-filtered earlier frame
// (as in SuperFlux) so that vibrato does not look like new notes. The max filter is about
// a semitone wide at every frequency, because vibrato moves high harmonics across many bins. Peaks are picked
// against an adaptive threshold: a multiple of the recent median plus an offset.
class OnsetDetector
{
public:
    struct Settings
    {
        float logGain = 100.0f;        // log(1 + logGain * magnitude)
        int referenceLag = 2;          // compare with the frame this many hops back
        float maxFilterSemitones = 1.0f; // half-width of the reference max filter
        float thresholdScale = 1.5f;   // times the running median
        float thresholdOffset = 0.05f; // plus this
        double medianSeconds = 0.3;
        double minInterOnsetSeconds = 0.08;
        float minHz = 60.0f;
        float maxHz = 6000.0f;
    };

    struct Result
    {
        float odf = 0.0f;           // value for the frame just processed
        bool onset = false;         // a peak was confirmed at the previous frame
        float strength = 0.0f;      // how far that peak rose above the threshold
    };

    // Not real-time safe. frameSize must be a power of two.
    void prepare(double sampleRate, int frameSize, int hopSize, const Settings& settings);
    void prepare(double sampleRate, int frameSize, int hopSize) { prepare(sampleRate, frameSize, hopSize, Settings {}); }
    void reset() noexcept;

    int frameSize() const noexcept { return frameSize_; }

    // Real-time safe. Called once per hop with the latest frameSize() samples, oldest first.
    // When `onset` is set, the onset belongs to the previous call's frame (one hop earlier).
    Result process(const float* frame) noexcept;

private:
    float median(int count) noexcept;

    Settings settings_;
    int frameSize_ = 0;
    std::vector<int> filterLow_;  // per bin: range of the reference max filter
    std::vector<int> filterHigh_;
    int firstBin_ = 0;
    int lastBin_ = 0;
    int spectrumLow_ = 0;  // bins whose log magnitude is needed
    int spectrumHigh_ = 0;
    int medianLength_ = 1;
    int minInterOnsetFrames_ = 1;

    Fft fft_;
    std::vector<float> window_;
    float magnitudeScale_ = 1.0f;
    std::vector<std::complex<float>> spectrum_;

    // Log spectra of the last referenceLag + 1 frames, as a ring.
    std::vector<std::vector<float>> logSpectra_;
    int newestSpectrum_ = 0;
    int framesSeen_ = 0;

    // Recent ODF values for the median, plus the last few for peak picking.
    std::vector<float> odfHistory_;
    std::vector<float> medianScratch_;
    int odfWrite_ = 0;
    int odfCount_ = 0;
    float recent_[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // recent_[0] newest
    float recentThreshold_[2] = { 0.0f, 0.0f };     // thresholds for recent_[0], recent_[1]
    int framesSinceOnset_ = 1 << 20;
};

} // namespace lb
