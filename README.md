# LiveBand

An offline Windows app that listens to a live singer and plays drums,
percussion, bass and keys that follow the singer's key and tempo. Built for the
Nigerian market: Afrobeats and worship styles first.

**Status: Phase 0** (setup and latency proof). The app passes the voice
through, plays a test instrument through sfizz, measures round-trip latency
and shows live audio diagnostics. No band yet.

- [Project brief](docs/SPEC.md)
- [Implementation plan](docs/IMPLEMENTATION_PLAN.md), with decisions in
  section 0
- [Windows setup and Phase 0 test](docs/WINDOWS_SETUP.md)

## Building

The code is C++20 with CMake. JUCE and sfizz are git submodules, so clone with
them:

```
git clone --recursive --shallow-submodules https://github.com/syflex/Live-band.git
```

### Windows (the target platform)

Use Visual Studio 2022 with the **Desktop development with C++** workload.
Open the folder in Visual Studio and pick the **Windows Release (MSVC)**
preset, or from a "Developer PowerShell for VS 2022":

```
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

The app is written to `out/build/windows-release/LiveBand_artefacts/Release/`.
Every push also builds it on GitHub Actions; see
[docs/WINDOWS_SETUP.md](docs/WINDOWS_SETUP.md) for downloading that build.

### Linux (development only)

Linux builds are for compile checks and tests. The core library and its tests
need only a compiler:

```
cmake --preset linux-core && cmake --build --preset linux-core && ctest --preset linux-core
```

The full app also needs JUCE's Linux dependencies (see
`.github/workflows/ci.yml`), then uses the `linux-debug` preset.

## Layout

```
src/common   lock-free queue, callback timing, real-time allocation guard    ┐ core library:
src/audio    DSP: latency measurement now; pitch and onsets in Phase 1       │ no JUCE, unit-tested
src/music    key, tempo and arranger (from Phase 2)                          ┘
src/engine   audio engine, device setup, sfizz instrument (JUCE)
src/Main.cpp, src/MainComponent.*   the app window
tests/       Catch2 tests; tests/engine drives the audio engine with a fake device
assets/      sound kits (downloaded) and grooves
libs/        JUCE and sfizz submodules
```

## Licence

LiveBand is licensed under the [GNU AGPLv3](LICENSE). It uses JUCE under
JUCE's AGPLv3 option; a closed-source or paid product would need a commercial
JUCE licence. Third-party components and their licences are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
