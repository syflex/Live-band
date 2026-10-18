# LiveBand AI PoC: Implementation Plan

This plan refines [SPEC.md](SPEC.md). It keeps the spec's goals, stack, success
criteria and phase order. It adds the concrete architecture, module designs,
build setup, a measurement method for every success criterion, and the
decisions that the spec leaves open.

---

## 0. Decisions from the project owner (14 October 2026)

These answers to the open questions override anything later in this document
that disagrees with them.

| Topic | Answer | What changes in the plan |
|-------|--------|--------------------------|
| Hardware | A Lenovo laptop and a Jedel gaming headset with a boom mic. No audio interface. | **WASAPI exclusive mode is the main audio path**, because a gaming headset has no ASIO driver. ASIO support stays in the build for later. Latency is measured acoustically, with the headset's mic held against its ear cup (section 4.11). If the round trip is above 15 ms, see "Latency fallback" below. |
| Repo visibility | Public | The project's own code is licensed **AGPLv3**, which matches JUCE's open-source licence. Vocal test recordings stay **out of the public repo** (private storage, anonymised singer IDs). |
| Musical style | No fixed style, but the product is for the **Nigerian market** | The demo styles are **Afrobeats** (upbeat, 4/4, about 100–112 BPM) and **Nigerian worship** (slow, 6/8, about 60–75 BPM). Highlife or praise is a possible third style. Add a **percussion layer** (shaker, congas, bell) next to drums, bass and keys. Test songs are Nigerian songs sung by Nigerian singers. |
| Time signature | 4/4 accepted | Afrobeats is 4/4, but many worship songs are in 6/8. The engine therefore treats the time signature as a property of each style (beats per bar and beat subdivision). The first style is 4/4. The 6/8 worship style follows in Phase 4. |
| Footswitch | None for the demo | **The laptop keyboard is the controller** (Space to tap and so on). Averaging over four taps makes keyboard timing good enough. MIDI pedal support stays in the plan because it costs little. |
| Who leads the tempo | The band leads. Following the singer matters less for now. | The band keeps the tapped tempo exactly. Re-tapping during playback changes it. **Automatic tempo following becomes a stretch goal**, done only if time is left after Phase 5. Phase 4 now covers feel and control: dynamics, fills, sections and the 6/8 style. The "±5% drift" success criterion is **deferred**. |

### Latency fallback for a headset without an audio interface

The band's sound is scheduled ahead of time on the beat clock, so the band
plays on the beat whatever the latency is. Round-trip latency only affects
**the singer hearing their own voice** in the headphones. If the headset
cannot reach 15 ms through WASAPI exclusive mode:

1. Turn software voice monitoring off, or down. The singer then hears their
   own voice acoustically and the band through the headset.
2. Use a low-cost USB audio interface with an ASIO driver and any wired
   headphones. Such interfaces usually reach well under 10 ms. The code
   already supports ASIO.

Phase 0 measures the headset and decides between these.

---

## 1. Summary of decisions

| # | Decision | Why |
|---|----------|-----|
| D1 | Split the spec's "music engine" in two. A **real-time player** on the audio thread holds the beat clock and the MIDI sequencer. A **non-real-time engine thread** runs the key estimator, beat follower and arranger, and sends commands to the player. | Notes must start on exact samples (sfizz `noteOn(delay, …)` takes a sample offset), so the clock has to advance with the audio. Slow work such as key estimation and tempo search stays off the audio thread. |
| D2 | A **JUCE-free core library** (`liveband_core`) holds all DSP and music logic. The app, the unit tests and an offline CLI (`lb-analyze`) all link it. | The Python/librosa evaluation then tests the same code that ships. The core builds and tests on Linux CI in seconds, and it can later be reused in a plugin or another host. |
| D3 | Pitch and onset analysis run at about **16 kHz** after decimating by 3. | YIN costs about 15 M multiply-adds per second at 16 kHz, against about 200 M at 48 kHz. Vocal pitch needs nothing above 8 kHz. |
| D4 | One **timeline**: a 64-bit sample counter. Every analysis frame, tap and scheduled note carries a timestamp on it, corrected by the measured input and output latency. | Analysis can then run on any thread without losing timing accuracy, and the band's sound is aligned to what the singer hears. |
| D5 | **Count-in tap**: four taps make one bar, and the band enters on the predicted fifth tap (beat 1 of the next bar). | This is how a band leader counts in. A least-squares fit over the taps gives both tempo and phase. |
| D6 | **Key lock**: a provisional key appears after at most 3 s of singing, the key locks when the band enters, and it changes only by hand or at a section boundary. | A wrong key change in the middle of a song sounds worse than a key that is slightly late. |
| D7 | **Follow** (now a stretch goal, see section 0) means a gentle phase-locked loop around the tapped tempo, not blind beat tracking. | ±5% drift from a known starting point is a much easier problem than finding the beat from nothing in sparse, syncopated vocal onsets. |
| D8 | Record test clips with the singer hearing a **click in headphones**, some with a ±5% tempo ramp. | This gives exact beat ground truth, with no hand annotation, if the follow stretch goal is attempted. The click also keeps the clips usable as file input at a known tempo. |
| D9 | Add **file-input mode** (a WAV file stands in for the mic) and a **session recorder** early, in Phase 1. | Testing becomes repeatable without a singer, and the 30-minute soak test can run unattended. |
| D10 | Keep SFZ sample kits **out of git** and download them with a pinned fetch script. Keep vocal test data in **private** storage. | Kits are large, and each has its own licence. Singers' recordings need consent. |

### Findings from checking the dependencies (October 2026)

- **JUCE is at 9.0.4.** Its licence is AGPLv3 or the JUCE 9 commercial licence.
  **JUCE now bundles the ASIO headers**
  (`modules/juce_audio_devices/native/asio/`), so `JUCE_ASIO=1` is enough and
  the "install the ASIO SDK" step drops out of Phase 0. Your program must still
  follow Steinberg's licence terms for those files, so review them before you
  distribute any binary. `JUCE_ASIO_USE_EXTERNAL_SDK=1` exists if you need your
  own copy. JUCE needs CMake 3.22 or later.
