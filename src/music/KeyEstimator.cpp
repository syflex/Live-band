#include "music/KeyEstimator.h"

#include <algorithm>
#include <cctype>
#include <numbers>

namespace lb
{
namespace
{
const char* const pitchNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

struct ProfilePair
{
    std::array<double, 12> major;
    std::array<double, 12> minor;
};

// From https://extras.humdrum.org/man/keycor/ (the same values as music21.analysis.discrete).
const ProfilePair krumhanslKessler { { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 },
                                     { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 } };
const ProfilePair aardenEssen { { 17.7661, 0.145624, 14.9265, 0.160186, 19.8049, 11.3587, 0.291248, 22.062, 0.145624, 8.15494, 0.232998, 4.95122 },
                                { 18.2648, 0.737619, 14.0499, 16.8599, 0.702494, 14.4362, 0.702494, 18.6161, 4.56621, 1.93186, 7.37619, 1.75623 } };
const ProfilePair temperleyKostkaPayne { { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 },
                                         { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 } };
const ProfilePair bellmanBudge { { 16.80, 0.86, 12.95, 1.41, 13.49, 11.93, 1.25, 20.28, 1.80, 8.04, 0.62, 10.57 },
                                 { 18.16, 0.69, 12.99, 13.34, 1.07, 11.15, 1.38, 21.07, 7.49, 1.53, 0.92, 10.21 } };

const ProfilePair& pairFor(KeyProfile profile) noexcept
{
    switch (profile)
    {
        case KeyProfile::aardenEssen:          return aardenEssen;
        case KeyProfile::temperleyKostkaPayne: return temperleyKostkaPayne;
        case KeyProfile::bellmanBudge:         return bellmanBudge;
        case KeyProfile::krumhanslKessler:     break;
    }
    return krumhanslKessler;
}
} // namespace

std::string Key::name() const
{
    return isValid() ? std::string(pitchNames[tonic]) + (minor ? " minor" : " major") : std::string("no key");
}

std::string Key::shortName() const
{
    return isValid() ? std::string(pitchNames[tonic]) + (minor ? "m" : "") : std::string("-");
}

Key Key::relative() const noexcept
{
    if (!isValid())
        return {};
    return minor ? Key { (tonic + 3) % 12, false } : Key { (tonic + 9) % 12, true };
}

bool Key::parse(const std::string& text, Key& key)
{
    std::string t;
    for (const char c : text)
        if (c != ' ')
            t += c;
    if (t.empty())
        return false;
    constexpr int naturals[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A to G
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
    if (letter < 'A' || letter > 'G')
        return false;
    int tonic = naturals[letter - 'A'];
    std::size_t i = 1;
    if (i < t.size() && (t[i] == '#' || t[i] == 'b'))
    {
        // "Bb" is B flat; a lone "b" after the letter is never "minor" here.
        tonic += t[i] == '#' ? 1 : -1;
        ++i;
    }
    std::string mode = t.substr(i);
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool isMinor = false;
    if (mode == "m" || mode == "min" || mode == "minor")
        isMinor = true;
    else if (!(mode.empty() || mode == "maj" || mode == "major"))
        return false;
    key = { (tonic + 12) % 12, isMinor };
    return true;
}

double mirexKeyScore(const Key& estimate, const Key& reference) noexcept
{
    if (!estimate.isValid() || !reference.isValid())
        return 0.0;
    if (estimate == reference)
        return 1.0;
    // As in mir_eval.key.weighted_score: only the fifth above counts.
    const int interval = ((estimate.tonic - reference.tonic) % 12 + 12) % 12;
    if (estimate.minor == reference.minor && interval == 7)
        return 0.5;
    if (estimate.minor != reference.minor)
    {
        // The relative minor's tonic is a minor third below the major's.
        const int relative = reference.minor ? (reference.tonic + 3) % 12 : (reference.tonic + 9) % 12;
        if (estimate.tonic == relative)
            return 0.3;
        if (estimate.tonic == reference.tonic)
            return 0.2;
    }
    return 0.0;
}

const char* profileName(KeyProfile profile) noexcept
{
    switch (profile)
    {
        case KeyProfile::aardenEssen:          return "aarden-essen";
        case KeyProfile::temperleyKostkaPayne: return "temperley";
        case KeyProfile::bellmanBudge:         return "bellman-budge";
        case KeyProfile::krumhanslKessler:     break;
    }
    return "krumhansl-kessler";
}

bool profileFromName(const std::string& name, KeyProfile& profile) noexcept
{
    for (auto p : { KeyProfile::krumhanslKessler, KeyProfile::aardenEssen, KeyProfile::temperleyKostkaPayne, KeyProfile::bellmanBudge })
    {
        if (name == profileName(p))
        {
            profile = p;
            return true;
        }
    }
    return false;
}

const std::array<double, 12>& profileWeights(KeyProfile profile, bool minor) noexcept
{
    const auto& pair = pairFor(profile);
    return minor ? pair.minor : pair.major;
}

void NoteSegmenter::reset() noexcept
{
    count_ = 0;
    sum_ = 0.0;
    levelSum_ = 0.0;
    unvoicedSince_ = -1.0;
    inPhrase_ = false;
}

void KeyEstimator::reset() noexcept
{
    segmenter_.reset();
    histogram_.fill(0.0);
    tuningX_ = tuningY_ = 0.0;
    lastFade_ = -1.0;
    voicedSeconds_ = 0.0;
    lastFrame_ = -1.0;
    notes_ = 0;
    lastPitchClass_ = -1;
    lastNoteWeight_ = 0.0;
    locked_ = false;
    lockedKey_ = {};
}

void KeyEstimator::process(double seconds, bool voiced, double midi, float rmsDb) noexcept
{
    if (voiced && lastFrame_ >= 0.0)
        voicedSeconds_ += std::clamp(seconds - lastFrame_, 0.0, 0.05);
    lastFrame_ = seconds;
    segmenter_.process(seconds, voiced, midi, rmsDb, [this](const SungNote& note) { addNote(note); }, [this] { addPhraseEnd(); });
}

void KeyEstimator::fadeTo(double seconds) noexcept
{
    if (lastFade_ >= 0.0 && seconds > lastFade_)
    {
        const double dt = seconds - lastFade_;
        const double histogramFactor = std::exp(-dt / settings_.decaySeconds);
        for (auto& weight : histogram_)
            weight *= histogramFactor;
        const double tuningFactor = std::exp(-dt / settings_.tuningDecaySeconds);
        tuningX_ *= tuningFactor;
        tuningY_ *= tuningFactor;
    }
    if (seconds > lastFade_)
        lastFade_ = seconds;
}

double KeyEstimator::tuningCents() const noexcept
{
    if (tuningX_ * tuningX_ + tuningY_ * tuningY_ < 1e-12)
        return 0.0;
    return std::atan2(tuningY_, tuningX_) / (2.0 * std::numbers::pi) * 100.0;
}

int KeyEstimator::pitchClassOf(double midi) const noexcept
{
    const auto rounded = static_cast<int>(std::lround(midi - tuningCents() / 100.0));
    return (rounded % 12 + 12) % 12;
}

void KeyEstimator::addNote(const SungNote& note) noexcept
{
    fadeTo(note.startSeconds + note.durationSeconds);

    // The note's deviation from the semitone grid, as an angle, so that +49 and -49 cents
    // average to 50 rather than 0.
    const double angle = 2.0 * std::numbers::pi * (note.midi - std::round(note.midi));
    tuningX_ += note.durationSeconds * std::cos(angle);
    tuningY_ += note.durationSeconds * std::sin(angle);

    const int pitchClass = pitchClassOf(note.midi);
    const double loudness = 0.5 + 0.5 * std::clamp((static_cast<double>(note.rmsDb) + 50.0) / 30.0, 0.0, 1.0);
    const double weight = note.durationSeconds * loudness;
    histogram_[static_cast<std::size_t>(pitchClass)] += weight;
    lastPitchClass_ = pitchClass;
    lastNoteWeight_ = weight;
    ++notes_;
    if (settings_.autoLock && !locked_ && estimate().state == State::confident)
        lock();
}

void KeyEstimator::addPhraseEnd() noexcept
{
    if (lastPitchClass_ < 0)
        return;
    histogram_[static_cast<std::size_t>(lastPitchClass_)] += settings_.phraseFinalBonus * lastNoteWeight_;
    lastPitchClass_ = -1; // once per phrase
    if (settings_.autoLock && !locked_ && estimate().state == State::confident)
        lock();
}

double KeyEstimator::requiredMargin(double voicedSeconds) const noexcept
{
    const double span = std::max(settings_.lockRampSeconds - settings_.minVoiceSeconds, 1e-9);
    const double progress = std::clamp((voicedSeconds - settings_.minVoiceSeconds) / span, 0.0, 1.0);
    return settings_.lockMarginStart + (settings_.lockMarginEnd - settings_.lockMarginStart) * progress;
}

void KeyEstimator::score(const std::array<double, 12>& histogram, KeyProfile profile, Key& best, double& bestR, Key& second,
                         double& secondR) noexcept
{
    best = second = {};
    bestR = secondR = -2.0;

    double hMean = 0.0;
    for (double h : histogram)
        hMean += h / 12.0;
    double hVar = 0.0;
    for (double h : histogram)
        hVar += (h - hMean) * (h - hMean);
    if (hVar <= 0.0)
        return;

    for (int minor = 0; minor < 2; ++minor)
    {
        const auto& p = profileWeights(profile, minor != 0);
        double pMean = 0.0;
        for (double w : p)
            pMean += w / 12.0;
        double pVar = 0.0;
        for (double w : p)
            pVar += (w - pMean) * (w - pMean);

        for (int tonic = 0; tonic < 12; ++tonic)
        {
            double cov = 0.0;
            for (int pc = 0; pc < 12; ++pc)
                cov += (histogram[static_cast<std::size_t>(pc)] - hMean) * (p[static_cast<std::size_t>((pc - tonic + 12) % 12)] - pMean);
            const double r = cov / std::sqrt(hVar * pVar);
            const Key key { tonic, minor != 0 };
            if (r > bestR)
            {
                second = best;
                secondR = bestR;
                best = key;
                bestR = r;
            }
            else if (r > secondR)
            {
                second = key;
                secondR = r;
            }
        }
    }
}

KeyEstimator::Estimate KeyEstimator::estimate() const noexcept
{
    Estimate e;
    e.voicedSeconds = voicedSeconds_;
    e.tuningCents = tuningCents();
    e.notes = notes_;
    score(histogram_, settings_.profile, e.best, e.correlation, e.second, e.margin);
    e.requiredMargin = requiredMargin(voicedSeconds_);
    if (e.best.isValid())
    {
        e.margin = e.second.isValid() ? e.correlation - e.margin : e.correlation;
        e.confidence = std::clamp(e.margin / std::max(e.requiredMargin, 1e-9), 0.0, 1.0);
    }
    else
    {
        e.correlation = e.margin = 0.0;
    }

    if (locked_)
    {
        e.state = State::locked;
        e.key = lockedKey_;
    }
    else if (e.best.isValid() && voicedSeconds_ >= settings_.minVoiceSeconds)
    {
        e.state = e.margin >= e.requiredMargin ? State::confident : State::guessing;
        e.key = e.best;
    }
    return e;
}

void KeyEstimator::lock() noexcept
{
    const Key best = estimate().best;
    if (best.isValid())
        lock(best);
}

void KeyEstimator::lock(const Key& key) noexcept
{
    lockedKey_ = key;
    locked_ = key.isValid();
}

} // namespace lb
