#include "common/RtAllocGuard.h"
#include "music/PatternPlayer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <vector>

using Catch::Approx;

namespace
{
constexpr double rate = 48000.0;
constexpr int kick = 36, snare = 38, hat = 42, tom = 48, crash = 49, stick = 37, conga = 63;

lb::GrooveNote note(double beat, int number, int layer = 0, int velocity = 100)
{
    lb::GrooveNote n;
    n.beat = beat;
    n.note = static_cast<std::uint8_t>(number);
    n.layer = static_cast<std::uint8_t>(layer);
    n.velocity = static_cast<std::uint8_t>(velocity);
    return n;
}

// Two sections. "A" is two bars: kick on 1 and 3, snare on 2 and 4, hats in sixteenths on
// layer 1; its second bar adds a conga on beat 4. "B" is one bar of kicks on every beat.
// Each fill is four toms.
lb::Style makeStyle()
{
    lb::Style style;
    style.name = "Test";
    style.beatsPerBar = 4;
    style.defaultBpm = 120.0;
    style.minBpm = 60.0;
    style.maxBpm = 200.0;
    style.layers = { "drums", "perc" };

    lb::Section a;
    a.name = "A";
    a.main.lengthBeats = 8.0;
    for (int bar = 0; bar < 2; ++bar)
    {
        const double b = bar * 4.0;
        for (double beat : { 0.0, 2.0 })
            a.main.notes.push_back(note(b + beat, kick));
        for (double beat : { 1.0, 3.0 })
            a.main.notes.push_back(note(b + beat, snare));
        for (int i = 0; i < 16; ++i)
            a.main.notes.push_back(note(b + i * 0.25, hat, 1, 60));
    }
    a.main.notes.push_back(note(7.0, conga, 1));
    a.main.sort();
    a.fill.lengthBeats = 4.0;
    for (int i = 0; i < 4; ++i)
        a.fill.notes.push_back(note(i, tom));

    lb::Section b;
    b.name = "B";
    b.main.lengthBeats = 4.0;
    for (int i = 0; i < 4; ++i)
        b.main.notes.push_back(note(i, kick));
    b.fill = a.fill;

    style.sections = { a, b };
    for (int i = 0; i < 4; ++i)
        style.countIn.push_back(note(i, stick));
    style.entry.push_back(note(0.0, crash));
    style.ending = { note(0.0, kick, 0, 120), note(0.0, crash, 0, 110) };
    return style;
}

// makeStyle with a bass and a pad on layers 2 and 3, written in C. Section "A" has the bass
// on C2 on beat 1 and G2 on beat 3 of each bar, and a held pad chord with an E (C major); its
// minor version has an Eb instead. "B" has the bass on every beat.
constexpr int bassLayer = 2, padLayer = 3;
constexpr int bassC = 36, bassG = 43, padC = 60, padE = 64, padEb = 63;

lb::Style makeHarmonicStyle()
{
    auto style = makeStyle();
    style.layers = { "drums", "perc", "bass", "pad" };
    style.pitchedLayers = 1u << bassLayer | 1u << padLayer;
    style.fillLayers = 0b0011u;
    auto& a = style.sections[0];
    for (int bar = 0; bar < 2; ++bar)
    {
        a.main.notes.push_back(note(bar * 4.0, bassC, bassLayer));
        a.main.notes.push_back(note(bar * 4.0 + 2.0, bassG, bassLayer));
        for (int n : { padC, padE })
        {
            auto held = note(bar * 4.0, n, padLayer, 70);
            held.lengthBeats = 3.9f;
            a.main.notes.push_back(held);
        }
    }
    a.main.sort();
    a.mainMinor = a.main;
    for (auto& n : a.mainMinor.notes)
        if (n.layer == padLayer && n.note == padE)
            n.note = padEb;
    a.mainMinor.sort();
    for (int i = 0; i < 4; ++i)
        style.sections[1].main.notes.push_back(note(i, bassC, bassLayer));
    style.sections[1].main.sort();
    style.ending.push_back(note(0.0, bassC, bassLayer));
    return style;
}

lb::Key key(int tonic, bool minor = false)
{
    return { tonic, minor };
}

struct Played
{
    std::int64_t time = 0;
    int note = 0;
    bool on = false;
    int layer = 0;
};

struct Recorder
{
    std::vector<Played> notes;
    std::vector<lb::BeatTick> ticks;

