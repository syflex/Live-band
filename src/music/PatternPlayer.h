#pragma once

#include "music/BeatClock.h"
#include "music/Groove.h"
#include "music/Key.h"

#include <array>
#include <cstdint>
#include <limits>

namespace lb
{

// A note event for an instrument, at a sample offset inside the current block.
struct PlayerNote
{
    int offset = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0; // 0 for note-off
    std::uint8_t layer = 0;
    bool on = false;
};

// One beat of the band, for beat lamps and logs.
struct BeatTick
{
    enum Flags : std::uint8_t
    {
        countIn = 1,
        fill = 2,
        ending = 4,
        sectionStart = 8
    };

    std::int64_t sampleTime = 0;
    std::int32_t bar = 0;        // from the band's first downbeat; negative during the count-in
    std::int16_t beat = 0;       // within the bar, from 0
    std::int16_t section = 0;
    std::uint8_t flags = 0;
    std::int8_t key = -1;      // Key::index() of the key the band plays in; -1 without one
    std::uint32_t layers = 0;  // the layers playing in this bar
    float bpm = 0.0f;
};

enum class BandState : std::uint8_t
{
    stopped,
    waiting, // start() was called; the first beat is still ahead
    playing, // includes the count-in
    ending   // the final hits are ringing out
};

// Plays a Style on the sample timeline: count-in, sections that loop, fills, a section
// change on the bar after a fill, and an ending. Notes are placed with sample accuracy, and
// every note-on gets its note-off, also when the band is stopped.
//
// Section changes work like an arranger keyboard: a request plays the current section's
// fill from the current beat to the end of the bar, and the new section starts on the next
// downbeat. A request in the last half beat of a bar plays the fill in the next bar. A fill
// replaces only the style's fill layers (the drums); the harmony carries on.
//
// Key: patterns are written in C. The pitched layers are moved to the key by -6 to +5
// semitones, so the bass and keys stay in their registers, and minor keys play the minor
// version of a section. A key change takes effect on the next downbeat. Without a key the
// pitched layers stay silent.
//
// Staged entry: the style says which layers come in on which bar, with and without a key
// at the start; a band started without one brings its harmony in at the first stage boundary
// after the key arrives. When the drums come in after the first bar, they bring the entry hits.
//
// Threads: prepare() and setStyle() while stopped and not processing; everything else on
// the audio thread, between process() calls. Real-time safe.
class PatternPlayer
{
public:
    static constexpr int noBar = std::numeric_limits<int>::min();
    static constexpr double minFillBeats = 0.5;

    void prepare(double sampleRate) noexcept;
    void setStyle(const Style* style) noexcept; // stops the band
    const Style* style() const noexcept { return style_; }

    // The band's first downbeat falls on downbeatSample. With a count-in, the count-in bars
    // come before it. The key (invalid if not known yet) chooses the staged entry.
    void start(std::int64_t downbeatSample, double samplesPerBeat, int section, int countInBars, Key key = {}) noexcept;
    void stop() noexcept;    // a fill, then the ending hits on the next downbeat
    void stopNow() noexcept; // every sounding note is released at the start of the next block
    void nextSection() noexcept;
    void selectSection(int section) noexcept;
    void fill() noexcept;
    void setTempo(double samplesPerBeat) noexcept;
    // Pulls the band towards a beat at `sample`, the beat nearest to where it is now.
    void steerToNearestBeat(std::int64_t sample, double samplesPerBeat) noexcept;
    void setLayerMask(std::uint32_t mask) noexcept { layerMask_ = mask; }
    void setKey(const Key& key) noexcept; // from the next downbeat when playing

    // How far the pitched layers move for a key: -6 to +5 semitones from C.
    static int transposition(const Key& key) noexcept { return key.isValid() ? (key.tonic + 6) % 12 - 6 : 0; }

    // Renders [blockStart, blockStart + numSamples): calls onNote(const PlayerNote&) in time
    // order, and onTick(const BeatTick&) for every beat that starts in the block.
    template <typename OnNote, typename OnTick>
    void process(std::int64_t blockStart, int numSamples, OnNote&& onNote, OnTick&& onTick) noexcept
    {
        render(blockStart, numSamples);
        for (int i = 0; i < eventCount_; ++i)
            onNote(events_[static_cast<std::size_t>(i)]);
        for (int i = 0; i < tickCount_; ++i)
            onTick(ticks_[static_cast<std::size_t>(i)]);
    }

