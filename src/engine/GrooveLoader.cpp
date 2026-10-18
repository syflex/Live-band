#include "engine/GrooveLoader.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace lb
{
namespace
{
std::uint32_t readLayerMask(const juce::var& list, const std::vector<std::string>& layers, juce::String& error)
{
    std::uint32_t mask = 0;
    if (const auto* names = list.getArray())
    {
        for (const auto& name : *names)
        {
            const auto found = std::find(layers.begin(), layers.end(), name.toString().toStdString());
            if (found == layers.end())
                error = "\"" + name.toString() + "\" is not one of the style's layers";
            else
                mask |= 1u << (found - layers.begin());
        }
    }
    return mask;
}

std::vector<EntryStage> readStages(const juce::var& list, const std::vector<std::string>& layers, juce::String& error)
{
    std::vector<EntryStage> stages;
    if (const auto* items = list.getArray())
        for (const auto& item : *items)
            stages.push_back({ static_cast<int>(item.getProperty("bar", 0)), readLayerMask(item.getProperty("layers", juce::var()), layers, error) });
    std::stable_sort(stages.begin(), stages.end(), [](const EntryStage& a, const EntryStage& b) { return a.bar < b.bar; });
    return stages;
}

std::vector<GrooveNote> readHits(const juce::var& list, const std::vector<std::string>& layers)
{
    std::vector<GrooveNote> hits;
    if (const auto* array = list.getArray())
    {
        for (const auto& item : *array)
        {
            GrooveNote n;
            n.beat = static_cast<double>(item.getProperty("beat", 0.0));
            n.note = static_cast<std::uint8_t>(juce::jlimit(0, 127, static_cast<int>(item.getProperty("note", 0))));
            n.velocity = static_cast<std::uint8_t>(juce::jlimit(1, 127, static_cast<int>(item.getProperty("velocity", 100))));
            n.lengthBeats = static_cast<float>(static_cast<double>(item.getProperty("length", 0.125)));
            const auto layer = item.getProperty("layer", 0);
            if (layer.isString())
            {
                const auto found = std::find(layers.begin(), layers.end(), layer.toString().toStdString());
                n.layer = static_cast<std::uint8_t>(found != layers.end() ? found - layers.begin() : 0);
            }
            else
            {
                n.layer = static_cast<std::uint8_t>(static_cast<int>(layer));
            }
            hits.push_back(n);
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const GrooveNote& a, const GrooveNote& b) { return a.beat < b.beat; });
    return hits;
}

bool readPattern(const juce::File& file, const std::vector<std::string>& layers, int beatsPerBar, Pattern& pattern, juce::String& error)
{
    juce::FileInputStream stream(file);
    juce::MidiFile midi;
    if (!stream.openedOk() || !midi.readFrom(stream))
    {
        error = "cannot read " + file.getFileName();
        return false;
    }
    const int ppq = midi.getTimeFormat();
    if (ppq <= 0)
    {
        error = file.getFileName() + " uses SMPTE time, not beats";
        return false;
    }

    pattern = {};
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence track(*midi.getTrack(t));
        track.updateMatchedPairs();

        juce::String name;
        bool hasNotes = false;
        for (const auto* event : track)
        {
            if (event->message.isTrackNameEvent() && name.isEmpty())
                name = event->message.getTextFromTextMetaEvent();
            hasNotes = hasNotes || event->message.isNoteOn();
        }
        if (!hasNotes)
            continue;

        int layer = 0;
        if (name.isNotEmpty())
        {
            const auto found = std::find(layers.begin(), layers.end(), name.toStdString());
            if (found == layers.end())
            {
                error = file.getFileName() + ": track \"" + name + "\" is not one of the style's layers";
                return false;
            }
            layer = static_cast<int>(found - layers.begin());
        }

        for (const auto* event : track)
        {
            if (!event->message.isNoteOn())
                continue;
            GrooveNote n;
            n.beat = event->message.getTimeStamp() / ppq;
            n.note = static_cast<std::uint8_t>(event->message.getNoteNumber());
            n.velocity = event->message.getVelocity();
            n.layer = static_cast<std::uint8_t>(layer);
            if (event->noteOffObject != nullptr)
                n.lengthBeats = static_cast<float>((event->noteOffObject->message.getTimeStamp() - event->message.getTimeStamp()) / ppq);
            if (n.lengthBeats <= 0.0f)
                n.lengthBeats = 0.125f;
            pattern.notes.push_back(n);
        }
    }

    // Whole bars, enough for the last event (the files mark the loop end with a meta event).
    const double lastBeat = midi.getLastTimestamp() / ppq;
    const int bars = std::max(1, static_cast<int>(std::ceil(lastBeat / beatsPerBar - 1e-9)));
    pattern.lengthBeats = static_cast<double>(bars) * beatsPerBar;
    pattern.sort();
    return true;
}
} // namespace

