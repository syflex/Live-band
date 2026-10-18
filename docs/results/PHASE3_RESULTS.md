# Phase 3 results: key and layering

Measured on 18 October 2026, on the `phase-3-key` branch.

## Summary

| Phase 3 exit criterion | Result | Target | |
|---|---|---|---|
| The key when the estimator locks it (the criterion agreed with the owner, see below) | held-out real singing (children's songs, 94 recordings): locks on **100%**, **94% exact**, **98%** exact, relative or a fifth away, median **6.0 s** of voice. Held-out synthetic singing (200 clips): 80%, **88%**, **98%**, 6.2 s. vocadito (24, used for development): 79%, 79%, 84%, 6.4 s | ≥ 80% exact, ≥ 95% compatible, median under 6 s | met on the real songs; on the synthetic clips the median is 0.2 s over; vocadito falls short |
| Exact key from the first 3 s of voiced singing (the plan's first criterion) | **80%** of the held-out real songs, 64% of the held-out synthetic clips, 42% of vocadito; MIREX scores 0.84, 0.71 and 0.47 | ≥ 80% exact | met on the real songs only; replaced by the criterion above |
| Staged entry works live | in the running app: pad and piano in bars 1 to 4, drums from bar 5, bass from bar 9, all in the sung key (G major), measured from the app's own output | works | met on the cloud machine; to repeat on the laptop |
| Transposed bass and keys stay in their registers | in all 24 keys: bass D1 to C4, piano E3 to Eb5, pad Ab2 to Bb4 | stays in register | met |
| No hanging notes on transposition or section changes | every note-on released after 30 simulated minutes of random section changes, fills and key changes, and after key changes during held chords | none | met |
| 30-minute soak with the full band (engine) | median **4.6%** of the time budget, 999 callbacks in 1000 under **16%** CPU time; 1 callback over budget, at a moment unrelated to anything the band did | under 100% | met for the sustained cost; spikes need the laptop |
| The running app on the cloud machine, 5 minutes with the full band | average load 9.1%; **64 overruns** in 114,338 callbacks (0.06%), against 55 with the drums alone in the same conditions | 0 overruns | not met here; to repeat on the laptop |

As in the earlier phases, everything here was measured on the cloud test machine: unit
and engine tests, offline evaluations, and the app itself running in real time on a
virtual sound device. The laptop checks are in [WINDOWS_SETUP.md](../WINDOWS_SETUP.md),
section 7.

### The key criterion

The plan asked for the exact key after 3 seconds of singing. Three seconds is usually
four to eight notes, and often they do not yet contain the notes that tell a key from its
neighbours: a tune that starts on the fifth and sixth of the scale fits its key, the
relative minor and the key a fifth away about equally well.

The band does not need the key that early. Started before the key is known, it plays the
drums and percussion alone and brings the bass, piano and pad in on the first four-bar
boundary after the key locks. So the estimator waits until it is sure: it locks when the
best key's correlation leads the second's by a margin that starts at 0.25 and relaxes to
0.1 between 2 and 10 seconds of voice. A clear melody locks early; an ambiguous one is
given more time.

I proposed judging Phase 3 at that lock, at least 80% exact and 95% harmonically
compatible (exact, relative, or a fifth away: the relative key uses the same notes) with
a median under 6 s of voice, and **the owner agreed on 18 October 2026**. The lock settings
are to be re-tuned on recordings of Nigerian singers once there are some.

## How key detection was tested and improved

Three kinds of test material, each split so that settings are chosen on one part and
checked once on another:

| Test set | Development (settings chosen here) | Held out (looked at once, at the end) |
|---|---|---|
| Synthetic singing: Bach chorale melodies (hymn tunes) and folk tunes from Ryan's Mammoth Collection, sung by a simple synthetic voice with vibrato, glides, a random tuning up to 40 cents off and a random key | 200 clips | 200 clips of other tunes |
| Real singing: the [Children's Song Dataset](https://doi.org/10.5281/zenodo.4916302), 50 English and 50 Korean children's songs, each sung in two keys by a professional singer, with every note annotated | 95 recordings (odd-numbered songs) | 94 recordings (even-numbered songs; both keys of a song stay on the same side) |
| Real singing: vocadito, 40 short clips of varied singers | 24 clips | (too small to split) |

The real recordings have no key labels. Their reference is the key that at least three
of four standard key-finding profiles give for the hand-annotated notes of the whole
melody; 189 of the 200 children's songs and 24 of the 40 vocadito clips have one. As a
check, 176 of those 189 melodies end on the reference key's tonic.

Two changes came out of the development sets:

1. **The phrase-ending bonus is off.** The first version counted each phrase's last note
   twice, because melodies tend to end on the tonic. On the development children's songs
   11 locks were wrong; 7 of them were the key a fifth above, and in 6 of those 7 the first
   phrase ended on the dominant or the second degree: a half cadence, which the bonus
   turned into evidence for the dominant key. Without it, locks on the development songs went from 88% to 96%
   exact, and on vocadito from 72% to 79%, with the synthetic clips unchanged.
2. **The notes are binned again with the latest tuning.** The first version placed each
   note in its semitone with the tuning estimate of the moment, so the first notes stayed
   wherever an unsettled estimate put them. Now the recent notes are kept and placed
   again for every estimate. This changed nothing on the steady synthetic voice or the
   professional singer, but on vocadito the lock came after 6.4 s of voice instead of 7.9 s
   at the same accuracy.

Sharing a note between two semitones when it falls near the boundary, and other lock
margins, were also tried; they made no difference that two dozen clips could show, so
they were left out.

On the held-out sets, looked at once at the end:

| Held out | 3 s: exact | Lock: reached / exact / compatible / median |
|---|---|---|
| Real children's songs (94), first version | 73% | 98% / 92% / 98% / 5.9 s |
| Real children's songs (94), shipped | **80%** | **100% / 94% / 98% / 6.0 s** |
| Synthetic (200), first version | 64% | 81% / 88% / 98% / 6.3 s |
| Synthetic (200), shipped | 64% | 80% / 88% / 98% / 6.2 s |

![Share of clips with the right key against seconds of singing on the held-out synthetic clips, the held-out children's songs and vocadito, with where the estimator locks](../images/phase3/key-accuracy.png)

Cautions: the children's songs are sung by one professional singer and are all in major
keys, so minor keys are only tested by the synthetic set; children's songs are simpler
than many worship and Afrobeats melodies; and the real sets' references come from
profiles, so they lean towards what profiles find. The test that matters is the owner's
own singing and recordings of Nigerian singers.

## What the band does now

![The app after hearing a few seconds of a sung hymn tune in G major: the key is locked, and the singer is 16 cents sharp](../images/phase3/app-key-locked.png)

A new **Key** row shows what the app hears and what the band plays:

- **Listening**, until there are 2 seconds of singing; then **Hearing G major (64 % sure)**,
  with how close it is to locking; then **G major, locked**. While locked, the app keeps
  listening and shows a different key if it starts to hear one.
- The singer's **tuning**: how far they are from A440, which the key detection allows for.
- **Lock** (K) locks the key it hears now. When locked, the same button reads **Listen
  again**: the app forgets what it heard and listens afresh, and the band keeps its key
  until the new one locks.
- **-1** and **+1** (Down and Up arrows) move the key a semitone. **Relative minor** /
  **Relative major** (M) swaps between, for example, C major and A minor.
- **Bass**, **Keys** and **Pad** switch those parts on and off, like Drums and Perc.

Every change of key takes effect on the band's next downbeat. Once the band has stopped,
the app listens afresh for the next song.

![The full band playing in G major, bar 11](../images/phase3/app-full-band.png)

How the band comes in depends on whether the key is known (the style's staged entry):

- **Key known when the band starts:** pad and piano on bar 1, drums and percussion on
  bar 5 (with a crash), bass on bar 9.
- **Key not known yet:** drums and percussion from bar 1; bass, piano and pad join on the
  first four-bar boundary after the key locks.

Fills now replace only the drums and percussion; the harmony carries on through them.
The ending hits include the tonic and fifth on the bass, piano and pad. Sessions record
the band's key and the parts playing on every beat (`beats.csv`), and the key decision in
`session.json`.

## The harmony

| Part | Instrument | Range in any key |
|---|---|---|
| Bass | A round synth bass made by sfizz's oscillators: a sine for the body and a filtered saw for the attack (no samples) | D1 to C4 |
| Keys | The Kawai grand piano of the [Versilian Community Sample Library](https://github.com/sgossner/VCSL) (CC0): one velocity layer, 11 samples from A2 to E6, each levelled and tuned to A440 by measurement | E3 to Eb5 |
| Pad | Three detuned saws through a low-pass filter (no samples) | Ab2 to Bb4 |

All three are generated by `tools/python/make_band_kits.py`; the piano's samples (40 MB)
are downloaded with the drum kit's by `tools/python/fetch_assets.py`. A test plays each
instrument and measures its pitch: the bass and pad are within 5 cents, the piano within
2 to 6 cents.

The parts are written in C major and C minor, over four-bar progressions:

| Section | Major | Minor |
|---|---|---|
| Verse | C G Am F (I V vi IV) | Cm Ab Eb Bb (i VI III VII) |
| Chorus | F G Em Am (IV V iii vi) | Ab Bb Cm Cm (VI VII i i) |

The bass locks with the kick (root on 1 and the "and" of 2, the fifth on 3, a step
towards the next chord at the end of the bar; an octave pop in the chorus). The piano
plays off-beat stabs in the verse and a syncopated figure in the chorus; the pad holds
the chord. In another key, the parts move by -6 to +5 semitones from C, so they stay in
their registers, and a minor key plays the minor version.

Levels were set against the drums by measuring each part alone, while all four play
(BS.1770 K-weighting, without gating): drums -24.6 LUFS, bass -26.8, piano -28.8,
pad -34.4.

![What the band plays when it starts without a key: drums alone, E minor heard in bar 2, the harmony from bar 5, the chorus from bar 9, G major from bar 11](../images/phase3/band-key-change.png)

The parts are generic pop and Afrobeats voicings: **a Nigerian keyboardist or producer
should review them**, with the drum grooves, before the demo.

## Key detection, in brief

The design follows section 4.4 of the plan:

1. **Notes:** a run of voiced frames whose pitch stays within half a semitone of its
   running mean for at least 80 ms. A phrase ends after 0.3 s without voice.
2. **Tuning:** a running circular mean of the notes' deviation from the A440 grid, so a
   singer 40 cents flat still lands on the right notes. The recent notes are placed again
   whenever the estimate is made.
3. **Histogram:** each note adds its duration times a loudness weight to its pitch class.
   It fades with a time constant of 30 s, so the estimate can follow a change of key.
4. **Score:** the correlation of the histogram with the 24 rotated major and minor key
   profiles.
5. **Decision:** nothing for 2 s of voice; then a guess; then a lock once the margin
   described above is reached.

Four profiles were compared on the development sets, all from Humdrum's `keycor` (the same
values as music21). The plan also named Albrecht and Shanahan's profile, but its values
could not be checked against a published source, so Bellman and Budge's took its place.

| Profile | Synthetic: 3 s / end / lock (exact) | Children's songs: 3 s / end / lock | vocadito: 3 s / end / lock |
|---|---|---|---|
| Krumhansl and Kessler | 48% / 70% / 65% | 49% / 75% / 53% | 21% / 54% / 64% |
| Aarden and Essen | 50% / 67% / 80% | 64% / 86% / 82% | 38% / 67% / 76% |
| **Temperley (shipped)** | **62% / 81% / 83%** | **73% / 99% / 96%** | **42% / 79% / 79%** |
| Bellman and Budge | 53% / 72% / 82% | 57% / 94% / 89% | 25% / 71% / 61% |

Temperley's profile is the best on every one of these measures.

When the shipped estimator locks a wrong key, it is nearly always a close one: on the
held-out children's songs 88 locks are exact, 4 a fifth away and 2 other (of 94); on the
held-out synthetic clips 140 exact, 10 relative, 6 a fifth away and 4 other (of 160). The
synthetic folk tunes are easier than the hymn tunes (95% against 80% exact at the lock).
The misses on vocadito come mostly from singers whose tuning is near a quarter tone off
A440 with a wide spread from note to note, so that some notes fall in the semitone above
or below.

## Checked in the running app

The app ran on the cloud machine with a virtual sound device, playing a synthetic sung
hymn tune in G major (Bach's BWV 394, transposed, sung 17 cents sharp) as its voice
input. It locked **G major** with the singer at **+16 cents** before the band started
(the screenshot above). Pressing Start then brought the band in with the staged entry
for a known key. The app's output was recorded from the virtual device and measured bar
by bar:

| Bar | Low band (30 to 120 Hz) | Piano and pad band (250 Hz to 1.5 kHz) | Hi-hats and shaker (6 to 12 kHz) | Strongest pitch below 400 Hz in beat 1 |
|---|---|---|---|---|
| count-in | 25 dB | 30 dB | 34 dB | (sticks) |
| 1 | 39 dB | 55 dB | 9 dB | B (G chord) |
| 2 | 46 dB | 55 dB | 8 dB | A (D chord) |
| 3 | 38 dB | 55 dB | 12 dB | B (Em chord) |
| 4 | 38 dB | 57 dB | 11 dB | C (C chord) |
| 5 to 8 | 66 to 67 dB | 56 to 57 dB | 47 dB | 52 Hz (the kick) |
| 9 | 70 dB | 56 dB | 47 dB | **G1, 48.9 Hz** (the bass) |
| 10 | 70 dB | 56 dB | 47 dB | **D2, 73.6 Hz** (the bass, D chord) |

The levels are relative (dB of summed spectral power). The pad and piano play the G major
verse (G D Em C) from bar 1, the drums come in on bar 5 and the bass on bar 9, playing G
then D.

The app was then left playing for 5 minutes at a time, with the full band and with the
bass, piano and pad switched off, before and after making the pad cheaper (below), with
nothing else running:

| 5 minutes in the running app | Average callback load | Slow callbacks (> 80%) | Overruns (> 100%) |
|---|---|---|---|
| Full band, first pad | 13.7% | 152 | 81 in 114,336 callbacks (0.07%) |
| Drums and percussion only | 7.5% | 104 | 56 in 115,143 callbacks (0.05%) |
| Full band, shipped pad | **9.1%** | 123 | **64** in 114,338 callbacks (0.06%) |
| Drums and percussion only | 6.1% | 106 | 55 in 115,175 callbacks (0.05%) |

None of these is the zero the project needs. Most of the overruns come from the machine:
with the drums alone the rate is already seven times that of the Phase 2 app soak (45 in
30 minutes), so the cloud machine was busier than it was then. It is a virtual machine
with four cores that runs the app without real-time priority, so other work can pause the
audio thread; the engine soak below, which measures only the engine's own work, shows no
sustained cost problem. The first version of the full band added 25 overruns to the drums'
56, because a longer callback is more often pushed past its deadline by such a pause; the
shipped one adds 9. **The laptop soak with the full band (WINDOWS_SETUP.md, section 4,
step 7) decides whether that is still a problem.**

## Soak and cost

Each part's own cost, from `lb-render --live` (sfizz at the app's live quality, three runs
of the Verse and Chorus at 104 BPM):

| Part | Share of real time | Voices at most |
|---|---|---|
| Drums and percussion | 1.3 to 2.0% | 26 |
| Bass | 0.2 to 0.3% | 4 |
| Piano | 0.5 to 0.8% | 6 |
| Pad, first version: five detuned saws, a triangle an octave down, 1.8 s release | 2.3 to 3.2% | 16 |
| **Pad, shipped: three detuned saws, 1.2 s release** | **1.2 to 1.4%** | 8 |

The pad was the most expensive part, more than the whole drum kit. Its octave-down
triangle also filled the low end that the bass and kick need: in bars 1 to 4 of the app's
output, the 30 to 120 Hz band dropped from 53 dB to 38 to 46 dB without it. Re-levelled
to the same loudness, the shipped pad costs half as much.

**The engine on its own** (`liveband_engine_tests "[soak]"`), with the full band: 30
minutes of audio with drums, percussion, bass, piano and pad, a section change or fill
every 30 seconds, a key change every 5 minutes, and the voice analysis running on a voice
file:

| | Median | 99.9th percentile | Worst | Over budget |
|---|---|---|---|---|
| Full band, first pad, CPU time | 6.9% | 20.6% | 111% | 1 |
| Full band, shipped pad, wall-clock time | 4.6% | 24.7% | 429% | 58 |
| Full band, shipped pad, CPU time | **4.6%** | **15.3%** | 126% | 1 |
| Control loop (identical work every time), CPU time | 6.5 to 7.8% | 17.9 to 22.3% | 74 to 176% | 0 to 1 |

The full band costs about twice the drums alone (median 2.5% in Phase 2), well under the
budget. The one callback over budget in CPU time came 26 seconds after a section change
and 146 seconds after a key change, not when the band did anything, and the control loop
has such spikes too. The key estimator runs on the message thread and costs nothing on
the audio thread; the callback still makes no memory allocations with the full band
playing and changing key (engine test).

## Tests

![The Phase 3 tests and the key-detection check](../images/phase3/tests.png)

98 tests pass (72 unit test cases and 26 engine test cases). New in Phase 3:

- **Key detection:** every profile recognizes all 24 keys from its own shape; a major and
  a minor melody; a singer 45 cents sharp; waiting, guessing, locking, the margin that
  relaxes with time, and forgetting an old key; key names that read back.
- **Player:** transposition in all 24 keys with the minor version in minor keys; a key
  change on the next downbeat with no hanging note; fills that keep the harmony; staged
  entry with and without a key, including the key arriving before the first downbeat;
  ending hits in the key; the 30-minute random test now changes key too.
- **Engine:** a sung melody (a synthetic voice in D major) locks the key through the
  analysis, and the band then comes in with the staged entry; without a key the bass
  stays silent, comes in on the four-bar boundary after a key is given, plays the tonic
  of G major, then of Ab major after +1, measured in the output; the full band does not
  allocate in the callback; the real Afrobeats parts stay in their registers in all 24
  keys; the bass, pad and piano play in tune.
- **CI** now fetches the piano samples, renders the full band with a key arriving and
  changing (`lb-render --key-at`), and fails if key detection on vocadito drops below
  regression floors (locked on 65% of clips, 75% of locks compatible). The children's
  songs (1.85 GB, non-commercial licence) are evaluated locally, not in CI.

## How to reproduce

```
python tools/python/fetch_assets.py                  # drum and piano samples
cmake --preset linux-release && cmake --build --preset linux-release
ctest --preset linux-release
out/build/linux-release/liveband_engine_tests_artefacts/Release/liveband_engine_tests "[soak]"

# Key detection (needs pip install -r tools/python/requirements.txt)
A=out/build/linux-release/lb-analyze_artefacts/Release/lb-analyze
python tools/python/make_key_testset.py              # development: testdata/external/keyset
python tools/python/make_key_testset.py --out testdata/external/keyset-holdout --seed 11 \
    --exclude testdata/external/keyset/labels.csv    # held out: other tunes
python tools/python/fetch_vocadito.py
python tools/python/fetch_csd.py                     # Children's Song Dataset, 1.85 GB
python tools/python/eval_key.py --analyzer $A --labels testdata/external/keyset/labels.csv
python tools/python/eval_key.py --analyzer $A --labels testdata/external/keyset-holdout/labels.csv
python tools/python/eval_key.py --analyzer $A --csd testdata/external/csd/CSD --split dev
python tools/python/eval_key.py --analyzer $A --csd testdata/external/csd/CSD --split test
python tools/python/eval_key.py --analyzer $A --vocadito testdata/external/vocadito
python tools/python/plot_key_eval.py --analyzer $A --labels testdata/external/keyset-holdout/labels.csv \
    --csd testdata/external/csd/CSD --vocadito testdata/external/vocadito --out key-accuracy.png

# The band, offline (--live times each part at the app's quality)
out/build/linux-release/lb-render_artefacts/Release/lb-render --style assets/grooves/afrobeats \
    --kit assets/sfz/afro-kit/afro-kit.sfz --bass assets/sfz/bass/synth-bass.sfz \
    --keys assets/sfz/keys/piano.sfz --pad assets/sfz/pad/warm-pad.sfz \
    --key-at 1:Em --key-at 9:G --plan Verse:8,Chorus:6 --out band.wav --events band.csv
python tools/python/plot_harmony.py --events band.csv --bpm 104 --out band.png
```

## Open items

- **Owner decisions:** whether the staged entry plans suit the band; whether the app
  should keep a key locked by hand after the band stops (today every song starts by
  listening).
- **Laptop checks** (WINDOWS_SETUP.md, section 7): the key locking on the owner's own
  singing, the staged entry and overrides by ear, and the soak with the full band.
- **Nigerian recordings:** sessions save the voice (`input.wav`), and the
  [recording guide](../RECORDING_GUIDE.md) asks for each song's key. A CSV with `file`
  and `key` columns is all `eval_key.py --labels` needs to score the key detection on
  them and re-tune the lock settings on real worship and Afrobeats singing. The
  recordings must not go into the public repository.
- **Musical review** of the harmony parts and progressions by a Nigerian musician.
- **Retuning the band to the singer** (sfizz's tuning frequency) when they are far from
  A440 remains a stretch goal.