    BandState state() const noexcept { return state_; }
    int section() const noexcept { return state_ == BandState::waiting ? startSection_ : section_; }
    int pendingSection() const noexcept { return nextSection_; }
    bool fillScheduled() const noexcept { return fillBar_ != noBar; }
    double bpm() const noexcept;
    Key key() const noexcept { return pendingKey_.isValid() ? pendingKey_ : key_; } // the latest key given
    Key playingKey() const noexcept { return key_; }
    std::uint32_t activeLayers() const noexcept { return arrangementMask_ & layerMask_; }
    // The tempo playing now; when stopped, the tempo the band will start at.
    double samplesPerBeat() const noexcept
    {
        return state_ == BandState::waiting || state_ == BandState::stopped ? startSpb_ : clock_.samplesPerBeat();
    }
    // Notes the player still has to release. Zero once the band has stopped.
    int sustainedNotes() const noexcept { return pendingCount_; }
    std::uint64_t droppedEvents() const noexcept { return droppedEvents_; }

private:
    enum class BarKind : std::uint8_t
    {
        none,
        countIn,
        main,
        fill,
        ending
    };

    struct PendingOff
    {
        double beat = 0.0;
        std::uint8_t note = 0;
        std::uint8_t layer = 0;
    };

    void render(std::int64_t blockStart, int numSamples) noexcept;
    void enterBar(int bar) noexcept;
    void scheduleFill() noexcept;
    void playBarSegment(int bar, double segmentStart, double segmentEnd) noexcept;
    void playNotes(const GrooveNote* begin, const GrooveNote* end, double barStart, double patternOffset,
                   double segmentStart, double segmentEnd, bool swing, bool ignoreMask, std::uint32_t layers = ~0u) noexcept;
    void playPatternBar(const Pattern& pattern, int barInPattern, double barStart, double segmentStart, double segmentEnd,
                        std::uint32_t layers) noexcept;
    void applyKey(const Key& key) noexcept;
    std::uint32_t arrangementFor(int bar) const noexcept;
    void noteOn(double beat, const GrooveNote& note, bool ignoreMask) noexcept;
    void releaseDue(double untilBeat) noexcept;
    void releaseAll(int offset) noexcept;
    void push(const PlayerNote& note) noexcept;
    int offsetOf(double beat) const noexcept;
    void resetArrangement() noexcept;

    const Style* style_ = nullptr;
    double sampleRate_ = 48000.0;

    // Key and staged entry.
    Key key_;
    Key pendingKey_;
    int transpose_ = 0;
    bool minor_ = false;
    static constexpr int maxStages = 8;
    std::array<EntryStage, maxStages> stages_ {};
    int stageCount_ = 0;
    bool waitingForKey_ = false;
    std::uint32_t arrangementMask_ = 0xffffffffu;
    std::uint32_t entryLayers_ = 0; // the layers the entry hits are on
    BeatClock clock_;
    BandState state_ = BandState::stopped;
    std::uint32_t layerMask_ = 0xffffffffu;
    std::int64_t nextBlockStart_ = 0;

    // Start request.
    std::int64_t startSample_ = 0;
    double startBeat_ = 0.0;
    double startSpb_ = 24000.0;
    int startSection_ = 0;

    // Arrangement.
    int currentBar_ = noBar;
    BarKind barKind_ = BarKind::none;
    int section_ = 0;
    int sectionStartBar_ = 0;
    int fillBar_ = noBar;
    int nextSection_ = -1;
    int endBar_ = noBar;
    bool entryThisBar_ = false;
    bool flushRequested_ = false;

    // The block being rendered.
    int blockLength_ = 0;
    int blockOffset_ = 0;
    double blockStartBeat_ = 0.0;
    double blockRate_ = 0.0;
    double scanFrom_ = 0.0; // where the next block's beat range starts
    std::int64_t blockStart_ = 0;

    static constexpr int maxEvents = 512;
    static constexpr int maxTicks = 64;
    static constexpr int maxPending = 256;
    std::array<PlayerNote, maxEvents> events_ {};
    std::array<BeatTick, maxTicks> ticks_ {};
    std::array<PendingOff, maxPending> pending_ {};
    int eventCount_ = 0;
    int tickCount_ = 0;
    int pendingCount_ = 0;
    std::uint64_t droppedEvents_ = 0;
};

} // namespace lb