- **sfizz's latest tag is 1.2.3, and the last commit on `main` is from March
  2025.** The project is effectively in maintenance mode. It is BSD-2-Clause.
  Pin a commit (we use `main` at f5c6e29, which adds stuck-note and
  use-after-free fixes made after 1.2.3), and put sfizz behind a small `Instrument` interface so that a
  fallback sampler stays possible (see Risks).
  - Build options to set: `SFIZZ_SHARED=OFF`, `SFIZZ_RENDER=OFF`,
    `SFIZZ_JACK=OFF`, `SFIZZ_USE_SNDFILE=OFF` (libsndfile is LGPL), and
    `SFIZZ_TESTS`, `SFIZZ_DEMOS`, `SFIZZ_BENCHMARKS` and `SFIZZ_DEVTOOLS` all
    `OFF`. Link `sfizz::sfizz`, the alias of the static library.
  - sfizz needs its own submodules (abseil-cpp, simde, dr_libs, wavpack,
    filesystem, invoke.hpp), so clone with `--recursive`. These appear to be
    permissively licensed; confirm each one in `THIRD_PARTY.md` during Phase 0.
  - sfizz chooses its own C++ standard (17 on MSVC). Set C++20 on our own
    targets with `target_compile_features` rather than globally.
  - The sfizz API calls we use: `loadSfzFile` (not real-time safe),
    `setSampleRate`, `setSamplesPerBlock`, `setNumVoices`, `setPreloadSize`,
    `noteOn`/`noteOff(delay, note, vel)`, `cc`,
    `renderBlock(float**, frames, numStereoOutputs)` and `allSoundOff`.
    `setTuningFrequency` is for a stretch goal.
- **JUCE has built-in diagnostics we can use.** `AudioIODevice::getXRunCount()`
  is implemented for ASIO and WASAPI. `AudioDeviceManager::getCpuUsage()`
  reports the audio callback's CPU load. `AudioFormatWriter::ThreadedWriter`
  records audio from the audio thread without blocking it.

**Language:** C++20. JUCE 9 and MSVC both support it, and `std::span`,
designated initialisers and `std::numbers` keep the DSP code cleaner.

---

## 2. Architecture

### 2.1 Threads

```
                 ┌────────────────────── AUDIO THREAD (driver callback, 64/128 frames) ───────────────────────┐
 Mic ──► input ──┼─► HPF ─► dry vocal ───────────────────────────────────────────────► Mixer ─► Limiter ─► Out │
                 │     └─► Decimator(÷3) ─► YIN + Onset ODF ─► AnalysisFrame ──┐           ▲                    │
                 │                                                             │           │ stereo buses       │
                 │   PlayerCommand ─► PatternPlayer (BeatClock + sequencer) ─► sfizz ×3 ───┘                    │
                 └─────────────────────────▲───────────────────────────────────┼────────────────────────────────┘
                          SPSC (lock-free) │                    SPSC (lock-free)│
                 ┌─────────────────────────┴──── ENGINE THREAD (200 Hz) ────────▼───────────────┐
                 │  KeyEstimator · TapTempo · BeatFollower · Arranger (state machine)            │
                 └──────────▲──────────────────────────────────────────────────┬────────────────┘
                 ControlEvent│ (mutex queue is fine; neither side is real-time) │ EngineStatus snapshot
           ┌────────────────┴───────┐                                ┌─────────▼──────────┐
           │ Keyboard (UI thread)   │                                │ UI thread, 30 Hz   │
           │ MIDI footswitch thread │                                │ note/key/tempo/CPU │
           └────────────────────────┘                                └────────────────────┘
```

| Thread | Owns | Runs | Rules |
|--------|------|------|-------|
| Audio (driver) | Input conditioning, dry pass-through, analysis DSP, `PatternPlayer` (beat clock and sequencer), 3 × sfizz render, mixer, limiter, meters | Every buffer (1.3 ms or 2.7 ms) | No allocation, locks, file or console I/O, or `juce::String`. Bounded work per block. |
| Engine (`juce::Thread`) | `KeyEstimator`, `TapTempo`, `BeatFollower`, `Arranger` | Wakes every 5 ms | May lock against other non-real-time threads, never against the audio thread. |
| UI / message | Window, keyboard, device settings, 30 Hz display timer, loading at startup | Event-driven | Loads all SFZ and MIDI files before audio starts. |
| MIDI input (JUCE) | Footswitch → `ControlEvent` | Event-driven | Timestamps come from JUCE's high-resolution clock. |
| Recorder (`TimeSliceThread`) | Session WAV and CSV writing | Background | Fed by `ThreadedWriter` and a FIFO of log records. |
| sfizz internal | Sample loading and streaming | Background | Set a preload size large enough that the kits sit in RAM. |

### 2.2 Queues and shared state

All audio-thread boundaries use preallocated **single-producer single-consumer
(SPSC) ring buffers**. A full queue increments an overflow counter; it never
blocks.

```cpp
struct AnalysisFrame {            // audio → engine, one per 8 ms hop
    uint64_t sampleTime;          // centre of the hop on the timeline, minus input latency
    float    f0Hz;                // 0 when unvoiced
    float    periodicity;         // 1 - YIN CMND minimum, from 0 to 1
    float    rmsDb;
    float    odf;                 // onset detection function value
    float    onsetStrength;       // > 0 only on a picked onset
};

struct PlayerCommand {            // engine → audio
    enum Type : uint8_t { Start, Stop, SetTempo, NudgePhase, QueueSection,
                          SetLayers, SetTranspose, SetDynamics, Panic } type;
    int64_t atSample;             // or -1 meaning "next bar boundary"
    double  value;                // samplesPerBeat, phase in beats, velocity scale, …
    int32_t a, b;                 // section id, layer mask, semitones, mode
};
```

- **Transport status (audio → engine and UI):** an atomic snapshot of
  sample time, beat position, samples per beat, bar, section and layers.
  Published with a seqlock or with two buffers and an atomic index.
