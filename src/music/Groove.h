#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lb
{

// One note of a groove. Positions and lengths are in beats.
struct GrooveNote
{
    double beat = 0.0;
    float lengthBeats = 0.125f;
    std::uint8_t note = 0;
    std::uint8_t velocity = 100;
    std::uint8_t layer = 0; // index into Style::layers
};

// A loop of whole bars. Notes are sorted by beat and lie in [0, lengthBeats).
struct Pattern
{
    double lengthBeats = 0.0;
    std::vector<GrooveNote> notes;

    int bars(int beatsPerBar) const noexcept;
    void sort();
};

struct Section
{
    std::string name;
    Pattern main;             // in C major
    Pattern mainMinor;        // in C minor; empty if the section has no minor version
    Pattern mainIntense;      // a fuller version for loud singing, in C major; may be empty
    Pattern mainIntenseMinor; // and in C minor
    Pattern fill;             // one bar, played into the next section or on request

    // The pattern for the key's mode and the singer's intensity, falling back to what the
    // section has.
    const Pattern& patternFor(bool minor, bool intense = false) const noexcept
    {
        if (intense && minor && !mainIntenseMinor.notes.empty())
            return mainIntenseMinor;
        if (intense && !minor && !mainIntense.notes.empty())
            return mainIntense;
        return minor && !mainMinor.notes.empty() ? mainMinor : main;
    }
};

// One step of a staged entry: from `bar` on, these layers play too.
struct EntryStage
{
    int bar = 0;
    std::uint32_t layers = 0; // bit i is Style::layers[i]
};

// A playing style: its sections, its feel and its tempo range. Built on the message thread
// and read-only afterwards, so the audio thread may read it without locks.
struct Style
{
    std::string name;
    std::string meter = "4/4"; // as written; in 6/8 the beat is the dotted quarter
    int beatsPerBar = 4;
    double defaultBpm = 100.0;
    double minBpm = 60.0;
    double maxBpm = 180.0;
    double swing = 0.5; // share of an eighth note taken by its first sixteenth; 0.5 is straight
    std::vector<std::string> layers;
    std::uint32_t pitchedLayers = 0; // transposed to the key; patterns are written in C
    std::uint32_t fillLayers = ~0u;  // what a fill replaces; other layers carry on
    std::vector<GrooveNote> countIn; // one bar, played before the band starts without taps
    std::vector<GrooveNote> entry;   // added on the downbeat when the band starts or a section begins
    std::vector<GrooveNote> ending;  // the final hits, on the downbeat after the last fill
    std::vector<Section> sections;

    // Staged entry: with the key known when the band starts, and without it. With no stages
    // every layer plays from the first bar. Without a key, afterKeyLockLayers join at the first
    // multiple of afterKeyLockEveryBars after the key arrives.
    std::vector<EntryStage> entryKeyKnown;
    std::vector<EntryStage> entryKeyUnknown;
    std::uint32_t afterKeyLockLayers = 0;
    int afterKeyLockEveryBars = 4;

    // The style's own balance: a level in dB for an instrument ("drums", "bass", "keys",
    // "pad"), on top of the band volume. A pad-led style turns its pad up.
    std::vector<std::pair<std::string, float>> mixDb;

    int layerIndex(const std::string& name) const noexcept;
    std::uint32_t allLayers() const noexcept { return layers.size() >= 32 ? ~0u : (1u << layers.size()) - 1u; }

    // A tapped tempo moved by octaves into [minBpm, maxBpm]: a singer tapping half time
    // at 55 BPM gets 110.
    double foldTempo(double bpm) const noexcept;

    // Empty if the style can be played, otherwise what is wrong with it.
    std::string validate() const;
};

// Moves a position inside a bar by the swing amount: the second sixteenth of every eighth
// note is delayed. Monotonic, and identity when swing is 0.5.
double swingBeat(double beatInBar, double swing) noexcept;

} // namespace lb
