#pragma once

// Synthetic test signals with known pitch and note timing.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace test
{

struct Note
{
    double startSeconds;
    double durationSeconds;
    double midi;
};

inline double midiToHz(double midi)
{
    return 440.0 * std::exp2((midi - 69.0) / 12.0);
}

// A voice-like tone: harmonics up to 5 kHz with a 1/k spectrum (like a sawtooth),
// optionally with vibrato. Phase-continuous across frequency changes.
class ToneSynth
{
public:
    explicit ToneSynth(double sampleRate) : sampleRate_(sampleRate) {}

    float next(double frequencyHz, float amplitude, int maxHarmonics = 40)
    {
        phase_ += frequencyHz / sampleRate_;
        phase_ -= std::floor(phase_);
        double sum = 0.0, norm = 0.0;
        for (int k = 1; k <= maxHarmonics && k * frequencyHz < 5000.0; ++k)
        {
            sum += std::sin(2.0 * std::numbers::pi * k * phase_) / k;
            norm += 1.0 / k;
        }
        return static_cast<float>(amplitude * sum / std::max(norm, 1.0) * 1.5);
    }

private:
    double sampleRate_;
    double phase_ = 0.0;
};

inline std::vector<float> sine(double sampleRate, double hz, double seconds, float amplitude)
{
    std::vector<float> out(static_cast<std::size_t>(seconds * sampleRate));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sampleRate));
    return out;
}

inline std::vector<float> harmonicTone(double sampleRate, double hz, double seconds, float amplitude)
{
    ToneSynth synth(sampleRate);
    std::vector<float> out(static_cast<std::size_t>(seconds * sampleRate));
    for (auto& x : out)
        x = synth.next(hz, amplitude);
    return out;
}

// Band-limited pulse train: the Fourier series of a raised-cosine pulse 40% of the period
// wide (like the open phase of the glottis), up to 7 kHz or 0.45 of the sample rate. It is
// rich in harmonics, and unlike pulses drawn sample by sample it does not alias.
inline std::vector<float> pulseTrain(double sampleRate, double hz, double seconds, float amplitude, double width = 0.4)
{
    const double limit = std::min(7000.0, 0.45 * sampleRate);
    const int harmonics = std::max(1, static_cast<int>(limit / hz));

    std::vector<double> coefficients(static_cast<std::size_t>(harmonics) + 1, 0.0);
    double peak = 0.0;
    for (int k = 1; k <= harmonics; ++k)
    {
        const double kw = k * width;
        coefficients[static_cast<std::size_t>(k)] = std::abs(kw - 1.0) < 1e-9
                                                        ? 0.5
                                                        : std::sin(std::numbers::pi * kw) / (std::numbers::pi * kw * (1.0 - kw * kw));
        peak += std::abs(coefficients[static_cast<std::size_t>(k)]);
    }

    std::vector<float> out(static_cast<std::size_t>(seconds * sampleRate), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / sampleRate;
        double sum = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            sum += coefficients[static_cast<std::size_t>(k)] * std::cos(k * phase);
        out[i] = static_cast<float>(amplitude * sum / peak);
    }
    return out;
}

// Odd harmonics at `oddLevel` of the even ones: as the odd harmonics fade, the half-period
// dip in YIN's difference function gets as deep as the full-period one.
inline std::vector<float> evenHeavyTone(double sampleRate, double hz, double seconds, double oddLevel)
{
    std::vector<float> out(static_cast<std::size_t>(seconds * sampleRate));
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        double sum = 0.0;
        for (int k = 1; k <= 8; ++k)
            sum += ((k % 2) != 0 ? oddLevel / k : 1.0 / k) * std::sin(2.0 * std::numbers::pi * k * hz * static_cast<double>(i) / sampleRate + 0.3 * k);
        out[i] = static_cast<float>(0.3 * sum);
    }
    return out;
}

inline void addNoise(std::vector<float>& signal, float rmsLevel, unsigned seed = 7)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0f, rmsLevel);
    for (auto& x : signal)
        x += noise(rng);
}

inline float rmsOf(const std::vector<float>& signal)
{
    double sum = 0.0;
    for (float x : signal)
        sum += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(sum / std::max<std::size_t>(signal.size(), 1)));
}

// Renders notes with 10 ms attacks and 30 ms releases. Notes that touch (legato) keep
// the same phase and level, so only the pitch changes.
inline std::vector<float> renderNotes(double sampleRate, const std::vector<Note>& notes, double totalSeconds, float amplitude,
                                      double vibratoCents = 0.0, double vibratoHz = 5.5)
{
    std::vector<float> out(static_cast<std::size_t>(totalSeconds * sampleRate), 0.0f);
    ToneSynth synth(sampleRate);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        for (std::size_t n = 0; n < notes.size(); ++n)
        {
            const auto& note = notes[n];
            const double end = note.startSeconds + note.durationSeconds;
            if (t < note.startSeconds || t >= end)
                continue;

            const bool legatoIn = n > 0 && std::abs(notes[n - 1].startSeconds + notes[n - 1].durationSeconds - note.startSeconds) < 1e-9;
            const bool legatoOut = n + 1 < notes.size() && std::abs(end - notes[n + 1].startSeconds) < 1e-9;
            double envelope = 1.0;
            if (!legatoIn)
                envelope = std::min(envelope, (t - note.startSeconds) / 0.01);
            if (!legatoOut)
                envelope = std::min(envelope, (end - t) / 0.03);

            const double vibrato = vibratoCents / 100.0 * std::sin(2.0 * std::numbers::pi * vibratoHz * t);
            out[i] = synth.next(midiToHz(note.midi + vibrato), amplitude * static_cast<float>(std::clamp(envelope, 0.0, 1.0)));
            break;
        }
    }
    return out;
}

// Matches detected onset times to reference times within `tolerance` seconds, one to one.
struct OnsetScore
{
    int hits = 0;
    int misses = 0;
    int falseAlarms = 0;
    double meanAbsErrorSeconds = 0.0;

    double fMeasure() const
    {
        const double precision = hits + falseAlarms > 0 ? static_cast<double>(hits) / (hits + falseAlarms) : 0.0;
        const double recall = hits + misses > 0 ? static_cast<double>(hits) / (hits + misses) : 0.0;
        return precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
    }
};

inline OnsetScore scoreOnsets(const std::vector<double>& reference, const std::vector<double>& detected, double tolerance)
{
    OnsetScore score;
    std::vector<bool> used(detected.size(), false);
    double errorSum = 0.0;
    for (double ref : reference)
    {
        int best = -1;
        for (std::size_t d = 0; d < detected.size(); ++d)
            if (!used[d] && std::abs(detected[d] - ref) <= tolerance
                && (best < 0 || std::abs(detected[d] - ref) < std::abs(detected[static_cast<std::size_t>(best)] - ref)))
                best = static_cast<int>(d);
        if (best >= 0)
        {
            used[static_cast<std::size_t>(best)] = true;
            ++score.hits;
            errorSum += std::abs(detected[static_cast<std::size_t>(best)] - ref);
        }
        else
        {
            ++score.misses;
        }
    }
    score.falseAlarms = static_cast<int>(std::count(used.begin(), used.end(), false));
    score.meanAbsErrorSeconds = score.hits > 0 ? errorSum / score.hits : 0.0;
    return score;
}

} // namespace test