- **Time mapping:** each block, the audio thread publishes the pair
  (sample time at block start, high-resolution milliseconds at callback entry).
  Tap timestamps, which arrive in milliseconds, are mapped onto the timeline
  with it.
- **Latency compensation:** subtract the measured input latency from analysis
  timestamps. Schedule notes early by the output latency, so that beat 1
  *reaches the singer's ear* on the predicted beat. Both values come from the
  Phase 0 loopback calibration, with the device-reported values as a
  fallback.

### 2.3 How the real-time rules are enforced

- All buffers are sized in `prepare(sampleRate, maxBlockSize)`. After a device
  change: stop the callback, prepare again on the message thread, then
  restart.
- A **debug-build allocation guard**: replace global `operator new` and
  `delete` so they assert when a `thread_local inAudioCallback` flag is set.
  This catches accidental allocation the first time it happens.
- Wrap the callback in `juce::ScopedNoDenormals`.
- **Callback profiler**: time every callback with the high-resolution
  counter. Track the mean, the maximum, and the number of callbacks over 80%
  of the buffer time. Show these in the UI with `getXRunCount()`.

---

## 3. Repository layout

The spec's folders are kept. `src/audio`, `src/music` and `src/common` make up
the JUCE-free core.

```
CMakeLists.txt
CMakePresets.json                 windows-debug, windows-release, linux-core
THIRD_PARTY.md                    licences of every dependency and asset
libs/JUCE                         submodule, pinned to 9.0.4
libs/sfizz                        submodule, pinned to main at f5c6e29 (recursive)
src/
  common/   SpscQueue.h  Timeline.h  RtAllocGuard.*  Seqlock.h        ┐
  audio/    Decimator.*  YinPitch.*  OnsetDetector.*  Fft.*           │ liveband_core
  music/    NoteSegmenter.*  KeyEstimator.*  KeyProfiles.h            │ (static lib,
            TapTempo.*  BeatClock.*  BeatFollower.*                   │  no JUCE,
            Pattern.h  PatternPlayer.*  Arranger.*                    ┘  unit-tested)
  engine/   AudioEngine.*  SfizzInstrument.*  Mixer.*  GrooveLoader.* (JUCE / sfizz)
            Controls.*  SessionRecorder.*  LatencyTester.*  FileInput.*
  ui/       NoteDisplay.*  PitchView.*  (JUCE components)
  Main.cpp
  MainComponent.*                 UI: note, key, tempo, CPU display
tests/                            Catch2 v3 (BSL-1.0) unit tests for the core
tools/lb-analyze/                 CLI: WAV in → CSV/JSON of frames, onsets, key, beats
tools/python/                     eval_pitch.py  eval_key.py  eval_beats.py
                                  make_click.py (steady and ramped clicks)  check_dropouts.py
tools/fetch_assets.py             downloads pinned SFZ kits and checks SHA-256
assets/sfz/                       gitignored, filled by fetch_assets.py
assets/grooves/<style>/           style.json plus one .mid per section
testdata/                         gitignored; manifest.csv lists ground truth (clips stored privately)
docs/
```

The spec's `TempoClock` is split into `TapTempo` (engine thread),
`BeatClock` (audio thread) and `BeatFollower` (engine thread), because they run
on different threads.

---

## 4. Module designs

### 4.1 Input conditioning and `Decimator`
- A high-pass filter at 70 Hz on the mic removes rumble and plosives, for both
  the dry signal and the analysis path.
- Decimate by `round(fs / 16000)`: by 3 at 48 kHz, which gives 16 kHz. Use a
  polyphase windowed-sinc FIR of about 48 taps that is evaluated only at the
  output rate.
- At 44.1 kHz the analysis rate becomes 14.7 kHz. Every analysis module takes
  its sample rate as a parameter.

### 4.2 `YinPitch`

| Parameter | Value at 16 kHz |
|-----------|-----------------|
| Range | 65 Hz (C2) to 1050 Hz (C6): lag τ from 15 to 246 samples |
| Window W | 512 samples (32 ms, at least two periods at 65 Hz) |
| Hop | 128 samples (8 ms), giving 125 frames per second |
| Threshold | 0.12 on the cumulative mean normalised difference (CMND), tuned on test data |

Steps:
1. Compute the difference function directly. The loop is SIMD-friendly and
   costs about 126 k multiply-adds per frame. An FFT-based version can follow
   if profiling asks for it.
2. Normalise it into the CMND.
3. Take the first dip below the threshold, or the global minimum if none.
4. Refine with parabolic interpolation.
5. Report `periodicity = 1 - CMND(τ*)`.

Voicing requires periodicity above a threshold **and** RMS above a noise gate
that is calibrated at startup. Voicing uses hysteresis.

Post-processing: a frame more than a semitone from the median of the last 5
frames is replaced by that median; other frames keep their own value (a plain
median lagged behind vibrato). The UI shows the note name, a ±50 cent meter
and the confidence.

Implemented additions (Phase 1): parabolic interpolation uses the raw
difference function, and a **sub-octave check** picks twice the period when its
interpolated CMND dip is less than 0.3 times the chosen one (and the chosen dip
is above 0.05). This catches octave-too-high answers when the second harmonic
dominates.

### 4.3 `OnsetDetector`
- Runs a 512-point Hann STFT with a 128-sample hop on the same frames as YIN,
  so each hop produces one `AnalysisFrame`.
- **Onset detection function (ODF):** log-compressed, half-wave-rectified
  spectral flux, with a SuperFlux-style maximum filter across frequency so
  that vibrato does not fire false onsets. The filter is a semitone wide at
  every frequency, because vibrato moves high harmonics across many FFT bins. Add a **pitch-novelty** term: a
  jump of more than 0.7 semitones between stable YIN segments, or voicing
  switching on. Soft vocal onsets often show up in pitch rather than energy.