    void run(lb::PatternPlayer& player, std::int64_t& t, std::int64_t until, int blockSize = 128)
    {
        while (t < until)
        {
            const int n = static_cast<int>(std::min<std::int64_t>(blockSize, until - t));
            int lastOffset = -1;
            player.process(t, n,
                [&](const lb::PlayerNote& e) {
                    REQUIRE(e.offset >= lastOffset); // time order within the block
                    REQUIRE(e.offset < n);
                    lastOffset = e.offset;
                    notes.push_back({ t + e.offset, e.note, e.on, e.layer });
                },
                [&](const lb::BeatTick& tick) { ticks.push_back(tick); });
            t += n;
        }
    }

    std::vector<std::int64_t> onsOf(int number, int layer = -1) const
    {
        std::vector<std::int64_t> out;
        for (const auto& p : notes)
            if (p.on && p.note == number && (layer < 0 || p.layer == layer))
                out.push_back(p.time);
        return out;
    }

    // The notes switched on on a layer, in [from, to), in time order.
    std::vector<int> layerNotes(int layer, std::int64_t from = 0, std::int64_t to = std::numeric_limits<std::int64_t>::max()) const
    {
        std::vector<int> out;
        for (const auto& p : notes)
            if (p.on && p.layer == layer && p.time >= from && p.time < to)
                out.push_back(p.note);
        return out;
    }

    std::int64_t firstOn(int layer) const
    {
        for (const auto& p : notes)
            if (p.on && p.layer == layer)
                return p.time;
        return -1;
    }

    // Every note-on is followed by its note-off.
    bool balanced() const
    {
        std::map<std::pair<int, int>, int> open;
        for (const auto& p : notes)
        {
            auto& count = open[{ p.note, p.layer }];
            count += p.on ? 1 : -1;
            if (count < 0 || count > 1)
                return false;
        }
        return std::all_of(open.begin(), open.end(), [](const auto& kv) { return kv.second == 0; });
    }
};

std::int64_t beatSample(std::int64_t downbeat, double spb, double beat)
{
    return downbeat + std::llround(beat * spb);
}
} // namespace

TEST_CASE("PatternPlayer places every note on its sample, with the entry crash on the first downbeat")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);

    constexpr double spb = 24000.0; // 120 BPM
    constexpr std::int64_t downbeat = 10007;
    player.start(downbeat, spb, 0, 0);
    CHECK(player.state() == lb::BandState::waiting);

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, downbeat + static_cast<std::int64_t>(8 * spb), 100);
    CHECK(player.state() == lb::BandState::playing);

    CHECK(r.onsOf(crash) == std::vector<std::int64_t> { downbeat });
    CHECK(r.onsOf(kick) == std::vector<std::int64_t> { downbeat, beatSample(downbeat, spb, 2), beatSample(downbeat, spb, 4), beatSample(downbeat, spb, 6) });
    CHECK(r.onsOf(snare).size() == 4);
    CHECK(r.onsOf(hat).size() == 32);
    CHECK(r.onsOf(conga) == std::vector<std::int64_t> { beatSample(downbeat, spb, 7) });

    REQUIRE(r.ticks.size() == 8);
    for (std::size_t i = 0; i < r.ticks.size(); ++i)
    {
        CHECK(r.ticks[i].sampleTime == beatSample(downbeat, spb, static_cast<double>(i)));
        CHECK(r.ticks[i].bar == static_cast<int>(i / 4));
        CHECK(r.ticks[i].beat == static_cast<int>(i % 4));
        CHECK(r.ticks[i].bpm == Approx(120.0f));
    }
    CHECK((r.ticks[0].flags & lb::BeatTick::sectionStart) != 0);
    CHECK((r.ticks[4].flags & lb::BeatTick::sectionStart) == 0); // the pattern loops without a new crash
}

TEST_CASE("PatternPlayer counts in for one bar before the downbeat")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);

    constexpr double spb = 24000.0;
    constexpr std::int64_t downbeat = 200000;
    player.start(downbeat, spb, 0, 1);

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, downbeat + 1000);
    CHECK(r.onsOf(stick) == std::vector<std::int64_t> { downbeat - 96000, downbeat - 72000, downbeat - 48000, downbeat - 24000 });
    CHECK(r.onsOf(kick) == std::vector<std::int64_t> { downbeat });
    REQUIRE(r.ticks.size() == 5);
    CHECK(r.ticks[0].bar == -1);
    CHECK((r.ticks[0].flags & lb::BeatTick::countIn) != 0);
    CHECK(r.ticks[4].bar == 0);
}

