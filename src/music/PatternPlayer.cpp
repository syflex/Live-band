#include "music/PatternPlayer.h"

#include <algorithm>
#include <cmath>

namespace lb
{

void PatternPlayer::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    stopNow();
}

void PatternPlayer::setStyle(const Style* style) noexcept
{
    stopNow();
    style_ = style;
    startSection_ = 0;
    if (style_ != nullptr)
        startSpb_ = 60.0 * sampleRate_ / style_->defaultBpm;
}

void PatternPlayer::start(std::int64_t downbeatSample, double samplesPerBeat, int section, int countInBars) noexcept
{
    if (style_ == nullptr || !(samplesPerBeat > 0.0))
        return;
    if (state_ == BandState::playing || state_ == BandState::ending)
        flushRequested_ = true;

    resetArrangement();
    const int bpb = style_->beatsPerBar;
    countInBars = std::max(countInBars, 0);
    startSample_ = std::llround(static_cast<double>(downbeatSample) - countInBars * bpb * samplesPerBeat);
    startBeat_ = static_cast<double>(startSample_ - downbeatSample) / samplesPerBeat;
    startSpb_ = samplesPerBeat;
    startSection_ = std::clamp(section, 0, static_cast<int>(style_->sections.size()) - 1);
    section_ = startSection_;
    state_ = BandState::waiting;
}

void PatternPlayer::stop() noexcept
{
    switch (state_)
    {
        case BandState::stopped:
        case BandState::ending:
            return;
        case BandState::waiting:
            state_ = BandState::stopped;
            resetArrangement();
            return;
        case BandState::playing:
            if (currentBar_ < 0)
            {
                stopNow(); // still counting in: nothing to end
                return;
            }
            if (endBar_ != noBar)
                return;
            nextSection_ = -1;
            scheduleFill();
            endBar_ = fillBar_ + 1;
            return;
    }
}

void PatternPlayer::stopNow() noexcept
{
    if (state_ == BandState::playing || state_ == BandState::ending)
        startSection_ = section_;
    flushRequested_ = true;
    state_ = BandState::stopped;
    resetArrangement();
}

void PatternPlayer::nextSection() noexcept
{
    if (style_ == nullptr)
        return;
    const int count = static_cast<int>(style_->sections.size());
    const int from = nextSection_ >= 0 ? nextSection_ : section();
    selectSection((from + 1) % count);
}

void PatternPlayer::selectSection(int section) noexcept
{
    if (style_ == nullptr || state_ == BandState::ending)
        return;
    section = std::clamp(section, 0, static_cast<int>(style_->sections.size()) - 1);

    // Before the first downbeat the request just changes where the band starts.
    if (state_ == BandState::stopped || state_ == BandState::waiting || currentBar_ < 0)
    {
        startSection_ = section;
        section_ = section;
        return;
    }
    if (endBar_ != noBar)
        return;
    if (section == section_ && nextSection_ < 0)
        return;
    nextSection_ = section;
    scheduleFill();
}

void PatternPlayer::fill() noexcept
{
    if (style_ == nullptr || state_ != BandState::playing || currentBar_ < 0 || endBar_ != noBar)
        return;
    scheduleFill();
}

void PatternPlayer::setTempo(double samplesPerBeat) noexcept
{
    if (!(samplesPerBeat > 0.0))
        return;
    if (state_ == BandState::playing || state_ == BandState::ending)
        clock_.setTempo(nextBlockStart_, samplesPerBeat);
    else
        startSpb_ = samplesPerBeat;
}

void PatternPlayer::steerToNearestBeat(std::int64_t sample, double samplesPerBeat) noexcept
{
    if (state_ != BandState::playing || !(samplesPerBeat > 0.0))
        return;
    clock_.steer(sample, std::round(clock_.beatAt(sample)), samplesPerBeat);
}

double PatternPlayer::bpm() const noexcept
{
    return 60.0 * sampleRate_ / samplesPerBeat();
}

void PatternPlayer::resetArrangement() noexcept
{
    currentBar_ = noBar;
    barKind_ = BarKind::none;
    sectionStartBar_ = 0;
    fillBar_ = noBar;
    nextSection_ = -1;
    endBar_ = noBar;
    entryThisBar_ = false;
}

void PatternPlayer::scheduleFill() noexcept
{
    if (fillBar_ != noBar)
        return; // one fill at a time; it is at or after the current bar
    const int bpb = style_->beatsPerBar;
    const double beat = clock_.beatAt(nextBlockStart_);
    const int bar = static_cast<int>(std::floor(beat / bpb));
    const double remaining = (bar + 1) * static_cast<double>(bpb) - beat;
    fillBar_ = remaining >= minFillBeats ? bar : bar + 1;
    if (fillBar_ == currentBar_ && barKind_ == BarKind::main)
        barKind_ = BarKind::fill; // the rest of this bar is the fill
}