- **Peak picking:** an adaptive threshold of the median of the last 300 ms
  times k, plus δ. Look 3 frames back for a local maximum; the 24 ms delay
  this adds is harmless because frames carry timestamps. Require at least
  100 ms between onsets.
- The FFT is our own small radix-2 implementation, or PFFFT, which has a
  BSD-style licence. Either way the core stays free of JUCE.

### 4.4 `NoteSegmenter` and `KeyEstimator`
1. **Notes:** a run of voiced frames whose pitch stays within ±50 cents for at
   least 80 ms becomes a note event {pitch class, duration, loudness,
   phrase-final flag}. A note is phrase-final when 300 ms or more of silence
   follows it.
2. **Tuning offset:** a running circular mean of each frame's deviation from
   the A440 grid, in cents. Notes are binned relative to it, because a singer
   with no accompaniment is often 20–50 cents off A440. The UI shows the
   offset. A stretch goal is to retune the band to the singer with sfizz's
   `setTuningFrequency`.
3. **Histogram:** each note adds duration × loudness weight to its pitch
   class. Phrase-final notes get extra weight, because melodies tend to end on
   the tonic, which helps tell relative major and minor apart. Before the key
   locks, the histogram decays with a time constant of about 30 s.
4. **Score:** Pearson correlation of the histogram against the 24 rotated
   major and minor profiles. Krumhansl–Kessler is the baseline. In the offline
   harness, also try **Temperley** and **Aarden–Essen** (Aarden–Essen comes
   from folk melodies and usually suits monophonic singing better) and
   Albrecht–Shanahan. Each is 24 numbers, so swapping them costs nothing.
   Ship whichever wins on the test data.
5. **Decision:** show a provisional key once there is at least 2 s of voiced
   singing and the margin between the best and second-best key passes a
   threshold. At 3 s, show the best guess whatever the margin, with its
   confidence. The key **locks** when the band enters. While locked, a shadow
   estimate keeps running and appears in the UI.
6. **Overrides:** key presses (or pedal commands) shift the key by ±1 semitone,
   toggle major/minor, and unlock the key.
7. *As built in Phase 3:* the profile is Temperley's (it won on both test sets, see
   [PHASE3_RESULTS.md](results/PHASE3_RESULTS.md)). The key locks **by itself once it is
   clear**, not when the band enters: the best key's correlation must lead the second's
   by a margin that relaxes from 0.25 to 0.1 between 2 and 10 s of voice, and the band
   follows the locked key, changing on its next downbeat. The overrides are ±1
   semitone, **relative** major/minor (C major and A minor share their notes, so that is
   the common mistake, not C major and C minor), lock now, and "listen again". The
   estimator runs on the message thread (`BandController`), fed by the analysis frames;
   it costs nothing on the audio thread. After the band stops it listens afresh.

### 4.5 `TapTempo` (engine thread)
- Taps are stored on the timeline. The tap buffer resets after a gap longer
  than 2 s, or when an interval differs from the running median by more than
  30%.
- After 2 taps, show a provisional BPM. After 4 taps, fit `t_i = t0 + i·P` by
  least squares. This gives BPM and phase together and averages out the
  timing jitter of keyboard events, which arrive on the message thread with
  a few milliseconds of jitter.
- Send `Start(atSample = t0 + 4P - outputLatency, samplesPerBeat = P)`. The
  tempo range is clamped to 50–200 BPM.
- Tapping while the band plays re-locks tempo and phase, and resets the
  follower. This is the manual fallback for following.
- *As built in Phase 2:* a run restarts when an interval is more than 30% from
  the fitted period, and presses under 0.1 s apart are ignored as key bounces.
  The fit uses the latest eight taps. Instead of clamping to 50–200 BPM, the
  tapped tempo is moved by octaves into the style's range, so half-time taps
  work. Taps while playing steer the band gradually (see 4.6). The logic lives
  in `BandController` on the message thread, using a `TimeMap` that converts
  clock times (key presses, MIDI timestamps) to sample times.

### 4.6 `BeatClock` and `PatternPlayer` (audio thread)
- **Clock state:** `int64 anchorSample`, `double anchorBeat`, and
  `double samplesPerBeat`. The beat position at time t is
  `anchorBeat + (t - anchorSample) / samplesPerBeat`. Every tempo change
  re-anchors the clock, so rounding error cannot build up over 30 minutes.
- **Corrections:** tempo changes slew, by at most 1% per beat. A phase nudge
  becomes a temporary rate change spread over one or two beats. The beat
  position **never moves backwards**, so events are never played twice or
  skipped.
- **Sequencing:** for each block covering beats [b0, b1), every instrument
  emits the pattern events in that range with
  `delay = (beat - b0) · samplesPerBeat`.
- **Active-note table:** a fixed array per instrument records which pitch each
  note actually sounds at and when it ends. Note-offs always use the sounding
  pitch, so changing the transposition, layers or section can never leave a
  note hanging. `Panic` calls `allSoundOff`.
- **Transposition:** shift by k semitones, with k in [-6, +5] relative to C,
  so the bass and keys stay in their registers. Pick the major or minor
  pattern variant to match the key's mode. Drum patterns are never
  transposed.
- Pattern, layer and transposition changes **take effect at bar boundaries**.
  They are queued until then.
- *As built in Phase 2:* a new tempo (from the BPM box) takes effect at once,
  keeping the beat position. A phase correction (taps while playing) moves the
  clock onto a new line with a proportional controller: at most 8% faster or
  slower, with a one-beat time constant, so it never jumps or runs backwards.
  Notes are assigned to blocks by their nearest sample, so each plays exactly
  once whatever the rounding. Engine tests and a recorded session show the kick
  starting within 0.25 ms of the logged beat.

### 4.7 `BeatFollower` (engine thread, stretch goal)

Deferred by the decision in section 0. The design is kept here for when it is
picked up.

1. Snap each onset to the nearest **subdivision grid point** of the current
   clock: eighth notes for 4/4 styles, triplet eighths for 6/8. The timing error is `e = t_onset - t_grid`, in beats.
