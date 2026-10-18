// lb-analyze: runs the app's VocalAnalyzer over audio files and writes the results as CSV,
// so the analysis can be evaluated offline (see tools/python).
//
//   lb-analyze [options] <audio file>...
//
// For each input, writes <name>.frames.csv (one row per 8 ms frame), <name>.onsets.csv,
// <name>.notes.csv (the sung notes the key estimator uses) and <name>.key.csv (the key
// estimate every 0.1 s).

#include "audio/VocalAnalyzer.h"
#include "music/KeyEstimator.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

namespace
{
void printUsage()
{
    std::puts(
        "usage: lb-analyze [options] <audio file>...\n"
        "\n"
        "Writes <name>.frames.csv, .onsets.csv, .notes.csv and .key.csv for each input.\n"
        "\n"
        "options:\n"
        "  --out-dir <dir>          output folder (default: next to each input)\n"
        "  --yin-threshold <x>      YIN CMND threshold (default 0.12)\n"
        "  --octave-min <x>         sub-octave check: minimum CMND of the chosen dip (default 0.05)\n"
        "  --octave-ratio <x>       sub-octave check: ratio the double-period dip must beat; 0 = off (default 0.3)\n"
        "  --gate-db <x>            level below which frames are unvoiced (default -50)\n"
        "  --voicing-hold <x>       CMND below which a voiced note stays voiced (default 0.2)\n"
        "  --onset-scale <x>        onset threshold: times the running median (default 1.5)\n"
        "  --onset-offset <x>       onset threshold: plus this (default 0.05)\n"
        "  --onset-lag <n>          frames between compared spectra (default 2)\n"
        "  --max-filter <st>        reference max filter half-width in semitones (default 1)\n"
        "  --note-change <st>       pitch change that counts as a new note (default 0.8)\n"
        "  --note-frames <n>        frames a change must last (default 4)\n"
        "  --min-ioi <seconds>      minimum time between onsets (default 0.08)\n"
        "  --key-profile <name>     krumhansl-kessler, aarden-essen, temperley or bellman-budge\n"
        "                           (default temperley)\n"
        "  --lock-margin <a> <b>    correlation lead needed to lock: a at 2 s of voice, b from\n"
        "                           10 s on (default 0.25 0.1)\n"
        "  --key-decay <seconds>    time constant of the pitch-class histogram (default 30)\n"
        "  --phrase-bonus <x>       extra weight of a phrase's last note (default 0)\n"
        "  --note-tolerance <st>    pitch range of a held note around its mean (default 0.5)\n"
        "  --min-note <seconds>     shortest note the key estimator counts (default 0.08)\n"


        "  --quiet                  print nothing but errors\n");
}

bool readMono(const juce::File& file, std::vector<float>& samples, double& sampleRate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "cannot read " + file.getFullPathName();
        return false;
    }

    const auto length = static_cast<int>(reader->lengthInSamples);
    const auto channels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(channels, length);
    reader->read(&buffer, 0, length, 0, true, true);

    samples.assign(static_cast<std::size_t>(length), 0.0f);
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::addWithMultiply(samples.data(), buffer.getReadPointer(ch), 1.0f / static_cast<float>(channels), length);
    sampleRate = reader->sampleRate;
    return true;
}
} // namespace