TEST_CASE("PatternPlayer: the next section starts on the downbeat after a fill from the current beat")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);

    constexpr double spb = 24000.0;
    player.start(0, spb, 0, 0);
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, static_cast<std::int64_t>(4 * spb + 1.2 * spb)); // bar 1, beat 1.2

    player.nextSection();
    CHECK(player.pendingSection() == 1);
    CHECK(player.fillScheduled());
    r.run(player, t, static_cast<std::int64_t>(12 * spb));

    // The fill's toms on beats 2 and 3 of bar 1 (beats 0 and 1 had passed); then section B
    // from bar 2, kicks on every beat, with a crash on its first downbeat.
    CHECK(r.onsOf(tom) == std::vector<std::int64_t> { 6 * 24000, 7 * 24000 });
    const auto crashes = r.onsOf(crash);
    CHECK(crashes == std::vector<std::int64_t> { 0, 8 * 24000 });
    CHECK(player.section() == 1);
    CHECK(player.pendingSection() == -1);
    const auto kicks = r.onsOf(kick);
    for (int beat = 8; beat < 12; ++beat)
        CHECK(std::count(kicks.begin(), kicks.end(), beat * 24000) == 1);
    CHECK(r.onsOf(snare).size() == 3); // bar 0 had 2; bar 1 had one before the fill took over

    const auto sectionStart = std::find_if(r.ticks.begin(), r.ticks.end(), [](const lb::BeatTick& tick) { return tick.bar == 2 && tick.beat == 0; });
    REQUIRE(sectionStart != r.ticks.end());
    CHECK(sectionStart->section == 1);
    CHECK((sectionStart->flags & lb::BeatTick::sectionStart) != 0);
    CHECK(r.balanced());
}

TEST_CASE("PatternPlayer: a request in the last half beat plays the fill in the next bar")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);

    constexpr double spb = 24000.0;
    player.start(0, spb, 0, 0);
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, static_cast<std::int64_t>(3.6 * spb));
    player.fill();
    r.run(player, t, static_cast<std::int64_t>(12 * spb));

    CHECK(r.onsOf(tom) == std::vector<std::int64_t> { 4 * 24000, 5 * 24000, 6 * 24000, 7 * 24000 });
    CHECK(player.section() == 0); // a fill alone does not change the section
    CHECK(r.onsOf(crash).size() == 1); // and adds no entry crash
}

TEST_CASE("PatternPlayer stops with a fill and the ending hits, releasing every note")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);

    constexpr double spb = 24000.0;
    player.start(0, spb, 0, 0);
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, static_cast<std::int64_t>(5.0 * spb));
    player.stop();
    r.run(player, t, static_cast<std::int64_t>(16 * spb));

    CHECK(player.state() == lb::BandState::stopped);
    CHECK(r.onsOf(tom) == std::vector<std::int64_t> { 5 * 24000, 6 * 24000, 7 * 24000 });
    CHECK(r.onsOf(crash) == std::vector<std::int64_t> { 0, 8 * 24000 });
    const auto kicks = r.onsOf(kick);
    CHECK(kicks.back() == 8 * 24000);
    CHECK(std::none_of(r.notes.begin(), r.notes.end(), [](const Played& p) { return p.on && p.time > 8 * 24000; }));
    CHECK(r.balanced());
    CHECK(player.sustainedNotes() == 0);
    REQUIRE(!r.ticks.empty());
    CHECK((r.ticks.back().flags & lb::BeatTick::ending) != 0);
}

TEST_CASE("PatternPlayer: stopNow releases every sounding note in the next block")
{
    auto style = makeStyle();
    for (auto& n : style.sections[0].main.notes)
        n.lengthBeats = 1.5f; // long notes, still sounding when stopped
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    player.start(0, 24000.0, 0, 0);

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, 30000);
    CHECK(player.sustainedNotes() > 0);
    CHECK(!r.balanced());
    player.stopNow();
    r.run(player, t, 30128);
    CHECK(r.balanced());
    CHECK(player.sustainedNotes() == 0);
    CHECK(player.state() == lb::BandState::stopped);
}

TEST_CASE("PatternPlayer: a muted layer plays nothing new, but its notes are still released")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    player.start(0, 24000.0, 0, 0);

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, 1000); // the first hat is on
    player.setLayerMask(1u << 0);
    r.run(player, t, 96000);
    CHECK(r.onsOf(hat).size() == 1);
    CHECK(r.onsOf(kick).size() == 2);
    CHECK(r.balanced());
}

TEST_CASE("PatternPlayer swings the off-sixteenths")
{
    auto style = makeStyle();
    style.swing = 0.6;
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    player.start(0, 24000.0, 0, 0);

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, 24000);
    // Beat 0.25 moves to 0.30 and 0.75 to 0.80; the eighths stay put.
    CHECK(r.onsOf(hat) == std::vector<std::int64_t> { 0, 7200, 12000, 19200 });
}

