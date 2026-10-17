#include "engine/SessionRecorder.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace lb
{
namespace
{
constexpr int fifoSamples = 65536; // about 1.4 s at 48 kHz
}

SessionRecorder::SessionRecorder() = default;

SessionRecorder::~SessionRecorder()
{
    if (isOpen_)
        close(new juce::DynamicObject());
    writerThread_.stopThread(2000);
}

std::unique_ptr<SessionRecorder::ThreadedWriter> SessionRecorder::makeWriter(const juce::File& file, int numChannels, juce::String& error)
{
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (!static_cast<juce::FileOutputStream*>(stream.get())->openedOk())
    {
        error = "cannot create " + file.getFullPathName();
        return nullptr;
    }

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate_).withNumChannels(numChannels).withBitsPerSample(24);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        error = "cannot write WAV at " + juce::String(sampleRate_) + " Hz";
        return nullptr;
    }
    return std::make_unique<ThreadedWriter>(writer.release(), writerThread_, fifoSamples);
}

bool SessionRecorder::open(const juce::File& folder, double sampleRate, juce::String& error)
{
    if (isOpen_)
    {
        error = "a session is already being recorded";
        return false;
    }
    if (!folder.createDirectory())
    {
        error = "cannot create " + folder.getFullPathName();
        return false;
    }

    folder_ = folder;
    sampleRate_ = sampleRate;
    inputWriter_ = makeWriter(folder.getChildFile("input.wav"), 1, error);
    outputWriter_ = inputWriter_ != nullptr ? makeWriter(folder.getChildFile("output.wav"), 2, error) : nullptr;

    analysisCsv_ = std::make_unique<juce::FileOutputStream>(folder.getChildFile("analysis.csv"));
    onsetsCsv_ = std::make_unique<juce::FileOutputStream>(folder.getChildFile("onsets.csv"));
    clicksCsv_ = std::make_unique<juce::FileOutputStream>(folder.getChildFile("clicks.csv"));
    if (inputWriter_ == nullptr || outputWriter_ == nullptr || !analysisCsv_->openedOk() || !onsetsCsv_->openedOk() || !clicksCsv_->openedOk())
    {
        if (error.isEmpty())
            error = "cannot create files in " + folder.getFullPathName();
        inputWriter_.reset();
        outputWriter_.reset();
        analysisCsv_.reset();
        onsetsCsv_.reset();
        clicksCsv_.reset();
        return false;
    }

    writeLine(analysisCsv_.get(), "time_s,f0_hz,midi,periodicity,rms_db,odf,voiced,onset\n");
    writeLine(onsetsCsv_.get(), "time_s,strength,source\n");
    writeLine(clicksCsv_.get(), "time_s,bar,beat,subdivision,bpm\n");

    framesWritten_ = onsetsWritten_ = clicksWritten_ = 0;
    droppedBlocks_.store(0);
    if (!writerThread_.isThreadRunning())
        writerThread_.startThread();

    activeInput_.store(inputWriter_.get());
    activeOutput_.store(outputWriter_.get());
    isOpen_ = true;
    return true;
}

void SessionRecorder::writeAudio(const float* input, const float* left, const float* right, int numSamples) noexcept
{
    // Sequentially consistent on purpose: close() relies on seeing audioInUse_ set before
    // this thread could have loaded a writer pointer.
    audioInUse_.store(true);
    if (auto* writer = activeInput_.load())
    {
        const float* channels[] = { input };
        if (!writer->write(channels, numSamples))
            droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
    }
    if (auto* writer = activeOutput_.load())
    {
        const float* channels[] = { left, right };
        if (!writer->write(channels, numSamples))
            droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
    }
    audioInUse_.store(false);
}

void SessionRecorder::writeLine(juce::FileOutputStream* stream, const char* format, ...)
{
    if (stream == nullptr)
        return;
    char line[256];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length > 0)
        stream->write(line, static_cast<size_t>(std::min<int>(length, static_cast<int>(sizeof(line)) - 1)));
}

void SessionRecorder::writeFrame(const AnalysisFrame& frame, std::int64_t sessionStart)
{
    if (!isOpen_ || frame.sampleTime < sessionStart)
        return;

    writeLine(analysisCsv_.get(), "%.5f,%.3f,%.3f,%.3f,%.3f,%.5f,%d,%d\n", static_cast<double>(frame.sampleTime - sessionStart) / sampleRate_,
              static_cast<double>(frame.f0Hz), static_cast<double>(frame.midiPitch), static_cast<double>(frame.periodicity),
              static_cast<double>(frame.rmsDb), static_cast<double>(frame.odf), frame.isVoiced() ? 1 : 0, frame.hasOnset() ? 1 : 0);
    ++framesWritten_;

    if (frame.hasOnset() && frame.onsetSampleTime >= sessionStart)
    {
        const char* source = (frame.flags & AnalysisFrame::energyOnset) != 0 ? "spectrum" : "pitch";
        writeLine(onsetsCsv_.get(), "%.5f,%.3f,%s\n", static_cast<double>(frame.onsetSampleTime - sessionStart) / sampleRate_,
                  static_cast<double>(frame.onsetStrength), source);
        ++onsetsWritten_;
    }
}

void SessionRecorder::writeClick(const ClickEvent& click, std::int64_t sessionStart)
{
    if (!isOpen_ || click.sampleTime < sessionStart)
        return;
    writeLine(clicksCsv_.get(), "%.5f,%d,%d,%d,%.3f\n", static_cast<double>(click.sampleTime - sessionStart) / sampleRate_, click.bar,
              click.beat, click.subdivision, static_cast<double>(click.bpm));
    ++clicksWritten_;
}

void SessionRecorder::close(juce::DynamicObject::Ptr metadata)
{
    if (!isOpen_)
        return;

    activeInput_.store(nullptr);
    activeOutput_.store(nullptr);
    while (audioInUse_.load())
        juce::Thread::sleep(1);

    // Deleting a ThreadedWriter flushes what is left in its FIFO and finishes the WAV header.
    inputWriter_.reset();
    outputWriter_.reset();
    analysisCsv_.reset();
    onsetsCsv_.reset();
    clicksCsv_.reset();
    isOpen_ = false;

    if (metadata == nullptr)
        metadata = new juce::DynamicObject();
    metadata->setProperty("sampleRate", sampleRate_);
    metadata->setProperty("analysisFrames", static_cast<juce::int64>(framesWritten_));
    metadata->setProperty("onsets", static_cast<juce::int64>(onsetsWritten_));
    metadata->setProperty("clicks", static_cast<juce::int64>(clicksWritten_));
    metadata->setProperty("droppedAudioBlocks", static_cast<juce::int64>(droppedBlocks_.load()));
    metadata->setProperty("timesAreRelativeTo", "the first sample of input.wav");
    metadata->setProperty("clickTiming",
                          "clicks.csv gives when each click was sent to the output. The singer heard it about one output latency "
                          "later, and their voice reached input.wav about one round-trip latency after the click was sent.");
    folder_.getChildFile("session.json").replaceWithText(juce::JSON::toString(juce::var(metadata.get())));
}

} // namespace lb
