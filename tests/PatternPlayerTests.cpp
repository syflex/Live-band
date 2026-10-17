#include "common/RtAllocGuard.h"
#include "music/PatternPlayer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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

    std::vector<std::int64_t> onsOf(int number) const
    {
        std::vector<std::int64_t> out;
        for (const auto& p : notes)
            if (p.on && p.note == number)
                out.push_back(p.time);
        return out;
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
            CHECK(std::abs((tick.sampleTime - previousTick) - 20000) <= 1742); // at most 8% slower: 20000 / 0.92
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
    const auto style = makeStyle();
    lb::PatternPlayer player;
    player.prepare(rate);
    player.setStyle(&style);
    const double spb = 48000.0 * 60.0 / 104.0;
    player.start(0, spb, 0, 0);

    std::mt19937 random(11);
    std::uniform_int_distribution<int> blockSize(16, 1024);
    std::uniform_int_distribution<int> action(0, 400);
    std::int64_t t = 0, worst = 0, ticks = 0;
    std::int64_t onCount = 0, offCount = 0;
    std::map<std::pair<int, int>, int> open;
    for (int number = 0; number < 128; ++number)
        for (int layer = 0; layer < 2; ++layer)
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