int main(int argc, char* argv[])
{
    lb::VocalAnalyzer::Settings settings;
    lb::KeyEstimator::Settings keySettings;
    juce::File outDir;
    bool quiet = false;
    std::vector<juce::File> inputs;

    const std::map<std::string, float*> floatOptions {
        { "--yin-threshold", &settings.pitch.threshold },  { "--gate-db", &settings.gateDb },
        { "--octave-min", &settings.pitch.octaveCheckMinCmnd }, { "--octave-ratio", &settings.pitch.octaveCheckRatio },
        { "--voicing-hold", &settings.voicingHoldCmnd },   { "--onset-scale", &settings.onsets.thresholdScale },
        { "--onset-offset", &settings.onsets.thresholdOffset }, { "--max-filter", &settings.onsets.maxFilterSemitones },
        { "--note-change", &settings.noteChangeSemitones },
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "-h" || arg == "--help")
        {
            printUsage();
            return 0;
        }
        if (arg == "--quiet")
        {
            quiet = true;
        }
        else if (arg == "--out-dir" && hasValue)
        {
            outDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
        }
        else if (auto it = floatOptions.find(arg); it != floatOptions.end() && hasValue)
        {
            *it->second = std::stof(argv[++i]);
        }
        else if (arg == "--onset-lag" && hasValue)
        {
            settings.onsets.referenceLag = std::stoi(argv[++i]);
        }
        else if (arg == "--note-frames" && hasValue)
        {
            settings.noteChangeFrames = std::stoi(argv[++i]);
        }
        else if (arg == "--min-ioi" && hasValue)
        {
            settings.onsets.minInterOnsetSeconds = std::stod(argv[++i]);
        }
        else if (arg == "--key-profile" && hasValue)
        {
            if (!lb::profileFromName(argv[++i], keySettings.profile))
            {
                std::fprintf(stderr, "unknown key profile: %s\n", argv[i]);
                return 2;
            }
        }
        else if (arg == "--lock-margin" && i + 2 < argc)
        {
            keySettings.lockMarginStart = std::stod(argv[++i]);
            keySettings.lockMarginEnd = std::stod(argv[++i]);
        }
        else if (arg == "--key-decay" && hasValue)
        {
            keySettings.decaySeconds = std::stod(argv[++i]);
        }
        else if (arg == "--phrase-bonus" && hasValue)
        {
            keySettings.phraseFinalBonus = std::stod(argv[++i]);
        }
        else if (arg == "--note-tolerance" && hasValue)
        {
            keySettings.notes.toleranceSemitones = std::stod(argv[++i]);
        }
        else if (arg == "--min-note" && hasValue)
        {
            keySettings.notes.minNoteSeconds = std::stod(argv[++i]);
        }

        else if (!arg.empty() && arg[0] == '-')
        {
            std::fprintf(stderr, "unknown option or missing value: %s\n", arg.c_str());
            printUsage();
            return 2;
        }
        else
        {
            inputs.push_back(juce::File::getCurrentWorkingDirectory().getChildFile(arg));
        }
    }

    if (inputs.empty())
    {
        printUsage();
        return 2;
    }
    if (outDir != juce::File() && !outDir.isDirectory() && !outDir.createDirectory())
    {
        std::fprintf(stderr, "cannot create %s\n", outDir.getFullPathName().toRawUTF8());
        return 1;
    }

    int failures = 0;
    for (const auto& input : inputs)
    {
        std::vector<float> samples;
        double rate = 0.0;
        juce::String error;
        if (!readMono(input, samples, rate, error))
        {
            std::fprintf(stderr, "%s\n", error.toRawUTF8());
            ++failures;
            continue;
        }

        lb::VocalAnalyzer analyzer;
        analyzer.prepare(rate, settings);
        std::vector<lb::AnalysisFrame> frames;
        frames.reserve(samples.size() / 300 + 16);

        const auto start = std::chrono::steady_clock::now();
        constexpr int blockSize = 512;
        for (std::size_t offset = 0; offset < samples.size(); offset += blockSize)
        {
            const int n = static_cast<int>(std::min<std::size_t>(blockSize, samples.size() - offset));
            analyzer.process(samples.data() + offset, n, [&](const lb::AnalysisFrame& frame) { frames.push_back(frame); });
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        const auto folder = outDir != juce::File() ? outDir : input.getParentDirectory();
        const auto base = folder.getChildFile(input.getFileNameWithoutExtension()).getFullPathName().toStdString();

        std::ofstream framesCsv(base + ".frames.csv");
        std::ofstream onsetsCsv(base + ".onsets.csv");
        std::ofstream notesCsv(base + ".notes.csv");
        std::ofstream keyCsv(base + ".key.csv");
        if (!framesCsv || !onsetsCsv || !notesCsv || !keyCsv)
        {
            std::fprintf(stderr, "cannot write results for %s\n", input.getFullPathName().toRawUTF8());
            ++failures;
            continue;
        }

        framesCsv << "time_s,f0_hz,midi,periodicity,rms_db,odf,voiced,onset\n" << std::fixed;
        onsetsCsv << "time_s,strength,source\n" << std::fixed;
        int voiced = 0, onsets = 0;
        for (const auto& f : frames)
        {
            framesCsv << std::setprecision(5) << static_cast<double>(f.sampleTime) / rate << ',' << std::setprecision(3) << f.f0Hz << ','
                      << f.midiPitch << ',' << f.periodicity << ',' << f.rmsDb << ',' << std::setprecision(5) << f.odf << ','
                      << (f.isVoiced() ? 1 : 0) << ',' << (f.hasOnset() ? 1 : 0) << '\n';
            voiced += f.isVoiced() ? 1 : 0;
            if (f.hasOnset())
            {
                ++onsets;
                const char* source = (f.flags & lb::AnalysisFrame::energyOnset) != 0 ? "spectrum" : "pitch";
                onsetsCsv << std::setprecision(5) << static_cast<double>(f.onsetSampleTime) / rate << ',' << f.onsetStrength << ','
                          << source << '\n';
            }
        }

        // The key, as the app would estimate it while listening. Keys are written as 0..11 for
        // C..B major and 12..23 for C..B minor; -1 means none.
        lb::KeyEstimator keys;
        keys.setSettings(keySettings);
        lb::NoteSegmenter segmenter;
        segmenter.setSettings(keySettings.notes);
        notesCsv << "start_s,duration_s,midi,rms_db,pitch_class\n" << std::fixed;
        keyCsv << "time_s,voiced_s,notes,tuning_cents,state,key,best,second,correlation,margin\n" << std::fixed;
        double nextKeyRow = 0.0;
        lb::KeyEstimator::Estimate keyEstimate;
        for (const auto& f : frames)
        {
            const double t = static_cast<double>(f.sampleTime) / rate;
            keys.process(t, f.isVoiced(), f.midiPitch, f.rmsDb);
            segmenter.process(t, f.isVoiced(), f.midiPitch, f.rmsDb,
                [&](const lb::SungNote& n) {
                    notesCsv << std::setprecision(4) << n.startSeconds << ',' << n.durationSeconds << ',' << std::setprecision(3) << n.midi << ','
                             << n.rmsDb << ',' << keys.pitchClassOf(n.midi) << '\n';
                },
                [] {});
            if (t >= nextKeyRow)
            {
                nextKeyRow += 0.1;
                keyEstimate = keys.estimate();
                keyCsv << std::setprecision(2) << t << ',' << keyEstimate.voicedSeconds << ',' << keyEstimate.notes << ',' << std::setprecision(1)
                       << keyEstimate.tuningCents << ',' << static_cast<int>(keyEstimate.state) << ',' << keyEstimate.key.index() << ','
                       << keyEstimate.best.index() << ',' << keyEstimate.second.index() << ',' << std::setprecision(4) << keyEstimate.correlation << ','
                       << keyEstimate.margin << '\n';
            }
        }
        keyEstimate = keys.estimate();

        if (!quiet)
        {
            const double duration = static_cast<double>(samples.size()) / rate;
            std::printf("%s: %.1f s at %.0f Hz, %zu frames, %.0f%% voiced, %d onsets, %.0fx real time, key %s\n",
                        input.getFileName().toRawUTF8(), duration, rate, frames.size(),
                        frames.empty() ? 0.0 : 100.0 * voiced / static_cast<double>(frames.size()), onsets,
                        seconds > 0.0 ? duration / seconds : 0.0, keyEstimate.best.name().c_str());
        }
    }

    return failures == 0 ? 0 : 1;
}