TEST_CASE("PatternPlayer: a tempo change keeps the bar running, and steering moves the phase smoothly")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    player.start(0, 24000.0, 0, 0);

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, 48000); // beat 2
    player.setTempo(20000.0);
    r.run(player, t, 48000 + 40000 + 10);
    CHECK(player.bpm() == Approx(144.0));
    // Snare on beat 3 is one new beat later; kick on beat 4 two.
    CHECK(r.onsOf(snare).back() == 48000 + 20000);
    CHECK(r.onsOf(kick).back() == 48000 + 40000);

    // Steer: tell the band the beat falls 3000 samples later than it plays it now.
    const std::int64_t anchor = t + 3000;
    player.steerToNearestBeat(anchor, 20000.0);
    std::int64_t previousTick = 0;
    r.ticks.clear();
    r.run(player, t, t + 40 * 20000);
    for (const auto& tick : r.ticks)
    {
        CHECK(tick.sampleTime > previousTick);
        if (previousTick > 0)
            CHECK(std::abs((tick.sampleTime - previousTick) - 20000) <= 3530); // at most 15% slower: 20000 / 0.85
        previousTick = tick.sampleTime;
    }
    // By now the beats sit on the new line, within a sample.
    const auto last = r.ticks.back().sampleTime;
    CHECK(std::abs(std::remainder(static_cast<double>(last - anchor), 20000.0)) <= 1.0);
}

TEST_CASE("PatternPlayer joins at the right phase when the start command arrives late")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    player.start(1000, 24000.0, 0, 0); // the downbeat was at sample 1000

    Recorder r;
    std::int64_t t = 5000;
    r.run(player, t, 5000 + 48000);
    CHECK(r.onsOf(snare) == std::vector<std::int64_t> { 25000 });
    CHECK(r.onsOf(kick) == std::vector<std::int64_t> { 49000 });
}

TEST_CASE("PatternPlayer keeps time over 30 minutes and never leaves a note hanging")
{
    const auto style = makeHarmonicStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    const double spb = 48000.0 * 60.0 / 104.0;
    player.start(0, spb, 0, 0, key(7));

    std::mt19937 random(11);
    std::uniform_int_distribution<int> blockSize(16, 1024);
    std::uniform_int_distribution<int> action(0, 400);
    std::uniform_int_distribution<int> anyKey(0, 23);
    std::int64_t t = 0, worst = 0, ticks = 0;
    std::int64_t onCount = 0, offCount = 0;
    std::map<std::pair<int, int>, int> open;
    for (int number = 0; number < 128; ++number)
        for (int layer = 0; layer < 4; ++layer)
            open[{ number, layer }] = 0; // so that counting in the audio section does not allocate
    bool balanced = true;
    const auto before = lb::rt::allocationCount();
    const std::int64_t end = static_cast<std::int64_t>(30 * 60 * rate);
    while (t < end)
    {
        const int n = blockSize(random);
        {
            const lb::rt::ScopedRealtimeSection realtime;
            switch (action(random))
            {
                case 0: player.nextSection(); break;
                case 1: player.fill(); break;
                case 2: player.setKey(lb::Key::fromIndex(anyKey(random))); break;
                default: break;
            }
            player.process(t, n,
                [&](const lb::PlayerNote& e) {
                    auto& count = open[{ e.note, e.layer }];
                    count += e.on ? 1 : -1;
                    balanced = balanced && count >= 0 && count <= 1;
                    (e.on ? onCount : offCount) += 1;
                },
                [&](const lb::BeatTick& tick) {
                    ++ticks;
                    const double beat = tick.bar * 4.0 + tick.beat;
                    worst = std::max<std::int64_t>(worst, std::abs(tick.sampleTime - static_cast<std::int64_t>(std::llround(beat * spb))));
                });
        }
        t += n;
    }
    CHECK(lb::rt::allocationCount() == before);
    CHECK(worst <= 1); // the last beat, after 30 minutes, within a sample of the ideal
    CHECK(ticks == static_cast<std::int64_t>(std::floor((static_cast<double>(t) - 0.5) / spb)) + 1); // it played every beat
    player.stop();
    for (int i = 0; i < 2000 && player.state() != lb::BandState::stopped; ++i, t += 256)
        player.process(t, 256, [&](const lb::PlayerNote& e) { (e.on ? onCount : offCount) += 1; open[{ e.note, e.layer }] += e.on ? 1 : -1; },
                       [](const lb::BeatTick&) {});
    CHECK(player.state() == lb::BandState::stopped);
    CHECK(balanced);
    CHECK(onCount == offCount);
    CHECK(onCount > 30 * 104 * 2);
    CHECK(player.droppedEvents() == 0);
}

