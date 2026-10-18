#include "music/KeyEstimator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Approx;

namespace
{
constexpr double hop = 0.008; // the analysis frame rate

struct Note
{
    double midi; // 0 for a rest
    double seconds;
};

// Feeds a melody to the estimator frame by frame, sung with vibrato (+-25 cents at 5.5 Hz)
// and an overall tuning offset, and returns the time after the last frame.
double sing(lb::KeyEstimator& estimator, const std::vector<Note>& melody, double start = 0.0, double tuningCents = 0.0,
            double vibratoCents = 25.0)
{
    double t = start;
    for (const auto& note : melody)
    {
        const double end = t + note.seconds;
        for (; t < end; t += hop)
        {
            const double vibrato = vibratoCents / 100.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t);
            if (note.midi > 0.0)
                estimator.process(t, true, note.midi + tuningCents / 100.0 + vibrato, -20.0f);
            else
                estimator.process(t, false, 0.0, -80.0f);
        }
    }
    return t;
}

// "Twinkle, twinkle" in D major, two phrases, each ending on a long note.
const std::vector<Note> twinkleD {
    { 62, 0.45 }, { 62, 0.45 }, { 69, 0.45 }, { 69, 0.45 }, { 71, 0.45 }, { 71, 0.45 }, { 69, 0.9 }, { 0, 0.4 },
    { 67, 0.45 }, { 67, 0.45 }, { 66, 0.45 }, { 66, 0.45 }, { 64, 0.45 }, { 64, 0.45 }, { 62, 0.9 }, { 0, 0.4 },
};

// A tune in E minor (natural minor) that dwells on E and B and ends each phrase on E.
const std::vector<Note> tuneEminor {
    { 64, 0.4 }, { 67, 0.4 }, { 71, 0.6 }, { 69, 0.3 }, { 67, 0.3 }, { 66, 0.3 }, { 64, 0.9 }, { 0, 0.4 },
    { 67, 0.4 }, { 69, 0.4 }, { 71, 0.4 }, { 72, 0.4 }, { 71, 0.4 }, { 69, 0.3 }, { 66, 0.3 }, { 64, 1.0 }, { 0, 0.4 },
};
} // namespace

TEST_CASE("Key names and the MIREX score follow the usual conventions")
{
    CHECK(lb::Key { 7, false }.name() == "G major");
    CHECK(lb::Key { 3, true }.shortName() == "Ebm");
    CHECK(lb::Key::fromIndex(14) == lb::Key { 2, true });
    CHECK(lb::Key { 10, false }.transposed(3) == lb::Key { 1, false });

    const lb::Key cMajor { 0, false };
    CHECK(lb::mirexKeyScore(cMajor, cMajor) == 1.0);
    CHECK(lb::mirexKeyScore({ 7, false }, cMajor) == 0.5);  // a fifth above
    CHECK(lb::mirexKeyScore({ 5, false }, cMajor) == 0.0);  // a fifth below does not count
    CHECK(lb::mirexKeyScore({ 9, true }, cMajor) == 0.3);   // relative minor
    CHECK(lb::mirexKeyScore(cMajor, { 9, true }) == 0.3);   // relative major
    CHECK(lb::mirexKeyScore({ 0, true }, cMajor) == 0.2);   // parallel
    CHECK(lb::mirexKeyScore({ 2, false }, cMajor) == 0.0);
}

TEST_CASE("Every profile recognizes all 24 keys from its own shape")
{
    for (auto profile : { lb::KeyProfile::krumhanslKessler, lb::KeyProfile::aardenEssen, lb::KeyProfile::temperleyKostkaPayne,
                          lb::KeyProfile::bellmanBudge })
    {
        for (int index = 0; index < 24; ++index)
        {
            const auto key = lb::Key::fromIndex(index);
            const auto& weights = lb::profileWeights(profile, key.minor);
            std::array<double, 12> histogram {};
            for (int pc = 0; pc < 12; ++pc)
                histogram[static_cast<std::size_t>(pc)] = weights[static_cast<std::size_t>((pc - key.tonic + 12) % 12)];
            lb::Key best, second;
            double r1 = 0.0, r2 = 0.0;
            lb::KeyEstimator::score(histogram, profile, best, r1, second, r2);
            CHECK(best == key);
            CHECK(r1 == Approx(1.0));
            CHECK(r2 < r1);
        }
    }
}

TEST_CASE("NoteSegmenter finds held notes through vibrato, ignores blips and glides, and marks phrase ends")
{
    lb::NoteSegmenter segmenter;
    std::vector<lb::SungNote> notes;
    int phraseEnds = 0;
    double t = 0.0;
    const auto frame = [&](bool voiced, double midi) {
        segmenter.process(t, voiced, midi, -20.0f, [&](const lb::SungNote& n) { notes.push_back(n); }, [&] { ++phraseEnds; });
        t += hop;
    };
    const auto hold = [&](double midi, double seconds) {
        for (double end = t + seconds; t < end;)
            frame(true, midi + 0.3 * std::sin(2.0 * std::numbers::pi * 5.5 * t)); // +-30 cents of vibrato
    };
    const auto rest = [&](double seconds) {
        for (double end = t + seconds; t < end;)
            frame(false, 0.0);
    };

    hold(60.0, 0.30);
    hold(62.0, 0.30);
    rest(0.10);              // a breath: not a phrase end
    hold(64.0, 0.05);        // a blip, too short to be a note
    rest(0.10);
    for (int i = 0; i < 25; ++i) // a 200 ms glide from E up to G: no stable pitch
        frame(true, 64.0 + 3.0 * i / 24.0);
    hold(67.0, 0.40);
    rest(0.50);              // a phrase end
    hold(65.0, 0.30);
    rest(0.40);              // another

    REQUIRE(notes.size() == 4);
    CHECK(notes[0].midi == Approx(60.0).margin(0.05));
    CHECK(notes[0].durationSeconds == Approx(0.30).margin(0.02));
    CHECK(notes[1].midi == Approx(62.0).margin(0.05));
    CHECK(notes[2].midi == Approx(67.0).margin(0.15)); // the end of the glide joins the note
    CHECK(notes[3].midi == Approx(65.0).margin(0.05));
    CHECK(phraseEnds == 2);
}