2. Accept the onset only if |e| < 0.15 beat and its strength passes a
   threshold. Onsets on the beat weigh more than onsets on the off-beat.
3. Run an **alpha-beta filter** with two states, phase and period, updated
   once per bar from the weighted mean error.
   - A dead band of ±15 ms ignores small errors.
   - The period may change by at most 2% per bar, and by at most ±10% in total
     from the tapped tempo.
   - The filter only acts when at least 3 accepted onsets fall in the last 2
     bars. Otherwise it **holds** the tempo.
4. **Sanity check:** every bar, run an autocorrelation or comb filter over the
   last 8 s of the ODF, limited to ±10% of the current tempo. If a clearly
   stronger tempo shows up more than 3% away, nudge toward it.
5. The outputs are `SetTempo` and `NudgePhase` commands. Following is
   deliberately slow, because the band keeps time and the singer is followed
   gradually. A "hold tempo" toggle turns following off.

### 4.8 `Arranger` (engine thread) and groove data
- **Style data**, loaded at startup:
  - `style.json` holds the name, time signature (beats per bar and beat
    subdivision, so 4/4 and 6/8 both work), BPM
    range, the sections (intro, verse, chorus, fill, ending), the chord chart
    per section, and the staged-entry plan.
  - One `.mid` file per section. Its tracks are named `drums`, `perc`, `bass`,
    `keys` and `pad`, and the music is written **in C major, with a C minor
    variant**.
  - `GrooveLoader` turns each `juce::MidiFile` into an immutable core
    `Pattern`: a list of events {beat, duration in beats, note, velocity},
    sorted by beat, plus the pattern length. Storing durations instead of
    separate note-offs makes the active-note table simple.
- **State machine:**
  `Idle → Listening (provisional key) → CountIn → Playing(section) → Ending → Idle`.
- **Staged entry**, configurable per style:
  - If the key is known when the band starts: pad and keys on bar 1, drums on
    bar 5, bass on bar 9. A key press (or pedal) can advance a stage early.
  - If the key is not yet known: drums first, then pad and bass at the next
    4-bar boundary after the key locks.
  - *As built in Phase 3:* both plans are in `style.json` (`stagedEntry`); without a
    key, the bass, keys and pad all join at the boundary. Drums coming in after bar 1
    bring the entry crash. Advancing a stage early by hand is not built yet. Fills
    replace only the style's `fillLayers` (drums and percussion); the harmony carries on.
- **Section change:** pressing "next" queues the change. A drum fill replaces
  the last bar of the current 4-bar phrase, and the new section starts on the
  next phrase boundary.
  - *As built in Phase 2, to confirm with the owner:* the change works like an
    arranger keyboard (Yamaha PSR, Korg Pa), which keyboardists in Nigerian
    churches and bands know well. "Next" plays the fill from the current beat to
    the end of the bar, and the new section starts on the next downbeat; in the
    last half beat, the fill takes the next bar instead. This answers at once,
    where waiting for a phrase boundary could take up to five bars. "Stop" plays a
    fill, then the ending hits on the next downbeat.
- **Dynamics:**
  - Vocal loudness is smoothed over 1.5 s and compared with the session's own
    distribution (by percentile). It maps to a velocity scale from 0.8 to
    1.15.
  - Loudness above the 80th percentile for 2 bars triggers a fill or crash,
    or switches to an "intense" pattern variant if the style has one.
  - Keep these effects subtle.

### 4.9 `SfizzInstrument`, `Mixer`, `AudioEngine`
- **`SfizzInstrument`** implements the small `Instrument` interface:
  `prepare`, `noteOn`, `noteOff`, `render` and `allNotesOff`.
  - Create one sfizz instance per kit: drums, bass, and keys or pad.
  - Load it on the message thread before audio starts, with 64 voices and a
    preload size large enough to keep the whole kit in RAM.
  - The PoC never swaps kits while running.
- **`Mixer`:** the mono vocal is panned to the centre, with gain and an
  optional light `juce::dsp::Reverb` send. Each instrument has a stereo bus
  with gain. The master goes through `juce::dsp::Limiter`. All buffers are
  preallocated.
  - *As built in Phase 2:* the master limiter is our own (`audio/Limiter`): instant
    gain reduction above -1 dBFS and an 80 ms release, with no look-ahead, so it
    adds no latency and leaves quieter signals untouched. `juce::dsp::Limiter`
    compresses everything above -10 dB at 4:1 first, which would change the mix.
- **`AudioEngine`** is a `juce::AudioIODeviceCallback` on an
  `AudioDeviceManager`, with no plugin wrapper. Device types are "ASIO" and
  "Windows Audio (Exclusive Mode)". Settings are saved as XML on the message
  thread.

### 4.10 Controls

| Action | Key | MIDI (default, learnable) |
|--------|-----|---------------------------|
| Tap | Space | CC 80, sustain pedal (CC 64) or any note |
| Start / stop | Enter | CC 81 |
| Next section | N | CC 82 |
| Fill now | F | CC 83 |
| Key lock / unlock | K | — |
| Key ±1 semitone, major/minor | ↑ / ↓ / M | — |
| Hold tempo (no follow) | H | — |
| Panic | Esc | — |

- **For the demo the laptop keyboard is the controller** (section 0).
- Footswitches that pretend to be a USB keyboard (page-turner pedals) work
  through the key map with no extra code.
- MIDI timestamps are more precise than key events, so recommend a MIDI pedal
  for tapping.
- The UI and MIDI threads both push `ControlEvent`s into the engine's queue.

### 4.11 Developer tools inside the app
- **Latency tester:** connect the output back to the input. With the
  headset, take it off and hold the mic against one ear cup (acoustic
  loopback). With an audio interface, use a loopback cable. The app plays a
  short log chirp at a moderate level, cross-correlates the recording
  with it, and reports the round-trip latency as the median of 10 runs. Check
  the result against Oblique Audio's RTL Utility. The measured input and
  output offsets are stored as the calibration used in 2.2.