TEST_CASE("PatternPlayer: 30 minutes of re-taps, loudness, staged entries and restarts leave no note hanging and play every beat once")
{
    auto style = makeHarmonicStyle();
    auto& a = style.sections[0];
    a.mainIntense = a.main; // the intense variation holds other notes, across the bar line
    for (auto& n : a.mainIntense.notes)
        if (n.layer == padLayer)
            n.lengthBeats = 5.9f;
    a.mainIntense.notes.push_back(note(1.5, bassG, bassLayer));
    a.mainIntense.sort();
    style.entryKeyKnown = { { 0, 1u << padLayer }, { 4, 0b0011u }, { 8, 1u << bassLayer } };
    style.entryKeyUnknown = { { 0, 0b0011u } };
    style.afterKeyLockLayers = 1u << bassLayer | 1u << padLayer;
    REQUIRE(style.validate().empty());

    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    const double baseSpb = 48000.0 * 60.0 / 104.0;

    std::mt19937 random(12);
    std::uniform_int_distribution<int> blockSize(16, 1024);
    std::uniform_int_distribution<int> action(0, 300);
    std::uniform_int_distribution<int> anyKey(-1, 23);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::int64_t t = 0, previousTick = -1, ticks = 0, starts = 0, intenseBeats = 0, onCount = 0, offCount = 0;
    std::int64_t previousBeat = std::numeric_limits<std::int64_t>::min();
    std::map<std::pair<int, int>, int> open;
    for (int number = 0; number < 128; ++number)
        for (int layer = 0; layer < 4; ++layer)
            open[{ number, layer }] = 0; // so that counting in the audio section does not allocate
    bool balanced = true, everyBeatOnce = true, steady = true;
    const auto onNote = [&](const lb::PlayerNote& e) {
        auto& count = open[{ e.note, e.layer }];
        count += e.on ? 1 : -1;
        balanced = balanced && count >= 0 && count <= 1;
        (e.on ? onCount : offCount) += 1;
    };
    const auto onTick = [&](const lb::BeatTick& tick) {
        ++ticks;
        intenseBeats += (tick.flags & lb::BeatTick::intense) != 0 ? 1 : 0;
        // Each beat follows the one before it, never skipped or played twice, and comes no more
        // than 15% early or late for the tempos used (re-taps spread their correction).
        const std::int64_t beat = static_cast<std::int64_t>(tick.bar) * 4 + tick.beat;
        everyBeatOnce = everyBeatOnce && (previousBeat == std::numeric_limits<std::int64_t>::min() || beat == previousBeat + 1);
        if (previousTick >= 0 && previousBeat != std::numeric_limits<std::int64_t>::min())
        {
            const auto interval = static_cast<double>(tick.sampleTime - previousTick);
            steady = steady && interval > 0.9 * baseSpb / 1.15 - 1.0 && interval < 1.1 * baseSpb / 0.85 + 1.0;
        }
        previousBeat = beat;
        previousTick = tick.sampleTime;
    };
    const auto start = [&] {
        previousBeat = std::numeric_limits<std::int64_t>::min();
        previousTick = -1;
        const auto spb = baseSpb * (0.9 + 0.2 * unit(random));
        const int countIn = unit(random) < 0.5 ? 1 : 0;
        const int k = anyKey(random); // -1: no key yet
        player.start(t + 1000 + static_cast<std::int64_t>(countIn * 4 * spb), spb, 0, countIn, k < 0 ? lb::Key {} : lb::Key::fromIndex(k));
        ++starts;
    };
    start();

    const auto before = lb::rt::allocationCount();
    const std::int64_t end = static_cast<std::int64_t>(30 * 60 * rate);
    bool stopping = false;
    while (t < end)
    {
        const int n = blockSize(random);
        {
            const lb::rt::ScopedRealtimeSection realtime;
            if (stopping && player.state() == lb::BandState::stopped)
            {
                stopping = false;
                start();
            }
            else if (!stopping)
            {
                switch (action(random))
                {
                    case 0: player.nextSection(); break;
                    case 1: player.fill(); break;
                    case 2: player.setKey(lb::Key::fromIndex(std::max(0, anyKey(random)))); break;
                    case 3: // four re-taps: a beat somewhere in the last beat, at a tempo within 10%
                    case 4: player.steerToNearestBeat(t - static_cast<std::int64_t>(unit(random) * baseSpb), baseSpb * (0.9 + 0.2 * unit(random))); break;
                    case 5:
                    case 6: player.setDynamics(static_cast<float>(0.8 + 0.35 * unit(random)), unit(random) < 0.5); break;
                    case 7: player.advanceEntry(); break;
                    case 8:
                        if (unit(random) < 0.1) // about every half minute: stop, and start again
                        {
                            player.stop();
                            stopping = true;
                        }
                        break;
                    default: break;
                }
            }
            player.process(t, n, onNote, onTick);
        }
        t += n;
    }
    CHECK(lb::rt::allocationCount() == before);
    // Stopped during a count-in, the band releases its notes in the next block, as the engine
    // renders a block in every callback.
    player.stop();
    for (int i = 0; i < 4000 && (i == 0 || player.state() != lb::BandState::stopped); ++i, t += 256)
        player.process(t, 256, onNote, onTick);
    CHECK(player.state() == lb::BandState::stopped);
    CHECK(starts > 5);
    CHECK(intenseBeats > 1000);
    CHECK(ticks > 30 * 104 * 3 / 4);
    CHECK(everyBeatOnce);
    CHECK(steady);
    CHECK(balanced);
    CHECK(onCount == offCount);
    CHECK(player.sustainedNotes() == 0);
    CHECK(player.droppedEvents() == 0);
}

