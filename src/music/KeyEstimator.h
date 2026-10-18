#pragma once

#include "music/Key.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace lb
{

// Pitch-class weightings for the 12 degrees of a major and a minor key, starting at the
// tonic. All four come from Humdrum's keycor (as also used by music21).
enum class KeyProfile
{
    krumhanslKessler,     // probe-tone ratings (Krumhansl and Kessler, 1982)
    aardenEssen,          // folk melodies of the Essen collection (Aarden, 2003)
    temperleyKostkaPayne, // a harmony textbook's examples (Temperley, 2007)
    bellmanBudge          // classical scores (Bellman, 2005)
};

const char* profileName(KeyProfile profile) noexcept;
bool profileFromName(const std::string& name, KeyProfile& profile) noexcept;
const std::array<double, 12>& profileWeights(KeyProfile profile, bool minor) noexcept;

// A sung note: a run of voiced frames that holds its pitch.
struct SungNote
{
    double startSeconds = 0.0;
    double durationSeconds = 0.0;
    double midi = 0.0;      // mean pitch, before any tuning correction
    float rmsDb = -120.0f;  // mean level
};

// Turns the analysis frames into sung notes and phrase ends. A note is a run of voiced frames
// whose pitch stays within the tolerance of the note's running mean for at least
// minNoteSeconds; glides and short blips are not notes. A phrase ends after phraseGapSeconds
// without voice.
//
// Real-time safe; used by one thread.
class NoteSegmenter
{
public:
    struct Settings
    {
        double toleranceSemitones = 0.5;
        double minNoteSeconds = 0.08;
        double phraseGapSeconds = 0.3;
    };

    void setSettings(const Settings& settings) noexcept { settings_ = settings; }
    void reset() noexcept;

    // One analysis frame. Calls onNote(const SungNote&) when a note ends and onPhraseEnd()
    // once after each phrase.
    template <typename OnNote, typename OnPhraseEnd>
    void process(double seconds, bool voiced, double midi, float rmsDb, OnNote&& onNote, OnPhraseEnd&& onPhraseEnd)
    {
        if (voiced && midi > 0.0)
        {
            unvoicedSince_ = -1.0;
            if (count_ > 0 && std::abs(midi - sum_ / count_) > settings_.toleranceSemitones)
                finishNote(seconds, onNote);
            if (count_ == 0)
                start_ = seconds;
            sum_ += midi;
            levelSum_ += rmsDb;
            ++count_;
            inPhrase_ = true;
            return;
        }
        if (count_ > 0)
            finishNote(seconds, onNote);
        if (unvoicedSince_ < 0.0)
            unvoicedSince_ = seconds;
        if (inPhrase_ && seconds - unvoicedSince_ >= settings_.phraseGapSeconds)
        {
            inPhrase_ = false;
            onPhraseEnd();
        }
    }

private:
    template <typename OnNote>
    void finishNote(double endSeconds, OnNote&& onNote)
    {
        const double duration = endSeconds - start_;
        if (duration >= settings_.minNoteSeconds)
            onNote(SungNote { start_, duration, sum_ / count_, static_cast<float>(levelSum_ / count_) });
        count_ = 0;
        sum_ = 0.0;
        levelSum_ = 0.0;
    }

    Settings settings_;
    double start_ = 0.0;
    double sum_ = 0.0;
    double levelSum_ = 0.0;
    int count_ = 0;
    double unvoicedSince_ = -1.0;
    bool inPhrase_ = false;
};

// Estimates the key of a sung melody as it goes.
//
// - Tuning: an unaccompanied singer is often 20-50 cents off A440, so notes are binned
//   relative to a running circular mean of their deviation from the semitone grid.
// - Histogram: each note adds its duration times a loudness weight to its pitch class; the
//   last note of a phrase counts extra, because melodies tend to end on the tonic. Until the
//   key locks, the histogram fades with a time constant of decaySeconds.
// - Score: the Pearson correlation of the histogram with the 24 rotated profiles.
// - Decision: nothing before provisionalSeconds of voice; a provisional key once the best
//   key leads the second by provisionalMargin; the best guess, whatever the margin, from
//   decisionSeconds. lock() freezes the key the band plays; the estimate keeps running as a
//   shadow.
//
// Real-time safe; used by one thread.
class KeyEstimator
{
public:
    struct Settings
    {
        KeyProfile profile = KeyProfile::krumhanslKessler;
        double decaySeconds = 30.0;
        double phraseFinalBonus = 1.0; // extra weight for a phrase's last note, times its own
        double provisionalSeconds = 2.0;
        double decisionSeconds = 3.0;
        double provisionalMargin = 0.1;
        double tuningDecaySeconds = 60.0;
    };

    enum class State : std::uint8_t
    {
        listening,   // not enough singing yet
        provisional, // a likely key, still open to change
        bestGuess,   // enough singing: the best key, with its confidence
        locked       // fixed for the band; the estimate runs on as a shadow
    };

    struct Estimate
    {
        State state = State::listening;
        Key key;      // what to show or play: the locked key when locked
        Key best;     // the current best estimate (the shadow, when locked)
        Key second;
        double correlation = 0.0;
        double margin = 0.0;     // correlation of the best minus the second
        double confidence = 0.0; // 0 to 1, from the margin
        double voicedSeconds = 0.0;
        double tuningCents = 0.0;
        int notes = 0;
    };

    void setSettings(const Settings& settings) noexcept { settings_ = settings; }
    const Settings& settings() const noexcept { return settings_; }
    void reset() noexcept;

    // Feed one analysis frame (8 ms): time in seconds, voicing, smoothed MIDI pitch, level.
    void process(double seconds, bool voiced, double midi, float rmsDb) noexcept;

    // Also usable directly, for tests and offline analysis.
    void addNote(const SungNote& note) noexcept;
    void addPhraseEnd() noexcept;

    void lock() noexcept;
    void lock(const Key& key) noexcept; // a manual key
    void unlock() noexcept { locked_ = false; }
    bool isLocked() const noexcept { return locked_; }

    Estimate estimate() const noexcept;
    const std::array<double, 12>& histogram() const noexcept { return histogram_; }
    double tuningCents() const noexcept;
    int pitchClassOf(double midi) const noexcept;

    // The best and second-best keys for a histogram, with their correlations.
    static void score(const std::array<double, 12>& histogram, KeyProfile profile, Key& best, double& bestR, Key& second,
                      double& secondR) noexcept;

private:
    void fadeTo(double seconds) noexcept;

    Settings settings_;
    NoteSegmenter segmenter_;
    std::array<double, 12> histogram_ {};
    double tuningX_ = 0.0, tuningY_ = 0.0; // sum of weighted unit vectors of the deviation
    double lastFade_ = -1.0;
    double voicedSeconds_ = 0.0;
    double lastFrame_ = -1.0;
    int notes_ = 0;
    int lastPitchClass_ = -1;
    double lastNoteWeight_ = 0.0;
    bool locked_ = false;
    Key lockedKey_;
};

} // namespace lb
