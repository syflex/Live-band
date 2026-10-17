#pragma once

#include <cstdint>
#include <string>
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
    Pattern main;
    Pattern fill; // one bar, played into the next section or on request
};

// A playing style: its sections, its feel and its tempo range. Built on the message thread
// and read-only afterwards, so the audio thread may read it without locks.
struct Style
{
    std::string name;
    int beatsPerBar = 4;
    double defaultBpm = 100.0;
    double minBpm = 60.0;
    double maxBpm = 180.0;
    double swing = 0.5; // share of an eighth note taken by its first sixteenth; 0.5 is straight
    std::vector<std::string> layers;
    std::vector<GrooveNote> countIn; // one bar, played before the band starts without taps
    std::vector<GrooveNote> entry;   // added on the downbeat when the band starts or a section begins
    std::vector<GrooveNote> ending;  // the final hits, on the downbeat after the last fill
    std::vector<Section> sections;

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