TEST_CASE("PatternPlayer moves the pitched layers to the key, in their register, and plays minor keys in minor")
{
    const auto style = makeHarmonicStyle();
    constexpr double spb = 24000.0;
    for (int index = 0; index < 24; ++index)
    {
        const auto k = lb::Key::fromIndex(index);
        lb::PatternPlayer player;
        player.prepare(rate);
        player.setStyle(&style);
        player.start(0, spb, 0, 0, k);
        Recorder r;
        std::int64_t t = 0;
        r.run(player, t, static_cast<std::int64_t>(4 * spb));

        const int shift = lb::PatternPlayer::transposition(k);
        CHECK(shift >= -6);
        CHECK(shift <= 5);
        CHECK(((shift % 12) + 12) % 12 == k.tonic);
        CHECK(r.layerNotes(bassLayer) == std::vector<int> { bassC + shift, bassG + shift });
        auto pad = r.layerNotes(padLayer);
        std::sort(pad.begin(), pad.end());
        CHECK(pad == std::vector<int> { padC + shift, (k.minor ? padEb : padE) + shift });
        CHECK(r.onsOf(kick, 0).size() == 2); // the drums are never moved
        CHECK(r.onsOf(crash).size() == 1);
    }
}

TEST_CASE("PatternPlayer: without a key the harmony waits; a key change takes effect on the next downbeat")
{
    const auto style = makeHarmonicStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    constexpr double spb = 24000.0;
    const auto bar = [&](double b) { return static_cast<std::int64_t>(b * 4 * spb); };
    player.start(0, spb, 0, 0);

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, bar(0.4));
    CHECK(r.layerNotes(bassLayer).empty()); // no key, no bass
    CHECK(r.layerNotes(padLayer).empty());
    CHECK(r.onsOf(kick, 0).size() == 1);    // but the drums play

    player.setKey(key(7)); // G major, in the middle of bar 0
    CHECK(player.key() == key(7));
    CHECK_FALSE(player.playingKey().isValid());
    r.run(player, t, bar(1.4));
    CHECK(r.layerNotes(bassLayer, 0, bar(1)).empty());
    CHECK(r.layerNotes(bassLayer, bar(1), bar(2)) == std::vector<int> { bassC - 5 });
    CHECK(r.firstOn(bassLayer) == bar(1));
    CHECK(player.playingKey() == key(7));

    // A minor, while the G major pad chord is held: the chord is released at its own end,
    // and the next bar starts in A minor.
    player.setKey(key(9, true));
    r.run(player, t, bar(3));
    CHECK(r.layerNotes(bassLayer, bar(1), bar(2)) == std::vector<int> { bassC - 5, bassG - 5 });
    auto pad = r.layerNotes(padLayer, bar(2), bar(3));
    std::sort(pad.begin(), pad.end());
    CHECK(pad == std::vector<int> { padC - 3, padEb - 3 });
    CHECK(r.layerNotes(bassLayer, bar(2), bar(3)) == std::vector<int> { bassC - 3, bassG - 3 });

    player.stopNow();
    r.run(player, t, t + 256);
    CHECK(r.balanced());
    CHECK(player.sustainedNotes() == 0);
}

TEST_CASE("PatternPlayer: a fill replaces only the fill layers; the harmony carries on")
{
    const auto style = makeHarmonicStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    constexpr double spb = 24000.0;
    player.start(0, spb, 0, 0, key(0));

    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, static_cast<std::int64_t>(1.0 * spb));
    player.fill();
    r.run(player, t, static_cast<std::int64_t>(4.0 * spb));

    CHECK(r.onsOf(kick, 0) == std::vector<std::int64_t> { 0 });                     // the kick on 3 gave way to the fill
    CHECK(r.onsOf(tom).size() == 3);                                                // the fill's beats 2 to 4
    CHECK(r.layerNotes(bassLayer) == std::vector<int> { bassC, bassG });            // the bass kept playing
    CHECK(r.onsOf(bassG) == std::vector<std::int64_t> { beatSample(0, spb, 2) });
}