void PatternPlayer::enterBar(int bar) noexcept
{
    currentBar_ = bar;
    entryThisBar_ = false;
    if (bar < 0)
    {
        barKind_ = BarKind::countIn;
        return;
    }
    if (bar == endBar_)
    {
        barKind_ = BarKind::ending;
        state_ = BandState::ending;
        startSection_ = section_;
        fillBar_ = noBar;
        return;
    }
    if (endBar_ != noBar && bar > endBar_)
    {
        barKind_ = BarKind::none;
        return;
    }
    if (fillBar_ != noBar && bar > fillBar_)
    {
        if (nextSection_ >= 0)
        {
            section_ = nextSection_;
            sectionStartBar_ = bar;
            entryThisBar_ = true;
        }
        nextSection_ = -1;
        fillBar_ = noBar;
    }
    if (bar == 0)
    {
        sectionStartBar_ = 0;
        entryThisBar_ = true;
    }
    barKind_ = bar == fillBar_ ? BarKind::fill : BarKind::main;
}

void PatternPlayer::render(std::int64_t blockStart, int numSamples) noexcept
{
    eventCount_ = 0;
    tickCount_ = 0;
    blockStart_ = blockStart;
    blockLength_ = numSamples;
    blockOffset_ = 0;
    nextBlockStart_ = blockStart + numSamples;

    if (flushRequested_)
    {
        releaseAll(0);
        flushRequested_ = false;
    }
    if (state_ == BandState::stopped || style_ == nullptr || numSamples <= 0)
        return;

    bool justStarted = false;
    if (state_ == BandState::waiting)
    {
        if (startSample_ >= blockStart + numSamples)
            return;
        if (startSample_ >= blockStart)
        {
            blockOffset_ = static_cast<int>(startSample_ - blockStart);
            clock_.start(startSample_, startBeat_, startSpb_);
        }
        else
        {
            // The start command arrived late: join the line where it is now.
            clock_.start(blockStart, startBeat_ + static_cast<double>(blockStart - startSample_) / startSpb_, startSpb_);
        }
        section_ = startSection_;
        state_ = BandState::playing;
        justStarted = true;
    }

    const auto span = clock_.advance(blockStart + blockOffset_, numSamples - blockOffset_);
    blockStartBeat_ = span.startBeat;
    blockRate_ = span.beatsPerSample;
    if (justStarted)
        scanFrom_ = span.startBeat - 0.5 * span.beatsPerSample;

    // The block owns the beats whose nearest sample is one of its own: the range ends half
    // a sample before the next block, and the next block continues exactly from there. So
    // every note is played once, on its nearest sample, whatever the rounding.
    const double b0 = scanFrom_;
    const double b1 = span.startBeat + span.beatsPerSample * (numSamples - blockOffset_ - 0.5);
    scanFrom_ = b1;

    const int bpb = style_->beatsPerBar;
    for (int bar = static_cast<int>(std::floor(b0 / bpb)); static_cast<double>(bar) * bpb < b1; ++bar)
    {
        if (bar != currentBar_)
            enterBar(bar);
        const double barStart = static_cast<double>(bar) * bpb;
        const double segmentStart = std::max(b0, barStart);
        const double segmentEnd = std::min(b1, barStart + bpb);
        playBarSegment(bar, segmentStart, segmentEnd);

        for (double beat = std::ceil(segmentStart); beat < segmentEnd; beat += 1.0)
        {
            const int beatInBar = static_cast<int>(beat - barStart);
            if (barKind_ == BarKind::none || (barKind_ == BarKind::ending && beatInBar > 0))
                continue;
            if (tickCount_ >= maxTicks)
                break;
            BeatTick tick;
            tick.sampleTime = blockStart_ + offsetOf(beat);
            tick.bar = bar;
            tick.beat = static_cast<std::int16_t>(beatInBar);
            tick.section = static_cast<std::int16_t>(section_);
            tick.bpm = static_cast<float>(bpm());
            if (barKind_ == BarKind::countIn)
                tick.flags |= BeatTick::countIn;
            if (barKind_ == BarKind::fill)
                tick.flags |= BeatTick::fill;
            if (barKind_ == BarKind::ending)
                tick.flags |= BeatTick::ending;
            if (entryThisBar_ && beatInBar == 0)
                tick.flags |= BeatTick::sectionStart;
            ticks_[static_cast<std::size_t>(tickCount_++)] = tick;
        }
    }
    releaseDue(b1);

    // Time order, and a note-off before a note-on at the same sample.
    for (int i = 1; i < eventCount_; ++i)
    {
        const PlayerNote item = events_[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && (events_[static_cast<std::size_t>(j)].offset > item.offset
                          || (events_[static_cast<std::size_t>(j)].offset == item.offset && events_[static_cast<std::size_t>(j)].on && !item.on)))
        {
            events_[static_cast<std::size_t>(j + 1)] = events_[static_cast<std::size_t>(j)];
            --j;
        }
        events_[static_cast<std::size_t>(j + 1)] = item;
    }

    // Stopped once the final hits have been played and released.
    if (state_ == BandState::ending && b1 > static_cast<double>(endBar_) * bpb && pendingCount_ == 0)
    {
        state_ = BandState::stopped;
        resetArrangement();
    }
}

