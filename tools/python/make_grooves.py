#!/usr/bin/env python3
"""Writes the Afrobeats groove: assets/grooves/afrobeats/*.mid and style.json.

The patterns are written below as 16th-note grids, one row per drum, so they are easy
to read and change. Each MIDI file has two tracks, "drums" and "perc", both on MIDI
channel 10 with General MIDI drum notes (the notes of assets/sfz/afro-kit). The app
can mute either layer.

  python tools/python/make_grooves.py

Grid symbols: "." rest, "g" ghost, "o" soft, "x" normal, "X" accent. "|" separates bars.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PPQ = 480
STEPS_PER_BEAT = 4
BEATS_PER_BAR = 4
VELOCITY = {"g": 42, "o": 70, "x": 96, "X": 118}

# GM drum notes used below.
KICK, STICK, SNARE, CLAP, FLOOR_LOW, HAT, FLOOR_HIGH, LOW_TOM, OPEN_HAT, HIGH_TOM = 36, 37, 38, 39, 41, 42, 43, 45, 46, 48
CRASH, TAMBOURINE = 49, 54
CONGA_MUTE, CONGA_OPEN, CONGA_LOW, AGOGO_HIGH, AGOGO_LOW, SHAKER_DOWN, SHAKER_UP = 62, 63, 64, 67, 68, 70, 82

# Kick on 1, the "and" of 2, 3 and the "and" of 4. The side stick plays the son
# clave without its first stroke, which the kick covers. The shaker runs in 16ths
# with its accents on the off-beat 8ths.
VERSE = {
    "drums": {
        KICK:   "X.....x.x.....x. | X.....x.x..o..x.",
        STICK:  "...x..x...x.x... | ...x..x...x.x..g",
        HAT:    "o.x.o.x.o.x.o.x. | o.x.o.x.o.x.o.x.",
    },
    "perc": {
        SHAKER_DOWN: "o.x.o.x.o.x.o.x. | o.x.o.x.o.x.o.x.",
        SHAKER_UP:   ".g.g.g.g.g.g.g.g | .g.g.g.g.g.g.g.g",
        CONGA_MUTE:  "g.o.....g.o..... | g.o.....g.o.....",
        CONGA_OPEN:  "....x..o....x... | ....x..o....x..o",
        CONGA_LOW:   "..........x...x. | ..........x...x.",
    },
}

# The chorus adds claps on 2 and 4, 16th hats with an open hat into the next bar,
# tambourine and agogo.
CHORUS = {
    "drums": {
        KICK:     "X.....x.x.....x. | X.....x.x..o..x.",
        STICK:    "...x..x...x..... | ...x..x...x.....",
        CLAP:     "....X.......X... | ....X.......X...",
        HAT:      "ogxgogxgogxgogxg | ogxgogxgogxgo...",
        OPEN_HAT: "................ | ..............x.",
    },
    "perc": {
        SHAKER_DOWN: "o.x.o.x.o.x.o.x. | o.x.o.x.o.x.o.x.",
        SHAKER_UP:   ".g.g.g.g.g.g.g.g | .g.g.g.g.g.g.g.g",
        TAMBOURINE:  "....x.......x... | ....x.......x...",
        CONGA_MUTE:  "g.o.....g.o..... | g.o.....g.o.....",
        CONGA_OPEN:  "....x..o.o..x... | ....x..o.o..x..o",
        CONGA_LOW:   "..........x...x. | ..........xx..x.",
        AGOGO_HIGH:  "x.....x.....x... | x.....x.....x...",
        AGOGO_LOW:   "...o......o..... | ...o......o.....",
    },
}

# Fills are one bar. The verse fill builds on the snare; the chorus fill runs down the toms.
FILL_VERSE = {
    "drums": {
        KICK:  "X.....x.........",
        STICK: "...x..x.........",
        HAT:   "o.x.o.x.........",
        SNARE: "..........oxxxXX",
    },
    "perc": {
        SHAKER_DOWN: "o.x.o.x.o.x.....",
        SHAKER_UP:   ".g.g.g.g.g.g....",
        CONGA_OPEN:  "....x.......xx..",
        CONGA_LOW:   "..............xX",
    },
}

FILL_CHORUS = {
    "drums": {
        KICK:       "X.....x.x.....X.",
        CLAP:       "....X...........",
        HAT:        "ogxgogxg........",
        HIGH_TOM:   "........xx......",
        LOW_TOM:    "..........xx....",
        FLOOR_HIGH: "............xx..",
        FLOOR_LOW:  "..............XX",
    },
    "perc": {
        SHAKER_DOWN: "o.x.o.x.........",
        SHAKER_UP:   ".g.g.g.g........",
        CONGA_OPEN:  "....x..o.x.x....",
        CONGA_LOW:   "............x.X.",
    },
}

STYLE = {
    "name": "Afrobeats",
    "description": "Modern Nigerian Afrobeats: kick on 1, the and of 2, 3 and the and of 4, "
                   "side stick on the son clave, 16th shaker, congas.",
    "timeSignature": [BEATS_PER_BAR, 4],
    "tempo": {"default": 104, "min": 90, "max": 125},
    "swing": 0.54,
    "layers": ["drums", "perc"],
    "countIn": [{"beat": b, "note": STICK, "velocity": 100 if b == 0 else 80} for b in range(BEATS_PER_BAR)],
    "entry": [{"note": CRASH, "velocity": 104}],
    "ending": [{"note": KICK, "velocity": 120}, {"note": CRASH, "velocity": 112}],
    "sections": [
        {"name": "Verse", "pattern": "verse.mid", "fill": "fill-verse.mid"},
        {"name": "Chorus", "pattern": "chorus.mid", "fill": "fill-chorus.mid"},
    ],
}

PATTERNS = {
    "verse.mid": VERSE,
    "chorus.mid": CHORUS,
    "fill-verse.mid": FILL_VERSE,
    "fill-chorus.mid": FILL_CHORUS,
}


def parse_grid(grid: str) -> list[tuple[int, int]]:
    """(step, velocity) pairs of a grid string."""
    bars = [b.strip() for b in grid.split("|")]
    steps_per_bar = STEPS_PER_BEAT * BEATS_PER_BAR
    hits = []
    for bar_index, bar in enumerate(bars):
        if len(bar) != steps_per_bar:
            raise ValueError(f"bar {bar_index + 1} of {grid!r} has {len(bar)} steps, not {steps_per_bar}")
        for step, symbol in enumerate(bar):
            if symbol == ".":
                continue
            if symbol not in VELOCITY:
                raise ValueError(f"unknown symbol {symbol!r} in {grid!r}")
            hits.append((bar_index * steps_per_bar + step, VELOCITY[symbol]))
    return hits


def bar_count(pattern: dict) -> int:
    counts = {len(grid.split("|")) for rows in pattern.values() for grid in rows.values()}
    if len(counts) != 1:
        raise ValueError(f"rows have different lengths: {counts}")
    return counts.pop()


def variable_length(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def track_chunk(events: list[tuple[int, bytes]]) -> bytes:
    """events: (absolute tick, message bytes); end of track is added."""
    body = bytearray()
    now = 0
    for tick, message in sorted(events, key=lambda e: (e[0], e[1][0] & 0xF0 != 0x80)):  # offs before ons
        body += variable_length(tick - now) + message
        now = tick
    body += variable_length(0) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def meta_text(kind: int, text: str) -> bytes:
    data = text.encode("ascii")
    return bytes([0xFF, kind]) + variable_length(len(data)) + data


def write_midi(path: Path, pattern: dict, bpm: float) -> None:
    bars = bar_count(pattern)
    length = bars * BEATS_PER_BAR * PPQ
    ticks_per_step = PPQ // STEPS_PER_BEAT
    gate = ticks_per_step // 2  # drums are one-shot; the length only has to be short

    tempo = int(round(60_000_000 / bpm))
    conductor = [
        (0, meta_text(0x03, path.stem)),
        (0, b"\xff\x58\x04" + bytes([BEATS_PER_BAR, 2, 24, 8])),  # 4/4
        (0, b"\xff\x51\x03" + tempo.to_bytes(3, "big")),
        (length, meta_text(0x06, "loop end")),  # marks the pattern length even when the last steps are rests
    ]
    chunks = [track_chunk(conductor)]
    for layer, rows in pattern.items():
        events = [(0, meta_text(0x03, layer))]
        for note, grid in rows.items():
            for step, velocity in parse_grid(grid):
                tick = step * ticks_per_step
                events.append((tick, bytes([0x99, note, velocity])))
                events.append((tick + gate, bytes([0x89, note, 0])))
        chunks.append(track_chunk(events))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    path.write_bytes(header + b"".join(chunks))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "assets" / "grooves" / "afrobeats")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    for section in STYLE["sections"]:
        if bar_count(PATTERNS[section["fill"]]) != 1:
            raise ValueError(f"{section['fill']} must be one bar")
    for name, pattern in PATTERNS.items():
        write_midi(args.out / name, pattern, STYLE["tempo"]["default"])
    (args.out / "style.json").write_text(json.dumps(STYLE, indent=2) + "\n")
    print(f"wrote {len(PATTERNS)} patterns and style.json to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