TEST_CASE("PatternPlayer: staged entry with the key known brings the drums in later, with the entry hits")
{
    auto style = makeHarmonicStyle();
    style.entryKeyKnown = { { 0, 1u << padLayer }, { 1, 0b0011u }, { 2, 1u << bassLayer } };
    style.entryKeyUnknown = { { 0, 0b0011u } };
    style.afterKeyLockLayers = 1u << bassLayer | 1u << padLayer;
    style.afterKeyLockEveryBars = 2;
    REQUIRE(style.validate().empty());

    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    constexpr double spb = 24000.0;
    const auto bar = [&](double b) { return static_cast<std::int64_t>(b * 4 * spb); };
    player.start(1000, spb, 0, 0, key(2)); // D major, known at the start
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, 1000 + bar(3));

    CHECK(r.firstOn(padLayer) == 1000);
    CHECK(r.onsOf(kick, 0).front() == 1000 + bar(1));
    CHECK(r.onsOf(crash) == std::vector<std::int64_t> { 1000 + bar(1) }); // the entry crash comes with the drums
    CHECK(r.firstOn(bassLayer) == 1000 + bar(2));
    CHECK(r.layerNotes(bassLayer).front() == bassC + 2);
    REQUIRE(r.ticks.size() == 12);
    CHECK((r.ticks[0].flags & lb::BeatTick::sectionStart) != 0);
    CHECK((r.ticks[4].flags & lb::BeatTick::sectionStart) != 0);
    CHECK((r.ticks[8].flags & lb::BeatTick::sectionStart) == 0); // the bass brings no crash
    player.stopNow();
    r.run(player, t, t + 256);
    CHECK(r.balanced());
}

TEST_CASE("PatternPlayer: staged entry without a key brings the harmony in on a boundary after the key arrives")
{
    auto style = makeHarmonicStyle();
    style.entryKeyKnown = { { 0, 1u << padLayer }, { 1, 0b0011u }, { 2, 1u << bassLayer } };
    style.entryKeyUnknown = { { 0, 0b0011u } };
    style.afterKeyLockLayers = 1u << bassLayer | 1u << padLayer;
    style.afterKeyLockEveryBars = 2;
    constexpr double spb = 24000.0;
    const auto bar = [&](double b) { return static_cast<std::int64_t>(b * 4 * spb); };

    SECTION("the key arrives while the band plays")
    {
        lb::PatternPlayer player;
        player.prepare(rate);
        player.setStyle(&style);
        player.start(0, spb, 0, 0);
        Recorder r;
        std::int64_t t = 0;
        r.run(player, t, bar(2.5));
        CHECK(r.onsOf(kick, 0).front() == 0); // the drums from the start
        CHECK(r.layerNotes(bassLayer).empty());
        CHECK(player.activeLayers() == 0b0011u);

        player.setKey(key(4, true)); // E minor, heard in bar 2: in from bar 4, not bar 3
        r.run(player, t, bar(5));
        CHECK(player.playingKey() == key(4, true));
        CHECK(r.firstOn(bassLayer) == bar(4));
        CHECK(r.firstOn(padLayer) == bar(4));
        CHECK(r.layerNotes(bassLayer).front() == bassC + 4);
        CHECK(r.onsOf(crash) == std::vector<std::int64_t> { 0 }); // the drums were already in
        CHECK(player.activeLayers() == 0b1111u);
    }

    SECTION("the key arrives before the first downbeat: the band enters as if it had been known")
    {
        lb::PatternPlayer player;
        player.prepare(rate);
        player.setStyle(&style);
        player.start(bar(1), spb, 0, 0);
        player.setKey(key(9));
        Recorder r;
        std::int64_t t = 0;
        r.run(player, t, bar(4));
        CHECK(r.firstOn(padLayer) == bar(1));
        CHECK(r.onsOf(kick, 0).front() == bar(2));
        CHECK(r.firstOn(bassLayer) == bar(3));
    }
}

