#pragma once

#include "audio/AnalysisFrame.h"
#include "music/ClickTrack.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace lb
{

// Records a session into a folder:
//   input.wav     what the analysis heard (the mic, or the voice file), mono, 24-bit
//   output.wav    what was sent to the headphones, stereo, 24-bit
//   analysis.csv  the live analysis frames (same columns as lb-analyze's .frames.csv)
//   onsets.csv    the live onsets (same columns as lb-analyze's .onsets.csv)
//   clicks.csv    when each click was sent to the output
//   session.json  settings and summary
// All times are seconds from the first sample of input.wav.
//
// Threads: open(), writeFrame(), writeClick() and close() run on the message thread.
// writeAudio() runs on the audio thread: it only pushes into a FIFO that a background
// thread writes to disk, and drops (and counts) audio if the disk falls behind.
class SessionRecorder
{
public:
    SessionRecorder();
    ~SessionRecorder();

    bool open(const juce::File& folder, double sampleRate, juce::String& error);
    bool isOpen() const noexcept { return isOpen_; }
    juce::File folder() const { return folder_; }

    void writeAudio(const float* input, const float* left, const float* right, int numSamples) noexcept;

    void writeFrame(const AnalysisFrame& frame, std::int64_t sessionStart);
    void writeClick(const ClickEvent& click, std::int64_t sessionStart);

    // Finishes the files and writes session.json from `metadata` plus the recorder's own counts.
    void close(juce::DynamicObject::Ptr metadata);

    std::uint64_t droppedAudioBlocks() const noexcept { return droppedBlocks_.load(std::memory_order_relaxed); }

private:
    using ThreadedWriter = juce::AudioFormatWriter::ThreadedWriter;

    std::unique_ptr<ThreadedWriter> makeWriter(const juce::File& file, int numChannels, juce::String& error);
    void writeLine(juce::FileOutputStream* stream, const char* format, ...);

    juce::TimeSliceThread writerThread_ { "LiveBand session writer" };
    std::unique_ptr<ThreadedWriter> inputWriter_;
    std::unique_ptr<ThreadedWriter> outputWriter_;

    // What the audio thread sees. close() clears these, then waits until audioInUse_ is false
    // before deleting the writers.
    std::atomic<ThreadedWriter*> activeInput_ { nullptr };
    std::atomic<ThreadedWriter*> activeOutput_ { nullptr };
    std::atomic<bool> audioInUse_ { false };
    std::atomic<std::uint64_t> droppedBlocks_ { 0 };

    std::unique_ptr<juce::FileOutputStream> analysisCsv_;
    std::unique_ptr<juce::FileOutputStream> onsetsCsv_;
    std::unique_ptr<juce::FileOutputStream> clicksCsv_;
    juce::File folder_;
    double sampleRate_ = 0.0;
    bool isOpen_ = false;
    std::int64_t framesWritten_ = 0;
    std::int64_t onsetsWritten_ = 0;
    std::int64_t clicksWritten_ = 0;
};

} // namespace lb