std::unique_ptr<Style> loadStyle(const juce::File& folder, juce::String& error)
{
    const auto jsonFile = folder.getChildFile("style.json");
    juce::var json;
    const auto parsed = juce::JSON::parse(jsonFile.loadFileAsString(), json);
    if (!jsonFile.existsAsFile() || parsed.failed() || !json.isObject())
    {
        error = "cannot read " + jsonFile.getFullPathName() + (parsed.failed() ? ": " + parsed.getErrorMessage() : juce::String());
        return nullptr;
    }

    auto style = std::make_unique<Style>();
    style->name = json.getProperty("name", folder.getFileName()).toString().toStdString();

    const auto signature = json.getProperty("timeSignature", juce::var());
    if (signature.isArray() && signature.size() == 2)
    {
        if (static_cast<int>(signature[1]) != 4)
        {
            error = "only time signatures counted in quarter notes (such as 4/4) are supported for now";
            return nullptr;
        }
        style->beatsPerBar = static_cast<int>(signature[0]);
    }

    const auto tempo = json.getProperty("tempo", juce::var());
    style->defaultBpm = static_cast<double>(tempo.getProperty("default", 100.0));
    style->minBpm = static_cast<double>(tempo.getProperty("min", style->defaultBpm * 0.75));
    style->maxBpm = static_cast<double>(tempo.getProperty("max", style->defaultBpm * 1.25));
    style->swing = static_cast<double>(json.getProperty("swing", 0.5));

    if (const auto* layers = json.getProperty("layers", juce::var()).getArray())
        for (const auto& layer : *layers)
            style->layers.push_back(layer.toString().toStdString());
    if (style->layers.empty())
        style->layers.push_back("drums");

    style->countIn = readHits(json.getProperty("countIn", juce::var()), style->layers);
    style->entry = readHits(json.getProperty("entry", juce::var()), style->layers);
    style->ending = readHits(json.getProperty("ending", juce::var()), style->layers);

    juce::String layerError;
    style->pitchedLayers = readLayerMask(json.getProperty("pitchedLayers", juce::var()), style->layers, layerError);
    if (json.hasProperty("fillLayers"))
        style->fillLayers = readLayerMask(json.getProperty("fillLayers", juce::var()), style->layers, layerError);
    const auto staged = json.getProperty("stagedEntry", juce::var());
    style->entryKeyKnown = readStages(staged.getProperty("keyKnown", juce::var()), style->layers, layerError);
    style->entryKeyUnknown = readStages(staged.getProperty("keyUnknown", juce::var()), style->layers, layerError);
    const auto afterLock = staged.getProperty("afterKeyLock", juce::var());
    style->afterKeyLockLayers = readLayerMask(afterLock.getProperty("layers", juce::var()), style->layers, layerError);
    style->afterKeyLockEveryBars = static_cast<int>(afterLock.getProperty("everyBars", 4));
    if (layerError.isNotEmpty())
    {
        error = juce::String(style->name) + ": " + layerError;
        return nullptr;
    }

    if (const auto* sections = json.getProperty("sections", juce::var()).getArray())
    {
        for (const auto& item : *sections)
        {
            Section section;
            section.name = item.getProperty("name", "Section").toString().toStdString();
            if (!readPattern(folder.getChildFile(item.getProperty("pattern", "").toString()), style->layers, style->beatsPerBar, section.main, error)
                || !readPattern(folder.getChildFile(item.getProperty("fill", "").toString()), style->layers, style->beatsPerBar, section.fill, error))
                return nullptr;
            if (item.hasProperty("patternMinor")
                && !readPattern(folder.getChildFile(item.getProperty("patternMinor", "").toString()), style->layers, style->beatsPerBar, section.mainMinor, error))
                return nullptr;
            style->sections.push_back(std::move(section));
        }
    }

    if (const auto problem = style->validate(); !problem.empty())
    {
        error = juce::String(style->name) + ": " + juce::String(problem);
        return nullptr;
    }
    return style;
}

} // namespace lb