- **File input:** a WAV file, preloaded into RAM and looped, replaces the mic.
- **Session recorder:** writes the raw input WAV, the output mix WAV, the
  analysis CSV, MIDI out and an event log (commands, key changes, tempo
  corrections, overruns). Python can then analyse any live session after the
  fact.

---

## 5. Build setup

- **Top-level `CMakeLists.txt`** (CMake 3.22 or later):
  - Set the sfizz cache options from section 1 **before**
    `add_subdirectory(libs/sfizz)`. Then `add_subdirectory(libs/JUCE)`.
  - `liveband_core` is a static library. It has `cxx_std_20`, `/W4
    /permissive- /utf-8` on MSVC, and `-Wall -Wextra` elsewhere.
  - `LiveBand` uses `juce_add_gui_app`. It links `liveband_core`,
    `sfizz::sfizz`, `juce_audio_utils`, `juce_dsp` and `juce_audio_formats`.
    Compile definitions: `JUCE_ASIO=1`, `JUCE_WASAPI=1`, `JUCE_DIRECTSOUND=0`,
    `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`. Turning off curl and the web
    browser keeps the app fully offline, with no network code.
  - `liveband_tests` uses Catch2 v3 through `FetchContent`. `lb-analyze`
    links only the core plus a WAV reader (dr_wav, which is public domain).
  - The option `LB_BUILD_APP` (ON by default) turns the app off for Linux CI.
- **`CMakePresets.json`:** `windows-debug` and `windows-release` use the
  Visual Studio 17 2022 generator for x64. `linux-core` sets
  `LB_BUILD_APP=OFF`.
- **CI (GitHub Actions):**
  - Ubuntu: build the core, the tests and `lb-analyze`, then run the tests.
  - `windows-latest`: build the full app with ASIO on (the headers are
    bundled), run the tests, and upload the Release `.exe` as a build
    artifact.
- **Assets:**
  - `tools/fetch_assets.py` downloads pinned kit archives and checks their
    SHA-256 hashes.
  - `THIRD_PARTY.md` records each licence. Prefer CC0 or CC-BY kits, and
    avoid non-commercial ones.
  - Candidates to check: VSCO-2 Community Edition (CC0), Salamander Grand
    Piano (CC-BY), the Karoryfer free libraries, and FreePats.

---

## 6. Phase plan

Each phase ends with exit criteria that can be checked. The estimates assume
one developer.

### Phase 0: Setup and latency proof (2–3 days)
- [x] Create the repo skeleton, the submodules (JUCE 9.0.4, and sfizz `main`
      recursive), CMake, presets, `.gitignore` and both CI jobs.
- [x] Build a pass-through app: `AudioEngine`, the device selector
      (`AudioDeviceSelectorComponent`), WASAPI exclusive mode first and ASIO
      when present, at 48 kHz with 128 frames. Also try 64.
- [x] Add the real-time guards: `ScopedNoDenormals`, the debug allocation
      guard, the callback profiler, and the xrun counter in the UI.
- [x] Build the latency tester and store the calibration.
- [x] **sfizz smoke test:** build it under MSVC, load one SFZ file and render
      a note. Doing this early removes the main integration risk.
- [x] Write `docs/WINDOWS_SETUP.md`: install steps, High Performance power
      plan, USB selective suspend off, run on AC power, no USB hub, and the
      headset settings (allow exclusive mode, 48 kHz default format, audio
      enhancements and voice effects off, no communications ducking). If
      LatencyMon flags Wi-Fi or Bluetooth, turn them off during a gig.
- [x] Write `THIRD_PARTY.md`, the first licence audit.
- **Exit:**
  - The headset's round-trip latency is measured with WASAPI exclusive mode
    at the smallest buffer that plays without dropouts. Below 15 ms passes.
    Above it, choose one of the two options in "Latency fallback" (section 0)
    and record the choice.
  - LatencyMon reports the system as suitable for real-time audio over
    10 minutes.
  - The sfizz test note plays through the headset.

### Phase 1: Listen (week 1) — done
Results: [docs/results/PHASE1_RESULTS.md](results/PHASE1_RESULTS.md).

- [x] Core library: Catch2, `SpscQueue`, and the engine's 64-bit sample counter as
      the timeline (a separate `Timeline` type was not needed yet).
- [x] `Decimator` and `YinPitch`, with unit tests on synthetic sines, harmonic
      tones and pulse trains from C2 to C6, with and without noise. Live note
      display.
- [x] `OnsetDetector`, plus a scrolling pitch and ODF view in the UI.
- [x] File-input mode (load or drop a WAV) and the session recorder.
- [x] `lb-analyze`, `eval_pitch.py` (against reference pitch tracks or pYIN) and
      `eval_onsets.py`. A CI job runs both on every push.
- [x] **Evaluated on real singing before our own recordings exist**, using the
      public vocadito dataset (40 clips with hand-corrected pitch and note
      annotations). This led to three changes: outlier-only pitch smoothing, a
      sub-octave check in YIN, and a frequency-proportional max filter in the
      onset detector.
- [x] **The app records the test set itself:** Record/Stop with a click in the
      headphones (4/4 or 6/8, steady or ±5% over 16 bars), saving `input.wav`,
      the live analysis and the click times. This replaces recording against a
      separately generated click.
- [ ] **Record the test set** (owner, with singers): see
      [RECORDING_GUIDE.md](RECORDING_GUIDE.md).
  - 15–20 clips of 30–60 s, from 4 or more Nigerian singers of mixed voice
    types, singing Nigerian songs: Afrobeats and pop, worship and praise.
  - At least 10 different keys, both major and minor, at 60–140 BPM, with
    some 6/8 worship songs.
  - About 5 clips with a ±5% tempo change, and about 3 sung freely.
  - Written consent from each singer.
- **Exit:** met on vocadito; to be repeated on the Nigerian test set.
  - Pitch error below 10 cents on the synthetic signals: **met**.
  - At least 95% of voiced frames within 50 cents, fewer than 2% octave errors:
    **97.4% and 0.9%** on vocadito.
  - Analysis below 5% of the callback's time budget: **about 128× real time**.
  - Onset F-measure of at least 0.65 with a 50 ms window: **0.757** on vocadito.