void PatternPlayer::playBarSegment(int bar, double segmentStart, double segmentEnd) noexcept
{
    const double barStart = static_cast<double>(bar) * style_->beatsPerBar;
    const auto hits = [&](const std::vector<GrooveNote>& notes, bool ignoreMask) {
        playNotes(notes.data(), notes.data() + notes.size(), barStart, 0.0, segmentStart, segmentEnd, false, ignoreMask);
    };

    switch (barKind_)
    {
        case BarKind::none:
            return;
        case BarKind::countIn:
            hits(style_->countIn, true);
            return;
        case BarKind::ending:
            hits(style_->ending, false);
            return;
        case BarKind::main:
        case BarKind::fill:
            break;
    }

    if (entryThisBar_)
        hits(style_->entry, false);

    const Section& section = style_->sections[static_cast<std::size_t>(section_)];
    const bool isFill = barKind_ == BarKind::fill;
    const Pattern& pattern = isFill ? section.fill : section.main;
    const int bpb = style_->beatsPerBar;
    const int bars = std::max(pattern.bars(bpb), 1);
    const int barInPattern = isFill ? 0 : ((bar - sectionStartBar_) % bars + bars) % bars;
    const double patternOffset = static_cast<double>(barInPattern) * bpb;

    const auto byBeat = [](const GrooveNote& n, double beat) { return n.beat < beat; };
    const auto* first = std::lower_bound(pattern.notes.data(), pattern.notes.data() + pattern.notes.size(), patternOffset, byBeat);
    const auto* last = std::lower_bound(first, pattern.notes.data() + pattern.notes.size(), patternOffset + bpb, byBeat);
    playNotes(first, last, barStart, patternOffset, segmentStart, segmentEnd, true, false);
}

void PatternPlayer::playNotes(const GrooveNote* begin, const GrooveNote* end, double barStart, double patternOffset,
                              double segmentStart, double segmentEnd, bool swing, bool ignoreMask) noexcept
{
    for (const GrooveNote* n = begin; n != end; ++n)
    {
        double position = n->beat - patternOffset;
        if (swing)
            position = swingBeat(position, style_->swing);
        const double at = barStart + position;
        if (at < segmentStart)
            continue;
        if (at >= segmentEnd)
            break; // sorted, and swing keeps the order
        noteOn(at, *n, ignoreMask);
    }
}

void PatternPlayer::noteOn(double beat, const GrooveNote& note, bool ignoreMask) noexcept
{
    if (!ignoreMask && (layerMask_ & (1u << note.layer)) == 0)
        return;
    const int offset = offsetOf(beat);

    // A repeated note: release the earlier one first.
    for (int i = 0; i < pendingCount_; ++i)
    {
        auto& p = pending_[static_cast<std::size_t>(i)];
        if (p.note == note.note && p.layer == note.layer)
        {
            push({ offset, p.note, 0, p.layer, false });
            p = pending_[static_cast<std::size_t>(--pendingCount_)];
            break;
        }
    }

    push({ offset, note.note, note.velocity, note.layer, true });
    if (pendingCount_ < maxPending)
        pending_[static_cast<std::size_t>(pendingCount_++)] = { beat + note.lengthBeats, note.note, note.layer };
    else
        push({ offset, note.note, 0, note.layer, false }); // never leave a note hanging
}

void PatternPlayer::releaseDue(double untilBeat) noexcept
{
    for (int i = 0; i < pendingCount_;)
    {
        const auto p = pending_[static_cast<std::size_t>(i)];
        if (p.beat < untilBeat)
        {
            push({ offsetOf(p.beat), p.note, 0, p.layer, false });
            pending_[static_cast<std::size_t>(i)] = pending_[static_cast<std::size_t>(--pendingCount_)];
        }
        else
        {
            ++i;
        }
    }
}

void PatternPlayer::releaseAll(int offset) noexcept
{
    for (int i = 0; i < pendingCount_; ++i)
    {
        const auto& p = pending_[static_cast<std::size_t>(i)];
        push({ offset, p.note, 0, p.layer, false });
    }
    pendingCount_ = 0;
}

void PatternPlayer::push(const PlayerNote& note) noexcept
{
    if (eventCount_ < maxEvents)
        events_[static_cast<std::size_t>(eventCount_++)] = note;
    else
        ++droppedEvents_;
}

int PatternPlayer::offsetOf(double beat) const noexcept
{
    const auto samples = std::llround((beat - blockStartBeat_) / blockRate_);
    return blockOffset_ + static_cast<int>(std::clamp<long long>(samples, 0, blockLength_ - blockOffset_ - 1));
}

} // namespace lb