TEST_CASE("PatternPlayer: the ending hits sound in the key and every note is released")
{
    const auto style = makeHarmonicStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    constexpr double spb = 24000.0;
    player.start(0, spb, 0, 0, key(10)); // Bb major: down two semitones
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, static_cast<std::int64_t>(4.2 * spb));
    player.stop();
    for (int i = 0; i < 400 && player.state() != lb::BandState::stopped; ++i)
        r.run(player, t, t + 512);
    CHECK(player.state() == lb::BandState::stopped);
    CHECK(r.layerNotes(bassLayer).back() == bassC - 2); // the final bass note, in Bb
    CHECK(r.balanced());
}

TEST_CASE("PatternPlayer reports the tempo it will start at while stopped, and the last tempo after it stops")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    CHECK(player.bpm() == Approx(120.0)); // the style's default
    player.setTempo(rate * 60.0 / 100.0);
    CHECK(player.bpm() == Approx(100.0));

    player.start(0, rate * 60.0 / 100.0, 0, 0);
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, 48000);
    player.setTempo(rate * 60.0 / 110.0);
    r.run(player, t, 96000);
    player.stopNow();
    r.run(player, t, 96256);
    CHECK(player.state() == lb::BandState::stopped);
    CHECK(player.bpm() == Approx(110.0));
}

TEST_CASE("PatternPlayer scales velocities by the singer's dynamics, but not the count-in")
{
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    player.setDynamics(1.1f, false);
    player.start(96000, 24000.0, 0, 1);

    std::vector<int> stickVelocities, kickVelocities;
    std::int64_t t = 0;
    while (t < 96000 + 4 * 24000)
    {
        player.process(t, 256,
            [&](const lb::PlayerNote& e) {
                if (e.on && e.note == stick)
                    stickVelocities.push_back(e.velocity);
                if (e.on && e.note == kick)
                    kickVelocities.push_back(e.velocity);
            },
            [](const lb::BeatTick&) {});
        t += 256;
    }
    REQUIRE(stickVelocities.size() == 4);
    CHECK(stickVelocities.front() == 100); // the count-in as written
    REQUIRE(!kickVelocities.empty());
    CHECK(kickVelocities.front() == 110);  // 100, louder by 10%
}

TEST_CASE("PatternPlayer starts the intense variation on the next downbeat with a crash, and ends it on a downbeat")
{
    auto style = makeHarmonicStyle();
    constexpr int tambourine = 54;
    auto& a = style.sections[0];
    a.mainIntense = a.main;
    for (int bar = 0; bar < 2; ++bar)
        a.mainIntense.notes.push_back(note(bar * 4.0 + 1.5, tambourine, 1));
    a.mainIntense.sort();
    REQUIRE(style.validate().empty());

    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    constexpr double spb = 24000.0;
    const auto bar = [&](double b) { return static_cast<std::int64_t>(b * 4 * spb); };
    player.start(0, spb, 0, 0, key(0));
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, bar(0.5));
    player.setDynamics(1.0f, true); // loud for two bars: from the next downbeat
    CHECK_FALSE(player.intense());
    r.run(player, t, bar(2.5));
    CHECK(player.intense());
    player.setDynamics(1.0f, false);
    r.run(player, t, bar(4));

    CHECK(r.onsOf(tambourine) == std::vector<std::int64_t> { beatSample(0, spb, 5.5), beatSample(0, spb, 9.5) }); // bars 1 and 2 only
    CHECK(r.onsOf(crash) == std::vector<std::int64_t> { 0, bar(1) });  // the start, and the intense downbeat; none when it ends
    REQUIRE(r.ticks.size() == 16);
    CHECK((r.ticks[0].flags & lb::BeatTick::intense) == 0);
    CHECK((r.ticks[4].flags & lb::BeatTick::intense) != 0);
    CHECK((r.ticks[12].flags & lb::BeatTick::intense) == 0);
}

TEST_CASE("PatternPlayer: advancing the staged entry brings the next step in on the next downbeat")
{
    auto style = makeHarmonicStyle();
    style.entryKeyKnown = { { 0, 1u << padLayer }, { 4, 0b0011u }, { 8, 1u << bassLayer } };
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    constexpr double spb = 24000.0;
    const auto bar = [&](double b) { return static_cast<std::int64_t>(b * 4 * spb); };
    player.start(0, spb, 0, 0, key(0));
    Recorder r;
    std::int64_t t = 0;
    r.run(player, t, bar(1.5));
    player.advanceEntry(); // the drums come in on bar 2 instead of bar 4
    r.run(player, t, bar(2.5));
    player.advanceEntry(); // and the bass on bar 3 instead of bar 8
    r.run(player, t, bar(4));
    CHECK(r.onsOf(kick, 0).front() == bar(2));
    CHECK(r.onsOf(crash).front() == bar(2)); // with the entry crash
    CHECK(r.firstOn(bassLayer) == bar(3));
}