### Phase 2: Play (week 2) — built; laptop checks pending
Results: [docs/results/PHASE2_RESULTS.md](results/PHASE2_RESULTS.md).

- [x] `SfizzInstrument` with the drum kit: a synthesized Afrobeats kick plus 116
      CC0 samples from the Versilian Community Sample Library, mapped to General
      MIDI drum notes, levels balanced by measurement, all in RAM. Built by
      `tools/python/make_afro_kit.py`; the samples are fetched by
      `tools/python/fetch_assets.py` and bundled with the Windows build by CI.
- [x] The `Pattern` and `Style` types, `GrooveLoader` and the `style.json` schema
      (time signature, tempo range, swing, layers, count-in, entry and ending hits,
      sections).
- [x] The **Afrobeats** style (4/4, 104 BPM, range 90–125): two-bar verse and
      chorus patterns and a one-bar fill for each, on a drums layer and a perc
      layer. Generated by `tools/python/make_grooves.py` from readable grids. **A
      Nigerian keyboardist or producer should review them before the demo.**
- [x] `BeatClock` and `PatternPlayer`, with unit tests:
  - No drift after a simulated 30 minutes of random block sizes (within 1 sample).
  - Steering never moves the beat position backwards.
  - No notes hang after random section changes, fills, stops and panics.
- [x] `TapTempo` and `BandController`: four taps start the band on the fifth beat,
      Enter starts with a count-in. Keys (Space, Enter, N, F, Esc) and MIDI (notes,
      sustain pedal, CC 80–83). Commands go through the engine's command queue.
- [x] Drum volume, layer switches and the master limiter.
- [x] `lb-render`: plays a style through the kit offline into a WAV file.
- [x] Sessions also record the band's beats (`beats.csv`).
- **Exit:**
  - After 4 taps, the drums start on the fifth predicted tap: **0.3 ms entry error
    and exact tempo** in the engine test with exact taps; with taps off by up to
    5 ms, 3.2 ms on average (6.5 ms worst) and at most 0.45% tempo error. The
    loopback measurement on the real laptop is still to do
    (`tools/python/check_tap_entry.py`, see WINDOWS_SETUP.md section 6).
  - No hanging notes: **met** (unit tests, engine tests, `lb-render`).
  - A 30-minute soak in file-input mode: **met for the engine's sustained cost** (median
    2.5% of the time budget, 99.9th percentile under 12%). Rare spikes over budget
    (0 to 2 per run) also hit a constant-work control loop on the cloud machine, a VM
    with no real-time priority, where the running app had 45 overruns in 678,738
    callbacks. To repeat on the laptop.

### Phase 3: Key and layering (week 3) — built; laptop checks pending
Results: [docs/results/PHASE3_RESULTS.md](results/PHASE3_RESULTS.md).

- [x] `NoteSegmenter`, the tuning offset, the histogram, four key profiles
      (Krumhansl–Kessler, Aarden–Essen, Temperley, Bellman–Budge), the margin and
      confidence, the lock logic, and the UI (the Key row).
- [x] `eval_key.py`: accuracy against seconds of voice, and the estimator's own lock,
      on a synthetic test set of 200 sung hymn and folk melodies
      (`make_key_testset.py`) and on vocadito. Temperley's profile chosen.
- [x] Bass (sfizz oscillators), piano (VCSL Kawai, CC0) and pad (sfizz oscillators),
      built by `make_band_kits.py`, levelled against the drums and tuned by measurement.
      Transposition by -6 to +5 semitones (the register rule). The minor version of a
      section in minor keys. The Afrobeats parts in C major and C minor over four-bar
      progressions. **To be reviewed by a Nigerian musician.**
- [x] Staged entry with and without a key, and key changes, at bar boundaries (in
      `PatternPlayer`; a separate `Arranger` was not needed).
- [x] Manual key overrides: ±1 semitone, relative major/minor, lock, listen again.
- [x] `lb-render` plays the pitched instruments, with a key at the start or later.
- **Exit:**
  - **Exact key correct on at least 80% of test clips, using the first 3 s of
    voiced singing:** **not met**: 62% of the synthetic clips and 42% of vocadito
    (MIREX 0.69 and 0.46). When the key locks (median 5.6 s of voice), it is exact on
    82% and exact, relative or a fifth away on 96% of the synthetic clips (vocadito:
    72% and 83%, median 7.9 s). The band starts without a key and brings the harmony
    in after the lock, so the proposal is to judge the criterion at the lock: **at
    least 80% exact and 95% compatible, median under 6 s**, re-tuned on Nigerian
    recordings. For the owner to decide.
  - Staged entry works live: **met** in the running app on the cloud machine,
    measured from its output; to repeat on the laptop.
  - Transposed bass and keys stay in their registers: **met** (bass D1 to C4, piano
    E3 to Eb5, pad Ab2 to Bb4 in every key).
  - No hanging notes on transposition or section changes: **met** (unit and engine
    tests, `lb-render`).
  - Not an exit criterion, but to watch: on the cloud machine the full band costs
    about three times the drums (engine soak median 6.9%), and the running app had 81
    overruns in 5 minutes with the full band against 56 with the drums alone. The
    laptop soak with the full band decides whether the pad needs to be cheaper.

### Phase 4: Feel, control and the 6/8 style (week 4)
- [ ] Vocal loudness drives velocity, fills and the intense variant.
- [ ] Section changes: Phase 2 built the arranger-keyboard behaviour (fill now,
      new section on the next bar). Add phrase-aligned changes only if the owner
      prefers them after trying it.
- [x] Re-tap during playback (done in Phase 2: four taps or more steer the band
      onto the tapped tempo and beat within a few beats).
- [ ] Nudge keys change the tempo by ±1%.
- [ ] The **Nigerian worship** style (6/8): pad- and piano-led, soft drums,
      with its own staged entry.