TEST_CASE("KeyEstimator finds the key of a major and a minor melody")
{
    lb::KeyEstimator major;
    sing(major, twinkleD);
    CHECK(major.estimate().best == (lb::Key { 2, false }));

    lb::KeyEstimator minor;
    sing(minor, tuneEminor);
    const auto e = minor.estimate();
    CHECK(e.best == (lb::Key { 4, true }));
    CHECK(e.second.isValid());
    CHECK(e.margin > 0.0);
}

TEST_CASE("KeyEstimator corrects for a singer who is 45 cents sharp")
{
    lb::KeyEstimator estimator;
    sing(estimator, twinkleD, 0.0, 45.0);
    const auto e = estimator.estimate();
    CHECK(e.tuningCents == Approx(45.0).margin(5.0));
    CHECK(e.best == (lb::Key { 2, false }));
    CHECK(estimator.pitchClassOf(62.45) == 2);
    CHECK(estimator.pitchClassOf(63.45) == 3);
    CHECK(estimator.pitchClassOf(61.95) == 1); // C# for this singer, though nearest to D on the A440 grid
}

TEST_CASE("KeyEstimator waits, guesses, locks once confident, and holds the locked key")
{
    lb::KeyEstimator estimator;
    lb::KeyEstimator::Settings settings;
    settings.autoLock = false; // locked by hand below
    estimator.setSettings(settings);

    // 1.5 s of voice: still listening.
    double t = sing(estimator, { { 62, 0.75 }, { 69, 0.75 } });
    CHECK(estimator.estimate().state == lb::KeyEstimator::State::listening);
    CHECK(!estimator.estimate().key.isValid());

    // Past 2 s: a guess, or already confident; either way a key to show.
    t = sing(estimator, { { 71, 0.7 } }, t);
    auto e = estimator.estimate();
    CHECK((e.state == lb::KeyEstimator::State::guessing || e.state == lb::KeyEstimator::State::confident));
    CHECK(e.key == e.best);
    CHECK(e.requiredMargin == Approx(0.25 - 0.15 * (e.voicedSeconds - 2.0) / 8.0).margin(1e-9));

    // The whole tune: confident.
    t = sing(estimator, twinkleD, t);
    e = estimator.estimate();
    CHECK(e.state == lb::KeyEstimator::State::confident);
    CHECK(e.best == (lb::Key { 2, false }));

    estimator.lock();
    const lb::Key locked = estimator.estimate().key;
    REQUIRE(locked == (lb::Key { 2, false }));

    // The singer moves a tone up for a long time: the shadow follows, the locked key stays.
    std::vector<Note> higher = twinkleD;
    for (auto& note : higher)
        if (note.midi > 0)
            note.midi += 2;
    for (int i = 0; i < 6; ++i)
        t = sing(estimator, higher, t);
    e = estimator.estimate();
    CHECK(e.state == lb::KeyEstimator::State::locked);
    CHECK(e.key == locked);
    CHECK(e.best == (lb::Key { 4, false }));

    estimator.unlock();
    CHECK(estimator.estimate().key == (lb::Key { 4, false }));
}

TEST_CASE("KeyEstimator locks by itself once the key is clear, and the margin it needs relaxes with time")
{
    lb::KeyEstimator estimator;
    CHECK(estimator.requiredMargin(0.0) == Approx(0.25));
    CHECK(estimator.requiredMargin(6.0) == Approx(0.175));
    CHECK(estimator.requiredMargin(30.0) == Approx(0.1));

    sing(estimator, twinkleD);
    const auto e = estimator.estimate();
    CHECK(e.state == lb::KeyEstimator::State::locked);
    CHECK(e.key == (lb::Key { 2, false }));
}

TEST_CASE("KeyEstimator forgets an old key within the decay time when unlocked")
{
    lb::KeyEstimator estimator;
    double t = 0.0;
    for (int i = 0; i < 3; ++i)
        t = sing(estimator, twinkleD, t); // about 21 s in D major
    REQUIRE(estimator.estimate().best == (lb::Key { 2, false }));

    std::vector<Note> inAb = twinkleD;
    for (auto& note : inAb)
        if (note.midi > 0)
            note.midi += 6;
    for (int i = 0; i < 5; ++i)
        t = sing(estimator, inAb, t); // 35 s in Ab major
    CHECK(estimator.estimate().best == (lb::Key { 8, false }));
}
