#include "audio/OnsetDetector.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lb
{

void OnsetDetector::prepare(double sampleRate, int frameSize, int hopSize, const Settings& settings)
{
    settings_ = settings;
    frameSize_ = frameSize;
    fft_.prepare(frameSize);

    window_.resize(static_cast<std::size_t>(frameSize));
    double windowSum = 0.0;
    for (int i = 0; i < frameSize; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / frameSize);
        window_[static_cast<std::size_t>(i)] = static_cast<float>(w);
        windowSum += w;
    }
    // A full-scale sine then has a peak magnitude of about 1.
    magnitudeScale_ = static_cast<float>(2.0 / windowSum);

    const double binHz = sampleRate / frameSize;
    firstBin_ = std::max(1, static_cast<int>(std::lround(settings.minHz / binHz)));
    lastBin_ = std::min(frameSize / 2 - 1, static_cast<int>(std::lround(settings.maxHz / binHz)));

    // Max-filter range for each bin: +-maxFilterSemitones, but at least one neighbour each side.
    const double ratio = std::exp2(settings.maxFilterSemitones / 12.0);
    filterLow_.assign(static_cast<std::size_t>(frameSize / 2 + 1), 0);
    filterHigh_.assign(static_cast<std::size_t>(frameSize / 2 + 1), 0);
    for (int k = firstBin_; k <= lastBin_; ++k)
    {
        filterLow_[static_cast<std::size_t>(k)] = std::max(1, std::min(k - 1, static_cast<int>(std::floor(k / ratio))));
        filterHigh_[static_cast<std::size_t>(k)] = std::min(frameSize / 2, std::max(k + 1, static_cast<int>(std::ceil(k * ratio))));
    }
    const int spectrumLow = filterLow_[static_cast<std::size_t>(firstBin_)];
    const int spectrumHigh = filterHigh_[static_cast<std::size_t>(lastBin_)];
    spectrumLow_ = spectrumLow;
    spectrumHigh_ = spectrumHigh;

    spectrum_.assign(static_cast<std::size_t>(frameSize), {});
    logSpectra_.assign(static_cast<std::size_t>(settings.referenceLag + 1), std::vector<float>(static_cast<std::size_t>(frameSize / 2 + 1), 0.0f));

    const double hopSeconds = hopSize / sampleRate;
    medianLength_ = std::max(3, static_cast<int>(std::lround(settings.medianSeconds / hopSeconds)));
    minInterOnsetFrames_ = std::max(1, static_cast<int>(std::lround(settings.minInterOnsetSeconds / hopSeconds)));
    odfHistory_.assign(static_cast<std::size_t>(medianLength_), 0.0f);
    medianScratch_.assign(static_cast<std::size_t>(medianLength_), 0.0f);

    reset();
}

void OnsetDetector::reset() noexcept
{
    for (auto& spectrum : logSpectra_)
        std::fill(spectrum.begin(), spectrum.end(), 0.0f);
    newestSpectrum_ = 0;
    framesSeen_ = 0;
    std::fill(odfHistory_.begin(), odfHistory_.end(), 0.0f);
    odfWrite_ = 0;
    odfCount_ = 0;
    std::fill(std::begin(recent_), std::end(recent_), 0.0f);
    std::fill(std::begin(recentThreshold_), std::end(recentThreshold_), 0.0f);
    framesSinceOnset_ = 1 << 20;
}

float OnsetDetector::median(int count) noexcept
{
    std::copy_n(odfHistory_.begin(), count, medianScratch_.begin());
    const auto middle = medianScratch_.begin() + count / 2;
    std::nth_element(medianScratch_.begin(), middle, medianScratch_.begin() + count);
    return *middle;
}

OnsetDetector::Result OnsetDetector::process(const float* frame) noexcept
{
    // Log-magnitude spectrum of the windowed frame.
    for (int i = 0; i < frameSize_; ++i)
        spectrum_[static_cast<std::size_t>(i)] = { frame[i] * window_[static_cast<std::size_t>(i)], 0.0f };
    fft_.forward(spectrum_.data());

    const int ringSize = static_cast<int>(logSpectra_.size());
    newestSpectrum_ = (newestSpectrum_ + 1) % ringSize;
    auto& current = logSpectra_[static_cast<std::size_t>(newestSpectrum_)];
    for (int k = spectrumLow_; k <= spectrumHigh_; ++k)
        current[static_cast<std::size_t>(k)] = std::log1p(settings_.logGain * magnitudeScale_ * std::abs(spectrum_[static_cast<std::size_t>(k)]));

    // Spectral flux against the max-filtered reference frame, averaged over the bins.
    float odf = 0.0f;
    if (framesSeen_ >= settings_.referenceLag)
    {
        const auto& reference = logSpectra_[static_cast<std::size_t>((newestSpectrum_ + 1) % ringSize)];
        float sum = 0.0f;
        for (int k = firstBin_; k <= lastBin_; ++k)
        {
            const auto low = reference.begin() + filterLow_[static_cast<std::size_t>(k)];
            const auto high = reference.begin() + filterHigh_[static_cast<std::size_t>(k)] + 1;
            const float ref = *std::max_element(low, high);
            sum += std::max(0.0f, current[static_cast<std::size_t>(k)] - ref);
        }
        odf = sum / static_cast<float>(lastBin_ - firstBin_ + 1);
    }
    ++framesSeen_;

    // Adaptive threshold from the median of the recent ODF, this frame included.
    odfHistory_[static_cast<std::size_t>(odfWrite_)] = odf;
    odfWrite_ = (odfWrite_ + 1) % medianLength_;
    odfCount_ = std::min(odfCount_ + 1, medianLength_);
    const float threshold = settings_.thresholdScale * median(odfCount_) + settings_.thresholdOffset;

    for (int i = 3; i > 0; --i)
        recent_[i] = recent_[i - 1];
    recent_[0] = odf;
    recentThreshold_[1] = recentThreshold_[0];
    recentThreshold_[0] = threshold;
    ++framesSinceOnset_;

    // The previous frame is an onset if it is a local peak (higher than the two frames
    // before it, not lower than this one), above its threshold, and not too soon after
    // the last onset.
    Result result;
    result.odf = odf;
    const float candidate = recent_[1];
    if (candidate > recentThreshold_[1] && candidate > recent_[2] && candidate > recent_[3] && candidate >= recent_[0]
        && framesSinceOnset_ - 1 >= minInterOnsetFrames_)
    {
        result.onset = true;
        result.strength = candidate - recentThreshold_[1];
        framesSinceOnset_ = 1; // the onset was one frame ago
    }
    return result;
}

} // namespace lb