- **Exit:**
  - Section changes land where the owner expects them (next bar, or the phrase
    boundary if that option is added), with the fill before them.
  - Re-tapping re-locks within one bar, with no hanging notes and no audible
    jump.
  - The 6/8 style plays in time, with transposition and staged entry working.

### Stretch goal: automatic tempo following
Only if time is left after Phase 5, following the design in section 4.7.
- [ ] `BeatFollower`, the latency-compensation check, and `eval_beats.py` on
      the ramp clips.
- **Exit:** on ramp clips, after 4 bars, at least 90% of band beats within
  ±40 ms of the true beat. A fixed tempo must fail the same test, and on
  steady or rubato clips following must not make things worse.

### Phase 5: Test and demo (week 5)
- [ ] Live sessions with at least 3 singers, following the measurement
      protocol in section 7. Keep a fix list.
- [ ] A 30-minute live soak with LatencyMon and CPU logging running.
- [ ] `docs/RESULTS.md`: the measured value for every success criterion.
- [ ] Record the demo video.

**Total:** about 5½ weeks, plus **one buffer week**. Automatic tempo
following is outside this estimate.

---

## 7. How each success criterion is measured

| Criterion | Method | Pass |
|-----------|--------|------|
| Round-trip latency < 15 ms | In-app loopback chirp tester (median of 10 runs), checked against RTL Utility, on the reference laptop with ASIO | < 15 ms |
| Key within 3 s | Live: time from the first voiced frame to the provisional key in the session log | ≤ 3 s |
| Key ≥ 80% correct | `lb-analyze` on `testdata/`, first 3 s of voiced singing, exact match (proposed in Phase 3: at the estimator's lock, ≥ 80% exact and ≥ 95% exact, relative or a fifth away) | ≥ 80% of clips |
| Tap locks instantly | A script sends MIDI taps; the loopback recording measures the band's first downbeat against the regression prediction | Entry error < 10 ms, tempo error < 0.5% |
| Follow ±5% drift | **Deferred** (section 0). If the stretch goal is attempted: `eval_beats.py` on ramp clips and session recordings | ≥ 90% of beats within ±40 ms after 4 bars |
| Re-tap during playback | Session log: time from the fourth re-tap to the new tempo taking effect | At the next bar, no hanging notes |
| Fully offline | Build without curl or web browser support; run with networking turned off | Works |
| CPU < 30% | `getCpuUsage()` (callback load) and the process CPU counter, logged every second over 30 min. Reference laptop: a 4-core/8-thread laptop from the last 5 years | Mean < 30%, no audible peaks |
| No dropouts in 30 min | Device xrun count, our own overrun counter, and `check_dropouts.py` scanning the recorded output for discontinuities | 0 |

---

## 8. Risks added to the spec's list

| Risk | Mitigation |
|------|------------|
| sfizz is dormant (no commits since March 2025) | Pin a known commit and build it in Phase 0. If it blocks, the `Instrument` interface allows a fallback: a simple one-shot sampler built on `juce::Synthesiser`, which is enough for drums. |
| Speaker bleed into the mic: the follower locks onto the band's own drums, and pitch tracking gets confused | Use headphones or in-ear monitors in the PoC. A cheap later fix: ignore onsets within ±30 ms of drum hits we scheduled ourselves, since we know exactly when they happen. Echo cancellation is out of scope. |
| Relative major/minor confusion (C major versus A minor) | Weight phrase-final notes, offer a manual mode toggle, and start groove progressions on the tonic. |
| The singer sings with no fixed reference and drifts from A440 | Bin relative to the estimated tuning offset. Stretch goal: retune the band with `setTuningFrequency`. |
| Keyboard tap jitter | Least-squares fit over the taps. Recommend a MIDI pedal. |
| Laptop DPC spikes (Wi-Fi, ACPI, GPU drivers) | `WINDOWS_SETUP.md`, LatencyMon in Phase 0 and Phase 5, run on AC power. |
| Licence obligations even in a PoC | Giving binaries to testers counts as distribution. Under AGPL, JUCE then requires the recipients to have the source. Either keep the repo AGPL-compatible or get a JUCE licence before any binary leaves your hands. Review Steinberg's terms for the bundled ASIO headers as well. |
| The grooves do not sound Nigerian enough | Generated starter grooves are only a starting point. Have a Nigerian keyboardist or producer review or rewrite them. |
| Free SFZ kits lack talking drum, shekere and log drum sounds | Use the closest General MIDI percussion (congas, shaker, agogo bell) for the PoC. Record or license authentic samples later. |
| The gaming headset's latency is above 15 ms | See "Latency fallback" in section 0. |
| Windows voice effects on the headset (noise suppression, automatic gain, Voice Clarity) distort pitch and loudness | Turn them off as described in `docs/WINDOWS_SETUP.md`. The app also shows the input level so it is easy to check. |

---

## 9. First commits

1. Skeleton: CMake, presets, submodules, `.gitignore`, `THIRD_PARTY.md`, CI
   (Linux core job with a placeholder test, Windows app job).
2. Pass-through `AudioEngine`, device settings UI, callback profiler, xrun
   display.
3. Latency tester and calibration storage.
4. sfizz smoke test (load an SFZ and play a note on a key press).
5. Core: `SpscQueue`, `Timeline`, `Decimator`, `YinPitch` with tests, and the
   note display.

---

## 10. Remaining questions

The first round of questions is answered in section 0. Still open:

1. Does the headset plug in by **USB** or by the **3.5 mm jack**? Both work,
   but they use different Windows drivers, which changes the latency to
   expect.
2. Which **Lenovo model** is the laptop (Settings → System → About)? It becomes
   the reference laptop for the CPU criterion.
3. Is there a **Nigerian keyboardist or producer** who could review the
   generated grooves before the demo? The Afrobeats patterns are written out as
   grids in `tools/python/make_grooves.py` and drawn in the Phase 2 results.
4. **Section changes:** the band changes section like an arranger keyboard (fill
   now, new section on the next bar). Is that how the singer or band leader
   expects it, or should changes wait for the end of a 4-bar phrase?
