#!/usr/bin/env python3
"""Builds the Afro drum and percussion kit, assets/sfz/afro-kit/afro-kit.sfz.

The kit maps sounds to General MIDI drum notes, so the grooves are ordinary GM drum
MIDI files. It is made of:

  - a synthesized Afrobeats kick, generated here and committed (kick/*.wav);
  - percussion and kit pieces from the Versilian Community Sample Library (VCSL,
    CC0, https://github.com/sgossner/VCSL), downloaded from a pinned commit into
    vcsl/ and not committed.

This script picks the VCSL regions, balances their levels by measurement and writes
three files: afro-kit.sfz, samples.json (the list of VCSL files with checksums that
tools/python/fetch_assets.py downloads) and the kick samples. Run it only to change
the kit; to use the kit, run fetch_assets.py.

  python tools/python/make_afro_kit.py
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf

VCSL_COMMIT = "dfcf4a4918771eee884b96ad4493de82ef84daf6"  # branch "sfz": samples plus SFZ mappings
VCSL_RAW = f"https://raw.githubusercontent.com/sgossner/VCSL/{VCSL_COMMIT}/"
REPO_ROOT = Path(__file__).resolve().parents[2]
KIT_DIR = REPO_ROOT / "assets" / "sfz" / "afro-kit"

# VCSL source instruments: local folder name -> SFZ file in the VCSL tree.
SOURCES = {
    "agogo": "Idiophones/Struck Idiophones/Agogo Bells.sfz",
    "bongos": "Membranophones/Struck Membranophones/Bongos.sfz",
    "cabasa": "Idiophones/Struck Idiophones/Cabasa.sfz",
    "claps": "Idiophones/Struck Idiophones/Claps.sfz",
    "claves": "Idiophones/Struck Idiophones/Claves.sfz",
    "conga": "Membranophones/Struck Membranophones/Conga.sfz",
    "cowbell": "Idiophones/Struck Idiophones/Cowbells.sfz",
    "cymbal": "Idiophones/Struck Idiophones/Suspended Cymbal 1.sfz",
    "hihat": "Idiophones/Struck Idiophones/Hi-Hat Cymbal.sfz",
    "shaker": "Idiophones/Struck Idiophones/Shaker, Small.sfz",
    "snare": "Membranophones/Struck Membranophones/Snare Drum, Modern 1.sfz",
    "tambourine": "Idiophones/Struck Idiophones/Tambourine 1.sfz",
    "tom-high": "Membranophones/Struck Membranophones/Tom 1.sfz",
    "tom-low": "Membranophones/Struck Membranophones/Tom 2.sfz",
    "woodblock": "Idiophones/Struck Idiophones/Woodblock.sfz",
}

HAT_CHOKE = 1  # closed and pedal hats cut the open hat


@dataclass(frozen=True)
class Piece:
    note: int  # GM drum note in this kit
    name: str
    source: str  # a SOURCES key, or "kick" for the synthesized kick
    source_key: int  # the key of the regions in the VCSL SFZ file
    level_db: float  # loudness at full velocity: the loudest 50 ms RMS, dBFS
    tune_cents: int = 0
    veltrack: int = 100  # SFZ amp_veltrack: 100 means velocity 64 is about 12 dB down
    polyphony: int = 4  # voices per note; old ones fade when exceeded
    choke_group: int = 0  # this piece's group
    choked_by: int = 0  # the group that silences it


# Levels set the mix inside the kit: kick and snare lead, the hi-hat sits about 15 dB under
# the kick, and the shaker and congas carry the Afrobeats bounce just behind the drums.
KIT = [
    Piece(36, "Kick", "kick", 0, -9.0, veltrack=60),
    Piece(37, "Side stick", "snare", 64, -16.0, veltrack=70),
    Piece(38, "Snare", "snare", 61, -12.0),
    Piece(39, "Clap", "claps", 60, -13.0, veltrack=70),
    Piece(40, "Snare, snares off", "snare", 60, -13.0),
    Piece(41, "Low floor tom", "tom-low", 62, -13.0, tune_cents=-400),
    Piece(42, "Closed hi-hat", "hihat", 42, -21.0, veltrack=80, polyphony=3, choke_group=HAT_CHOKE),
    Piece(43, "High floor tom", "tom-low", 62, -13.0),
    Piece(44, "Pedal hi-hat", "hihat", 44, -23.0, polyphony=2, choke_group=HAT_CHOKE),
    Piece(45, "Low tom", "tom-high", 62, -13.0, tune_cents=-300),
    Piece(46, "Open hi-hat", "hihat", 46, -20.0, polyphony=2, choked_by=HAT_CHOKE),
    Piece(47, "Low-mid tom", "tom-high", 62, -13.0, tune_cents=-150),
    Piece(48, "High-mid tom", "tom-high", 62, -13.0),
    Piece(49, "Crash cymbal", "cymbal", 68, -17.0, polyphony=2),
    Piece(51, "Ride cymbal", "cymbal", 70, -21.0, polyphony=3),
    Piece(53, "Ride bell", "cymbal", 69, -20.0, polyphony=2),
    Piece(54, "Tambourine", "tambourine", 60, -19.0),
    Piece(56, "Cowbell", "cowbell", 60, -18.0),
    Piece(60, "High bongo", "bongos", 60, -15.0),
    Piece(61, "Low bongo", "bongos", 63, -15.0),
    Piece(62, "Muted high conga", "conga", 62, -14.0),
    Piece(63, "Open high conga", "conga", 63, -13.0),
    Piece(64, "Low conga", "conga", 61, -13.0),
    Piece(67, "High agogo", "agogo", 60, -18.0),
    Piece(68, "Low agogo", "agogo", 61, -18.0),
    Piece(69, "Cabasa", "cabasa", 60, -20.0),
    Piece(70, "Shaker, down stroke", "shaker", 64, -18.0, veltrack=80),
    Piece(75, "Claves", "claves", 60, -19.0),
    Piece(76, "High woodblock", "woodblock", 60, -19.0),
    Piece(77, "Low woodblock", "woodblock", 62, -19.0),
    Piece(82, "Shaker, up stroke", "shaker", 65, -18.0, veltrack=80),
]


@dataclass
class Region:
    sample: str  # path inside the VCSL tree
    lovel: int
    hivel: int
    seq_length: int
    seq_position: int
    offset: int
    volume: float


def fetch(url_path: str, destination: Path) -> Path:
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        url = VCSL_RAW + urllib.parse.quote(url_path)
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
        destination.write_bytes(data)
    return destination


def parse_vcsl_sfz(text: str, folder: str) -> list[tuple[int, Region]]:
    """The (key, region) pairs of a VCSL SFZ file. VCSL files are flat: <group> then <region>s."""
    regions: list[dict[str, str]] = []
    group: dict[str, str] = {}
    scope = None
    for raw in text.splitlines():
        line = raw.split("//", 1)[0].strip()
        if not line:
            continue
        if line.startswith("<"):
            scope = line
            if line == "<group>":
                group = {}
            elif line == "<region>":
                regions.append(dict(group))
            continue
        key, _, value = line.partition("=")
        if scope == "<group>":
            group[key] = value
        elif scope == "<region>":
            regions[-1][key] = value
    out = []
    for r in regions:
        if r.get("lokey") != r.get("hikey"):
            raise ValueError(f"unexpected key range in {r}")
        out.append((int(r["lokey"]), Region(
            sample=f"{folder}/{r['sample']}",
            lovel=int(r.get("lovel", 0)),
            hivel=int(r.get("hivel", 127)),
            seq_length=int(r.get("seq_length", 1)),
            seq_position=int(r.get("seq_position", 1)),
            offset=int(r.get("offset", 0)),
            volume=float(r.get("volume", 0.0)),
        )))
    return out


def loudest_rms_db(audio: np.ndarray, rate: int, window_s: float = 0.05) -> float:
    """The loudest RMS over a sliding window, in dBFS, of a mono or stereo signal."""
    power = np.mean(np.square(audio.reshape(len(audio), -1)), axis=1)
    n = max(1, int(window_s * rate))
    if len(power) < n:
        return 10 * np.log10(max(power.mean(), 1e-20))
    cumulative = np.concatenate([[0.0], np.cumsum(power)])
    windowed = (cumulative[n:] - cumulative[:-n]) / n
    return 10 * np.log10(max(windowed.max(), 1e-20))


def local_name(source: str, sample: str) -> str:
    return f"vcsl/{source}/{sample.rsplit('/', 1)[1]}"


# --- The synthesized kick ----------------------------------------------------------------

def synth_kick(rate: int, seed: int, end_hz: float, decay_s: float) -> np.ndarray:
    """A deep, short Afrobeats kick: a sine falling from 165 Hz to end_hz, a soft knock and a click."""
    rng = np.random.default_rng(seed)
    length = int(0.65 * rate)
    t = np.arange(length) / rate
    freq = end_hz + (165.0 - end_hz) * np.exp(-t / 0.028)
    phase = 2 * np.pi * np.cumsum(freq) / rate
    body = np.sin(phase) * np.exp(-t / decay_s)
    body = np.tanh(1.8 * body) / np.tanh(1.8)  # some harmonics, so small speakers still hear it

    knock = np.sin(2 * np.pi * 330.0 * t) * np.exp(-t / 0.008) * 0.25
    noise = rng.standard_normal(length)
    spectrum = np.fft.rfft(noise)
    bins = np.fft.rfftfreq(length, 1.0 / rate)
    spectrum *= np.exp(-0.5 * (np.log2(np.maximum(bins, 1.0) / 4000.0) / 0.7) ** 2)  # band around 4 kHz
    click = np.fft.irfft(spectrum, length)
    click *= np.exp(-t / 0.0025) * 0.35 / np.max(np.abs(click[: int(0.004 * rate)]))

    kick = body + knock + click
    attack = np.minimum(1.0, t / 0.0008)
    release = np.clip((0.65 - t) / 0.08, 0.0, 1.0) ** 2
    kick *= attack * release
    return (kick / np.max(np.abs(kick)) * 0.89).astype(np.float32)


KICKS = [
    ("kick/afro-kick-rr1.wav", dict(seed=1, end_hz=52.0, decay_s=0.26)),
    ("kick/afro-kick-rr2.wav", dict(seed=2, end_hz=51.0, decay_s=0.25)),
]


# --- SFZ output ----------------------------------------------------------------------------

def sfz_region_lines(piece: Piece, sample: str, volume: float, lovel: int, hivel: int,
                     seq_length: int, seq_position: int, offset: int) -> list[str]:
    opcodes = [f"key={piece.note}", f"lovel={lovel}", f"hivel={hivel}"]
    if seq_length > 1:
        opcodes += [f"seq_length={seq_length}", f"seq_position={seq_position}"]
    if offset:
        opcodes.append(f"offset={offset}")
    opcodes.append(f"volume={volume:.2f}")
    return [f"<region> {' '.join(opcodes)}", f"sample={sample}"]


def write_sfz(path: Path, blocks: list[tuple[Piece, list[str]]]) -> None:
    lines = [
        "// LiveBand Afro kit: General MIDI drum notes.",
        "// Generated by tools/python/make_afro_kit.py; edit that script, not this file.",
        "// Kick: synthesized by the script (AGPLv3, part of this repository).",
        f"// Everything else: Versilian Community Sample Library (CC0), commit {VCSL_COMMIT[:12]};",
        "// download those samples with tools/python/fetch_assets.py.",
        "",
        "<control>",
        "hint_ram_based=1",  # the whole kit (about 80 MB as floats) in memory: no disk reads while playing
        "",
        "<global>",
        "loop_mode=one_shot",  # drums ignore note-off; the pattern player still sends them
        "pitch_keytrack=0",
        "ampeg_attack=0.001",
        "ampeg_release=0.05",
        "off_mode=time",
        "off_time=0.04",
        "",
    ]
    for piece, regions in blocks:
        group = [f"<group> // {piece.note} {piece.name}", f"note_polyphony={piece.polyphony}",
                 f"amp_veltrack={piece.veltrack}"]
        if piece.tune_cents:
            group.append(f"tune={piece.tune_cents}")
        if piece.choke_group:
            group.append(f"group={piece.choke_group}")
        if piece.choked_by:
            group.append(f"off_by={piece.choked_by}")
        lines += group + regions + [""]
    path.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kit-dir", type=Path, default=KIT_DIR)
    parser.add_argument("--work-dir", type=Path, default=None,
                        help="where to cache the VCSL SFZ files (default: <kit-dir>/vcsl/.sfz)")
    args = parser.parse_args()
    kit_dir: Path = args.kit_dir
    work = args.work_dir or kit_dir / "vcsl" / ".sfz"
    rate = 48000

    for name, settings in KICKS:
        target = kit_dir / name
        target.parent.mkdir(parents=True, exist_ok=True)
        sf.write(target, synth_kick(rate, **settings), rate, subtype="PCM_24")

    sources: dict[str, list[tuple[int, Region]]] = {}
    for short, sfz_path in SOURCES.items():
        text = fetch(sfz_path, work / f"{short}.sfz").read_text()
        sources[short] = parse_vcsl_sfz(text, sfz_path.rsplit("/", 1)[0])

    manifest: dict[str, dict] = {}
    blocks: list[tuple[Piece, list[str]]] = []
    report = []
    for piece in KIT:
        if piece.source == "kick":
            measured = max(loudest_rms_db(*sf.read(kit_dir / name, always_2d=True)) for name, _ in KICKS)
            trim = piece.level_db - measured
            lines = []
            for i, (name, _) in enumerate(KICKS):
                lines += sfz_region_lines(piece, name, trim, 0, 127, len(KICKS), i + 1, 0)
            blocks.append((piece, lines))
            report.append((piece, measured, trim, len(KICKS)))
            continue

        regions = [r for key, r in sources[piece.source] if key == piece.source_key]
        if not regions:
            raise ValueError(f"no regions for key {piece.source_key} in {piece.source}")
        for r in regions:
            name = local_name(piece.source, r.sample)
            path = fetch(r.sample, kit_dir / name)
            data = path.read_bytes()
            manifest[name] = {"source": r.sample, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}

        # Balance on the loudest velocity layer, keeping VCSL's per-layer gains so the
        # layers stay in proportion.
        top = max(r.hivel for r in regions)
        loudest = []
        for r in regions:
            if r.hivel != top:
                continue
            audio, sr = sf.read(kit_dir / local_name(piece.source, r.sample), always_2d=True)
            loudest.append(loudest_rms_db(audio[r.offset:], sr) + r.volume)
        measured = float(np.mean(loudest))
        trim = piece.level_db - measured
        lines = []
        for r in sorted(regions, key=lambda r: (r.lovel, r.seq_position)):
            lines += sfz_region_lines(piece, local_name(piece.source, r.sample), r.volume + trim,
                                      r.lovel, r.hivel, r.seq_length, r.seq_position, r.offset)
        blocks.append((piece, lines))
        report.append((piece, measured, trim, len(regions)))

    write_sfz(kit_dir / "afro-kit.sfz", blocks)
    (kit_dir / "samples.json").write_text(json.dumps({
        "description": "VCSL samples used by afro-kit.sfz. Downloaded by tools/python/fetch_assets.py.",
        "licence": "CC0 1.0 (Versilian Community Sample Library, https://github.com/sgossner/VCSL)",
        "base_url": VCSL_RAW,
        "files": dict(sorted(manifest.items())),
    }, indent=2) + "\n")

    total = sum(f["bytes"] for f in manifest.values())
    print(f"{'note':>4}  {'piece':<22} {'regions':>7} {'measured':>9} {'trim':>7}")
    for piece, measured, trim, count in report:
        print(f"{piece.note:>4}  {piece.name:<22} {count:>7} {measured:>8.1f}  {trim:>+6.1f}")
    print(f"{len(manifest)} VCSL files, {total / 1e6:.1f} MB; wrote {kit_dir / 'afro-kit.sfz'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
