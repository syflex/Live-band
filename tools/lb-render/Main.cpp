// lb-render: plays a style through an SFZ drum kit, offline, into a WAV file. It drives the
// same PatternPlayer and limiter the app uses, block by block, so it is a way to listen to
// a groove (and to check it) without an audio device.
//
//   lb-render --style assets/grooves/afrobeats --kit assets/sfz/afro-kit/afro-kit.sfz
//             --plan Verse:8,Chorus:8 --out afrobeats.wav

#include "audio/Limiter.h"
#include "engine/GrooveLoader.h"
#include "engine/SfizzInstrument.h"
#include "music/PatternPlayer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
void printUsage()
{
    std::puts(
        "usage: lb-render --style <folder> --kit <file.sfz> --out <file.wav> [options]\n"
        "\n"
        "options:\n"
        "  --plan <list>       sections and bar counts, e.g. Verse:8,Chorus:8 (default: each section 4 bars)\n"
        "                      each change plays the outgoing section's fill in its last bar\n"
        "  --bpm <x>           tempo (default: the style's)\n"
        "  --count-in          start with a one-bar count-in\n"
        "  --mute <layer>      leave a layer out, e.g. perc (may repeat)\n"
        "  --gain-db <x>       drum kit volume (default 0)\n"
        "  --rate <hz>         sample rate (default 48000)\n"
        "  --block <n>         block size (default 128)\n"
        "  --events <file>     also write every note as CSV: time_s,note,velocity,layer,on\n");
}

struct Step
{
    int section = 0;
    int bars = 4;
};

bool parsePlan(const juce::String& text, const lb::Style& style, std::vector<Step>& plan, juce::String& error)
{
    for (const auto& item : juce::StringArray::fromTokens(text, ",", ""))
    {
        const auto name = item.upToFirstOccurrenceOf(":", false, false).trim();
        const int bars = item.containsChar(':') ? item.fromFirstOccurrenceOf(":", false, false).getIntValue() : 4;
        int section = -1;
        for (std::size_t i = 0; i < style.sections.size(); ++i)
            if (name.equalsIgnoreCase(juce::String(style.sections[i].name)))
                section = static_cast<int>(i);
        if (section < 0 || bars < 1)
        {
            error = "bad plan entry \"" + item + "\"";
            return false;
        }
        plan.push_back({ section, bars });
    }
    return !plan.empty();
}
} // namespace

