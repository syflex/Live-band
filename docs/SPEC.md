# LiveBand AI — Proof of Concept (Windows)

> Original project brief. The implementation plan that refines it is in
> [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md).

## Goal
Prove that an offline Windows laptop app can listen to a live singer and play
drums, bass and keys that follow the singer's key and tempo, with no noticeable lag.

## Success criteria
- Mic-to-speaker round-trip latency under 15 ms
- Key detected within 3 s of singing, correct on at least 80% of test songs
- Tap tempo locks instantly; auto-follow stays in sync with ±5% tempo drift
- Fully offline, CPU under 30% on a mid-range laptop
- No audio dropouts over a 30-minute session

## Stack
- Language: C++17 (or C++20)
- Framework: JUCE, standalone desktop app
- Build: CMake, with JUCE and sfizz as git submodules
- Compiler: Visual Studio 2022+ ("Desktop development with C++")
- Audio I/O: WASAPI exclusive (built in) + ASIO (Steinberg ASIO SDK, JUCE_ASIO=1)
- Audio settings: 48 kHz, 128-sample buffer or smaller
- Pitch detection: own YIN implementation
- Key detection: pitch-class histogram + Krumhansl major/minor key profiles
- Tempo: tap tempo first, then own onset-based beat tracker
- Instruments: sfizz (BSD licence) + free SFZ kits for drums, bass, piano/pad
- Grooves: predefined .mid files loaded with juce::MidiFile, transposed to the detected key
- Controls: computer keyboard or MIDI footswitch (tap, start, stop, next section)
- Offline testing: Python + librosa on recorded vocals (optional)
- Diagnostics: LatencyMon
- Version control: Git + GitHub
- Hardware: USB audio interface with ASIO driver (e.g. Focusrite Scarlett,
  Behringer UMC), dynamic mic, headphones or monitors

Licensing: JUCE is AGPLv3 or commercial (a commercial licence is needed for a closed paid product).
sfizz is BSD. Avoid GPL libraries (aubio, Essentia, BTrack) to keep the product closed-source later.

## Architecture
Mic -> Audio thread:
  1. Analysis: YIN pitch + onset detection
  2. Lock-free FIFO -> Music engine:
     - Key estimator
     - Tempo / beat clock
     - Arranger (picks groove, transposes, schedules layers)
  3. MIDI events -> sfizz -> mix with dry vocal -> Speakers

Rule: no memory allocation, locks or file access on the audio thread.

## Repo structure
```
liveband-poc/
  CMakeLists.txt
  libs/JUCE            (submodule)
  libs/sfizz           (submodule)
  src/Main.cpp
  src/MainComponent.*  (UI: note, key, tempo display)
  src/audio/YinPitch.*
  src/audio/OnsetDetector.*
  src/music/KeyEstimator.*
  src/music/TempoClock.*
  src/music/Arranger.*
  src/engine/SfizzInstrument.*
  assets/sfz/          (drum, bass, keys kits)
  assets/grooves/      (.mid files)
  tools/python/        (librosa test scripts)
  testdata/            (recorded vocal clips)
```

## Plan (rough estimates, one developer)

### Phase 0: Setup (1–2 days)
- Install VS, CMake, JUCE, ASIO SDK
- Build a mic -> speakers passthrough app
- Measure round-trip latency; run LatencyMon

### Phase 1: Listen (week 1)
- Implement YIN pitch detector; show live note on screen
- Implement onset detection
- Record 10–20 test vocal clips (different singers, keys, tempos)

### Phase 2: Play (week 2)
- Integrate sfizz; load a drum kit
- Build tempo clock; tap tempo starts a .mid drum groove
- Start/stop and footswitch controls

### Phase 3: Key + layering (week 3)
- Key detection from pitch histogram
- Bass and keys grooves transposed to detected key
- Staged entry: pad/keys -> drums -> bass

### Phase 4: Follow (week 4)
- Beat tracker nudges tempo to follow the singer
- Vocal loudness drives instrument velocity and fills
- Section changes (verse/chorus) via footswitch

### Phase 5: Test + demo (week 5)
- Test with real singers and songs; measure success criteria
- Fix latency, dropouts and detection errors
- Record a demo video

## Key risks
- Chords can't be reliably guessed from melody alone -> use predefined chord progressions per groove
- Free-time or rubato singing breaks beat tracking -> tap tempo fallback
- Room noise, reverb, instrument bleed into mic -> dynamic mic, headphones; ML pitch later if needed
- Windows audio lag -> ASIO interface, high-performance power plan

## Out of scope for PoC
- Machine-learning models
- Automatic chord detection
- Large song library
- Polished UI
- Mac support, VST/AU plugin versions
