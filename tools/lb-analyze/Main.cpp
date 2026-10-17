// lb-analyze: runs the app's VocalAnalyzer over audio files and writes the results as CSV,
// so the analysis can be evaluated offline (see tools/python).
//
//   lb-analyze [options] <audio file>...
//
// For each input, writes <name>.frames.csv (one row per 8 ms frame) and <name>.onsets.csv.

#include "audio/VocalAnalyzer.h"

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
        "Writes <name>.frames.csv and <name>.onsets.csv for each input.\n"
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
        if (!framesCsv || !onsetsCsv)
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

        if (!quiet)
        {
            const double duration = static_cast<double>(samples.size()) / rate;
            std::printf("%s: %.1f s at %.0f Hz, %zu frames, %.0f%% voiced, %d onsets, %.0fx real time\n",
                        input.getFileName().toRawUTF8(), duration, rate, frames.size(),
                        frames.empty() ? 0.0 : 100.0 * voiced / static_cast<double>(frames.size()), onsets,
                        seconds > 0.0 ? duration / seconds : 0.0);
        }
    }

    return failures == 0 ? 0 : 1;
}