int main(int argc, char* argv[])
{
    juce::File styleDir, kitFile, outFile, eventsFile;
    juce::String planText;
    juce::StringArray muted;
    double bpm = 0.0, rate = 48000.0;
    float gainDb = 0.0f;
    int blockSize = 128;
    bool countIn = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> juce::String { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };
        const auto file = [&]() { return juce::File::getCurrentWorkingDirectory().getChildFile(next()); };
        if (arg == "--style") styleDir = file();
        else if (arg == "--kit") kitFile = file();
        else if (arg == "--out") outFile = file();
        else if (arg == "--events") eventsFile = file();
        else if (arg == "--plan") planText = next();
        else if (arg == "--bpm") bpm = next().getDoubleValue();
        else if (arg == "--count-in") countIn = true;
        else if (arg == "--mute") muted.add(next());
        else if (arg == "--gain-db") gainDb = next().getFloatValue();
        else if (arg == "--rate") rate = next().getDoubleValue();
        else if (arg == "--block") blockSize = next().getIntValue();
        else
        {
            printUsage();
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }
    if (styleDir == juce::File() || kitFile == juce::File() || outFile == juce::File() || rate <= 0.0 || blockSize < 1)
    {
        printUsage();
        return 2;
    }

    juce::String error;
    const auto style = lb::loadStyle(styleDir, error);
    if (style == nullptr)
    {
        std::fprintf(stderr, "lb-render: %s\n", error.toRawUTF8());
        return 1;
    }
    std::vector<Step> plan;
    if (planText.isEmpty())
    {
        for (std::size_t i = 0; i < style->sections.size(); ++i)
            plan.push_back({ static_cast<int>(i), 4 });
    }
    else if (!parsePlan(planText, *style, plan, error))
    {
        std::fprintf(stderr, "lb-render: %s\n", error.toRawUTF8());
        return 1;
    }

    lb::SfizzInstrument kit;
    if (!kit.load(kitFile.getFullPathName().toStdString()))
    {
        std::fprintf(stderr, "lb-render: cannot load %s\n", kitFile.getFullPathName().toRawUTF8());
        return 1;
    }
    kit.prepare(rate, blockSize);
    kit.enableFreeWheeling();

    std::uint32_t mask = 0xffffffffu;
    for (const auto& layer : muted)
    {
        const auto found = std::find(style->layers.begin(), style->layers.end(), layer.toStdString());
        if (found == style->layers.end())
        {
            std::fprintf(stderr, "lb-render: no layer called %s\n", layer.toRawUTF8());
            return 1;
        }
        mask &= ~(1u << (found - style->layers.begin()));
    }

    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(style.get());
    player.setLayerMask(mask);
    if (bpm <= 0.0)
        bpm = style->defaultBpm;
    const double spb = 60.0 * rate / bpm;
    const int bpb = style->beatsPerBar;
    const int countInBars = countIn ? 1 : 0;
    const auto downbeat = static_cast<std::int64_t>(std::llround(countInBars * bpb * spb)) + blockSize;
    player.start(downbeat, spb, plan.front().section, countInBars);

    // Requests go in at the start of each step's last bar, so its fill takes the whole bar.
    struct Request
    {
        std::int64_t sample;
        int section; // -1: stop
    };
    std::vector<Request> requests;
    int bar = 0;
    for (std::size_t i = 0; i < plan.size(); ++i)
    {
        bar += plan[i].bars;
        const auto at = downbeat + static_cast<std::int64_t>(std::llround((bar - 1) * bpb * spb));
        requests.push_back({ at, i + 1 < plan.size() ? plan[i + 1].section : -1 });
    }

    juce::AudioBuffer<float> mix(2, static_cast<int>(requests.back().sample + std::llround(bpb * spb) + 3.0 * rate));
    mix.clear();
    juce::AudioBuffer<float> block(2, blockSize);
    lb::Limiter limiter;
    limiter.prepare(rate);
    const float gain = juce::Decibels::decibelsToGain(gainDb);

    std::ofstream events;
    if (eventsFile != juce::File())
    {
        events.open(eventsFile.getFullPathName().toStdString());
        events << "time_s,note,velocity,layer,on\n";
    }
    std::vector<int> notesPerLayer(style->layers.size(), 0);
    float minLimiterGain = 1.0f;

    std::size_t nextRequest = 0;
    for (std::int64_t t = 0; t < mix.getNumSamples(); t += blockSize)
    {
        const int n = static_cast<int>(std::min<std::int64_t>(blockSize, mix.getNumSamples() - t));
        while (nextRequest < requests.size() && requests[nextRequest].sample <= t)
        {
            const auto& request = requests[nextRequest++];
            if (request.section < 0)
                player.stop();
            else if (request.section == player.section())
                player.fill();
            else
                player.selectSection(request.section);
        }
        player.process(t, n,
            [&](const lb::PlayerNote& e) {
                if (e.on)
                {
                    kit.noteOn(e.offset, e.note, e.velocity);
                    ++notesPerLayer[e.layer];
                }
                else
                {
                    kit.noteOff(e.offset, e.note);
                }
                if (events.is_open())
                    events << (static_cast<double>(t + e.offset) / rate) << ',' << int(e.note) << ',' << int(e.velocity) << ','
                           << style->layers[e.layer] << ',' << (e.on ? 1 : 0) << '\n';
            },
            [](const lb::BeatTick&) {});

        kit.render(block.getWritePointer(0), block.getWritePointer(1), n);
        block.applyGain(0, n, gain);
        limiter.process(block.getWritePointer(0), block.getWritePointer(1), n);
        minLimiterGain = std::min(minLimiterGain, limiter.currentGain());
        for (int ch = 0; ch < 2; ++ch)
            mix.copyFrom(ch, static_cast<int>(t), block, ch, 0, n);
    }

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = outFile.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (stream != nullptr)
        writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(mix, 0, mix.getNumSamples()))
    {
        std::fprintf(stderr, "lb-render: cannot write %s\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }
    writer.reset();

    const float peak = mix.getMagnitude(0, mix.getNumSamples());
    const float rms = std::sqrt(0.5f * (std::pow(mix.getRMSLevel(0, 0, mix.getNumSamples()), 2.0f) + std::pow(mix.getRMSLevel(1, 0, mix.getNumSamples()), 2.0f)));
    std::printf("%s: %.1f s at %.1f BPM, peak %.1f dBFS, RMS %.1f dBFS, limiter at most %.1f dB\n", outFile.getFileName().toRawUTF8(),
                mix.getNumSamples() / rate, bpm, juce::Decibels::gainToDecibels(peak), juce::Decibels::gainToDecibels(rms),
                juce::Decibels::gainToDecibels(minLimiterGain));
    for (std::size_t i = 0; i < style->layers.size(); ++i)
        std::printf("  %-8s %d notes\n", style->layers[i].c_str(), notesPerLayer[i]);
    if (player.sustainedNotes() != 0 || player.droppedEvents() != 0)
    {
        std::fprintf(stderr, "lb-render: %d notes still held, %llu events dropped\n", player.sustainedNotes(),
                     static_cast<unsigned long long>(player.droppedEvents()));
        return 1;
    }
    return 0;
}
