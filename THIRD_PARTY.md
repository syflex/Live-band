# Third-party software and assets

LiveBand's own code is licensed under the GNU AGPLv3 (see [LICENSE](LICENSE)).
This file lists everything else that is built into the app or downloaded by its
tools. Update it whenever a dependency or asset is added.

## Linked into the app

| Component | Version | Licence | Notes |
|-----------|---------|---------|-------|
| [JUCE](https://github.com/juce-framework/JUCE) | 9.0.4 (submodule `libs/JUCE`) | AGPLv3 or commercial JUCE licence | A closed-source or paid product needs a commercial JUCE licence. |
| ASIO headers (bundled with JUCE) | as shipped in JUCE 9.0.4 | Steinberg SDK licence ([steinberg.net/sdklicenses](https://www.steinberg.net/sdklicenses)) | Used because `JUCE_ASIO=1`. Review Steinberg's terms before distributing binaries. |
| [sfizz](https://github.com/sfztools/sfizz) | `main` at f5c6e29 (March 2025; submodule `libs/sfizz`) | BSD-2-Clause | Pinned after 1.2.3 for its stuck-note and use-after-free fixes. Built with `SFIZZ_USE_SNDFILE=OFF`. |

### Bundled inside sfizz

Checked against each project's licence file in the pinned checkout.

| Component | Licence |
|-----------|---------|
| abseil-cpp | Apache-2.0 |
| atomic_queue | MIT |
| cephes | BSD (permission from the author, included in its licence file) |
| cpuid | BSD-3-Clause |
| cxxopts | MIT (only used by sfizz tools, which are not built) |
| dr_libs | Public domain or MIT-0 |
| filesystem (gulrak) | MIT |
| fmidi | BSL-1.0 |
| hiir | WTFPL |
| invoke.hpp | MIT |
| jsl | BSL-1.0 |
| kiss_fft | BSD-3-Clause |
| libaiff | MIT-style |
| pugixml | MIT |
| simde | MIT |
| spline | BSD-3-Clause |
| st_audiofile | BSD-2-Clause |
| stb_vorbis | Public domain or MIT |
| threadpool | zlib |
| tunings | MIT |
| WavPack | BSD-3-Clause |

## Build and test tools (not shipped)

| Component | Version | Licence |
|-----------|---------|---------|
| [Catch2](https://github.com/catchorg/Catch2) | v3.15.3 (downloaded by CMake) | BSL-1.0 |
| numpy, scipy | from `tools/python/requirements.txt` | BSD-3-Clause |
| [librosa](https://librosa.org) | from `tools/python/requirements.txt` | ISC |
| [mir_eval](https://github.com/mir-evaluation/mir_eval) | from `tools/python/requirements.txt` | MIT |
| soundfile | from `tools/python/requirements.txt` | BSD-3-Clause |
| [matplotlib](https://matplotlib.org) | from `tools/python/requirements.txt` | Matplotlib licence (PSF-style, BSD-compatible) |

## Evaluation data (downloaded, not committed)

| Dataset | Licence | Use |
|---|---|---|
| [vocadito](https://doi.org/10.5281/zenodo.5578807) (Bittner, Pasalo, Bosch, Meseguer Brocal and Rubinstein, 2021) | CC BY 4.0 | Scoring pitch and onset detection. Fetched by `tools/python/fetch_vocadito.py` into `testdata/external/`. |

## Sound assets

| Asset | Licence | Notes |
|-------|---------|-------|
| `assets/sfz/test/sine.sfz` | AGPLv3 (part of this repo) | Uses sfizz's built-in sine generator; no samples. |

Real drum, percussion, bass and keys kits arrive in Phase 2 and 3. They are
downloaded by a fetch script rather than committed. Prefer CC0 or CC-BY kits and
avoid non-commercial licences.
