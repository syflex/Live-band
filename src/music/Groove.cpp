#include "music/Groove.h"

#include <algorithm>
#include <cmath>

namespace lb
{

int Pattern::bars(int beatsPerBar) const noexcept
{
    return beatsPerBar > 0 ? static_cast<int>(std::lround(lengthBeats / beatsPerBar)) : 0;
}

void Pattern::sort()
{
    std::stable_sort(notes.begin(), notes.end(), [](const GrooveNote& a, const GrooveNote& b) { return a.beat < b.beat; });
}

double Style::foldTempo(double bpm) const noexcept
{
    if (!(bpm > 0.0))
        return defaultBpm;
    // The octave of the tapped tempo nearest to the range (in ratio), then clamped: 55 BPM
    // becomes 110, and 70 becomes 125 rather than 90 when the range is 90 to 125.
    const auto distance = [&](double b) { return b < minBpm ? std::log(minBpm / b) : b > maxBpm ? std::log(b / maxBpm) : 0.0; };
    double best = bpm;
    for (int k = -4; k <= 4; ++k)
    {
        const double candidate = bpm * std::exp2(k);
        if (distance(candidate) < distance(best) - 1e-12)
            best = candidate;
    }
    return std::clamp(best, minBpm, maxBpm);
}

int Style::layerIndex(const std::string& name) const noexcept
{
    for (std::size_t i = 0; i < layers.size(); ++i)
        if (layers[i] == name)
            return static_cast<int>(i);
    return -1;
}

std::string Style::validate() const
{
    if (beatsPerBar < 1 || beatsPerBar > 16)
        return "beats per bar must be between 1 and 16";
    if (!(minBpm > 0.0) || minBpm > defaultBpm || defaultBpm > maxBpm)
        return "the tempo range must contain the default tempo";
    if (swing < 0.5 || swing > 0.75)
        return "swing must be between 0.5 and 0.75";
    if (sections.empty())
        return "a style needs at least one section";

    const auto checkNotes = [&](const std::vector<GrooveNote>& notes, double length, const std::string& what) -> std::string {
        for (const auto& n : notes)
        {
            if (n.beat < 0.0 || n.beat >= length)
                return what + " has a note outside its length";
            if (n.layer >= layers.size())
                return what + " uses an unknown layer";
            if (n.note > 127 || n.velocity < 1 || n.velocity > 127)
                return what + " has an invalid note or velocity";
        }
        if (!std::is_sorted(notes.begin(), notes.end(), [](const GrooveNote& a, const GrooveNote& b) { return a.beat < b.beat; }))
            return what + " is not sorted";
        return {};
    };

    for (const auto& section : sections)
    {
        const double bar = beatsPerBar;
        if (section.main.lengthBeats < bar || std::fmod(section.main.lengthBeats, bar) != 0.0)
            return section.name + ": the pattern must be whole bars long";
        if (section.fill.lengthBeats != bar)
            return section.name + ": the fill must be one bar long";
        if (auto error = checkNotes(section.main.notes, section.main.lengthBeats, section.name); !error.empty())
            return error;
        if (!section.mainMinor.notes.empty())
        {
            if (section.mainMinor.lengthBeats != section.main.lengthBeats)
                return section.name + ": the minor pattern must be as long as the major one";
            if (auto error = checkNotes(section.mainMinor.notes, section.mainMinor.lengthBeats, section.name + " minor"); !error.empty())
                return error;
        }
        if (auto error = checkNotes(section.fill.notes, section.fill.lengthBeats, section.name + " fill"); !error.empty())
            return error;
    }
    if (layers.size() > 16)
        return "a style has at most 16 layers";
    for (const auto* stages : { &entryKeyKnown, &entryKeyUnknown })
        for (const auto& stage : *stages)
            if (stage.bar < 0 || (stage.layers & ~allLayers()) != 0)
                return "a staged entry names an unknown layer or a negative bar";
    if (afterKeyLockEveryBars < 1)
        return "afterKeyLock needs everyBars of at least 1";
    for (const auto* hits : { &countIn, &entry, &ending })
        if (auto error = checkNotes(*hits, beatsPerBar, "count-in, entry or ending"); !error.empty())
            return error;
    return {};
}

double swingBeat(double beatInBar, double swing) noexcept
{
    if (swing == 0.5)
        return beatInBar;
    // Work in eighth notes: the first half of each eighth stretches to `swing`, the
    // second half shrinks to 1 - swing.
    const double eighths = beatInBar * 2.0;
    const double whole = std::floor(eighths);
    const double part = eighths - whole;
    const double moved = part < 0.5 ? part * 2.0 * swing : swing + (part - 0.5) * 2.0 * (1.0 - swing);
    return (whole + moved) * 0.5;
}

} // namespace lb
