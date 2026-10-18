#!/usr/bin/env python3
"""Writes the band's styles into assets/grooves/<style>/: MIDI patterns and style.json.

  afrobeats    modern Nigerian Afrobeats in 4/4
  worship-6-8  Nigerian worship in 6/8, led by the pad and piano, with soft drums

The drum patterns are written below as 16th-note grids, one row per drum, so they are easy
to read and change. The bass, keys and pad parts are generated from each section's chord
progression, in C major and in C minor; the band transposes them to the singer's key.

Each section's MIDI file holds five tracks: "drums" and "perc" on MIDI channel 10 with
General MIDI drum notes (the notes of assets/sfz/afro-kit), and "bass", "keys" and "pad" on
channel 1. The minor version of a section has the same drums and the minor harmony, and the
intense version, which the band plays while the singer is loud, is fuller in every part.
Fills are one bar of drums and percussion; the harmony carries on through them.

In 4/4 a bar has 16 steps (four beats of four sixteenths). In 6/8 the beat is the dotted
quarter: a bar has two beats of six sixteenths, so 12 steps.

  python tools/python/make_grooves.py

Grid symbols: "." rest, "g" ghost, "o" soft, "x" normal, "X" accent. "|" separates bars.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PPQ = 480
VELOCITY = {"g": 42, "o": 70, "x": 96, "X": 118}


@dataclass(frozen=True)
class Meter:
    numerator: int
    denominator: int
    beats_per_bar: int    # the beats a singer taps: quarters in 4/4, dotted quarters in 6/8
    steps_per_beat: int   # sixteenths
    beat_quarters: float  # the beat's length in quarter notes, MIDI's unit

    @property
    def steps_per_bar(self) -> int:
        return self.beats_per_bar * self.steps_per_beat

    @property
    def ticks_per_step(self) -> int:
        return round(PPQ * self.beat_quarters / self.steps_per_beat)


FOUR_FOUR = Meter(4, 4, 4, 4, 1.0)
SIX_EIGHT = Meter(6, 8, 2, 6, 1.5)

# The Afrobeats grid, also used by plot_groove.py.
STEPS_PER_BEAT = FOUR_FOUR.steps_per_beat
BEATS_PER_BAR = FOUR_FOUR.beats_per_bar

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

# The intense verse, while the singer is loud: the chorus's 16th hats and soft claps, and
# tambourine and busier congas.
VERSE_INTENSE = {
    "drums": {
        KICK:     "X.....x.x.....x. | X.....x.x..o..x.",
        STICK:    "...x..x...x.x... | ...x..x...x.x..g",
        CLAP:     "....o.......o... | ....o.......o...",
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
    },
}

# The intense chorus: a snare under the claps, an open hat into every bar, tambourine in 8ths
# and a pushier kick and congas.
CHORUS_INTENSE = {
    "drums": {
        KICK:     "X.....x.x.....x. | X.....x.x..o..xo",
        STICK:    "...x..x...x..... | ...x..x...x.....",
        CLAP:     "....X.......X... | ....X.......X...",
        SNARE:    "....o.......o... | ....o.......o...",
        HAT:      "ogxgogxgogxgo... | ogxgogxgogxgo...",
        OPEN_HAT: "..............x. | ..............x.",
    },
    "perc": {
        SHAKER_DOWN: "o.x.o.x.o.x.o.x. | o.x.o.x.o.x.o.x.",
        SHAKER_UP:   ".g.g.g.g.g.g.g.g | .g.g.g.g.g.g.g.g",
        TAMBOURINE:  "x.o.x.o.x.o.x.o. | x.o.x.o.x.o.x.o.",
        CONGA_MUTE:  "g.o.....g.o..... | g.o.....g.o.....",
        CONGA_OPEN:  "....x..o.o..x.o. | ....x..o.o..x..o",
        CONGA_LOW:   "..........x...x. | ..........xx..xX",
        AGOGO_HIGH:  "x.....x.....x... | x.....x.....x...",
        AGOGO_LOW:   "...o..o...o..... | ...o..o...o.....",
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

# --- Harmony ------------------------------------------------------------------------------

# One chord per bar, four-bar loops. Major: verse I-V-vi-IV, chorus IV-V-iii-vi. Minor: verse
# i-VI-III-VII, chorus VI-VII-i-i. Chords are (root pitch class, "maj" or "min"), in C.
PROGRESSIONS = {
    ("verse", "major"): [(0, "maj"), (7, "maj"), (9, "min"), (5, "maj")],
    ("chorus", "major"): [(5, "maj"), (7, "maj"), (4, "min"), (9, "min")],
    ("verse", "minor"): [(0, "min"), (8, "maj"), (3, "maj"), (10, "maj")],
    ("chorus", "minor"): [(8, "maj"), (10, "maj"), (0, "min"), (0, "min")],
}
TRIADS = {"maj": (0, 4, 7), "min": (0, 3, 7)}
BAR = FOUR_FOUR.steps_per_bar


def voicing(root: int, quality: str, centre: float) -> list[int]:
    """The close-position triad (root position or an inversion) nearest to `centre`."""
    pcs = [(root + i) % 12 for i in TRIADS[quality]]
    best = None
    for inversion in range(3):
        order = pcs[inversion:] + pcs[:inversion]
        low = int(centre) - 12
        notes = []
        for pc in order:
            n = low + ((pc - low) % 12)
            while notes and n <= notes[-1]:
                n += 12
            notes.append(n)
        while sum(notes) / 3 < centre - 6:
            notes = [n + 12 for n in notes]
        score = abs(sum(notes) / 3 - centre)
        if best is None or score < best[0]:
            best = (score, notes)
    return best[1]


def bass_part(progression, busy: bool) -> list[tuple[int, int, int, int]]:
    """(step, length in steps, note, velocity). The root locks with the kick; a fifth on 3;
    in the chorus an octave pop; each bar ends with a step towards the next chord's root."""
    notes = []
    for bar, (root, quality) in enumerate(progression):
        base = bar * BAR
        low = 36 + root if root <= 7 else 24 + root   # roots between C2 and G2, then F#1 to B1
        fifth = low + 7
        next_root = progression[(bar + 1) % len(progression)][0]
        target = 36 + next_root if next_root <= 7 else 24 + next_root
        approach = target - 1 if target > low else target + 2  # a semitone below, or a tone above
        notes += [(base + 0, 5, low, 104), (base + 6, 2, low, 86), (base + 8, 4, fifth, 94)]
        if busy:
            notes.append((base + 11, 1, low + 12, 78))
        notes.append((base + 14, 2, approach, 82))
    return notes


KEYS_FIGURES = {
    "verse": [(2, 1, 74), (6, 1, 80), (10, 1, 74), (14, 1, 80)],                                         # off-beat stabs
    "chorus": [(0, 2, 92), (3, 1, 80), (6, 2, 88), (10, 1, 80), (12, 2, 86)],                            # syncopated
    "intense": [(0, 2, 96), (3, 1, 84), (6, 2, 92), (8, 1, 80), (10, 1, 84), (12, 2, 90), (14, 1, 80)],  # denser
}


def keys_part(progression, figure: str) -> list[tuple[int, int, int, int]]:
    """Piano: a rhythm figure of close-position chords, one chord per bar."""
    notes = []
    rhythm = KEYS_FIGURES[figure]
    for bar, (root, quality) in enumerate(progression):
        chord = voicing(root, quality, 64.0)
        for step, length, velocity in rhythm:
            notes += [(bar * BAR + step, length, n, velocity) for n in chord]
    return notes


def pad_part(progression, chorus: bool) -> list[tuple[int, int, int, int]]:
    """A held chord per bar, lower than the piano; fuller in the chorus."""
    notes = []
    for bar, (root, quality) in enumerate(progression):
        chord = voicing(root, quality, 55.0)
        if chorus:
            chord = chord + [chord[0] + 12]
        notes += [(bar * BAR, BAR - 1, n, 84 if chorus else 70) for n in chord]
    return notes


def four_bars(drums: dict) -> dict:
    """A two-bar drum pattern written out twice, to match the four-bar harmony."""
    return {layer: {note: f"{grid} | {grid}" for note, grid in rows.items()} for layer, rows in drums.items()}


def section(drums: dict, name: str, mode: str, intense: bool = False) -> dict:
    """An Afrobeats section: the drums written out to four bars, and the harmony. The intense
    version plays the next figure up: a verse like a chorus, a chorus denser still."""
    progression = PROGRESSIONS[(name, mode)]
    chorus = name == "chorus"
    pattern = four_bars(drums)
    pattern["bass"] = bass_part(progression, busy=chorus or intense)
    pattern["keys"] = keys_part(progression, ("intense" if chorus else "chorus") if intense else ("chorus" if chorus else "verse"))
    pattern["pad"] = pad_part(progression, chorus or intense)
    return pattern


STYLE = {
    "name": "Afrobeats",
    "description": "Modern Nigerian Afrobeats: kick on 1, the and of 2, 3 and the and of 4, "
                   "side stick on the son clave, 16th shaker, congas.",
    "timeSignature": [BEATS_PER_BAR, 4],
    "tempo": {"default": 104, "min": 90, "max": 125},
    "swing": 0.54,
    "layers": ["drums", "perc", "bass", "keys", "pad"],
    "pitchedLayers": ["bass", "keys", "pad"],  # transposed to the key; the drums never are
    "fillLayers": ["drums", "perc"],           # what a fill replaces; the harmony carries on
    "countIn": [{"beat": b, "note": STICK, "velocity": 100 if b == 0 else 80} for b in range(BEATS_PER_BAR)],
    "entry": [{"note": CRASH, "velocity": 104}],
    # The final hits: the drums, and the tonic and fifth in the bass, piano and pad (no third,
    # so the same hits end a major or a minor song).
    "ending": [{"note": KICK, "velocity": 120}, {"note": CRASH, "velocity": 112},
               {"note": 36, "velocity": 110, "layer": "bass", "length": 2},
               *({"note": n, "velocity": 96, "layer": "keys", "length": 2} for n in (55, 60, 67)),
               *({"note": n, "velocity": 84, "layer": "pad", "length": 3} for n in (48, 55, 60))],
    "sections": [
        {"name": "Verse", "pattern": "verse.mid", "patternMinor": "verse-minor.mid",
         "patternIntense": "verse-intense.mid", "patternIntenseMinor": "verse-intense-minor.mid", "fill": "fill-verse.mid"},
        {"name": "Chorus", "pattern": "chorus.mid", "patternMinor": "chorus-minor.mid",
         "patternIntense": "chorus-intense.mid", "patternIntenseMinor": "chorus-intense-minor.mid", "fill": "fill-chorus.mid"},
    ],
    # How the band comes in. With the key known at the start: pad and piano first, the drums
    # four bars later, the bass four bars after that. Without it: the drums alone, and the
    # rest from the first four-bar boundary after the key locks.
    "stagedEntry": {
        "keyKnown": [{"bar": 0, "layers": ["pad", "keys"]}, {"bar": 4, "layers": ["drums", "perc"]}, {"bar": 8, "layers": ["bass"]}],
        "keyUnknown": [{"bar": 0, "layers": ["drums", "perc"]}],
        "afterKeyLock": {"layers": ["bass", "keys", "pad"], "everyBars": 4},
    },
}

PATTERNS = {
    "verse.mid": section(VERSE, "verse", "major"),
    "verse-minor.mid": section(VERSE, "verse", "minor"),
    "verse-intense.mid": section(VERSE_INTENSE, "verse", "major", intense=True),
    "verse-intense-minor.mid": section(VERSE_INTENSE, "verse", "minor", intense=True),
    "chorus.mid": section(CHORUS, "chorus", "major"),
    "chorus-minor.mid": section(CHORUS, "chorus", "minor"),
    "chorus-intense.mid": section(CHORUS_INTENSE, "chorus", "major", intense=True),
    "chorus-intense-minor.mid": section(CHORUS_INTENSE, "chorus", "minor", intense=True),
    "fill-verse.mid": FILL_VERSE,
    "fill-chorus.mid": FILL_CHORUS,
}


# --- Worship in 6/8 -----------------------------------------------------------------------
# Led by the pad and piano. The drums stay soft: a kick on 1, a rim on 4 and the shaker's
# eighths in the verse; a snare on 4 and hi-hat eighths in the chorus. 12 steps per bar.

WORSHIP_VERSE = {
    "drums": {
        KICK:  "X........... | X.........o.",
        STICK: "......o..... | ......o.....",
        HAT:   "o.....o..... | o.....o.....",
    },
    "perc": {
        SHAKER_DOWN: "o.g.g.o.g.g. | o.g.g.o.g.g.",
    },
}

WORSHIP_CHORUS = {
    "drums": {
        KICK:  "X.....o..... | X.....o...o.",
        SNARE: "......x..... | ......x.....",
        HAT:   "o.g.g.o.g.g. | o.g.g.o.g.g.",
    },
    "perc": {
        SHAKER_DOWN: "o.g.g.o.g.g. | o.g.g.o.g.g.",
        TAMBOURINE:  "......o..... | ......o.....",
    },
}

WORSHIP_VERSE_INTENSE = {
    "drums": {
        KICK:  "X.....o..... | X.....o...o.",
        STICK: "......x..... | ......x.....",
        HAT:   "o.g.g.o.g.g. | o.g.g.o.g.g.",
    },
    "perc": {
        SHAKER_DOWN: "o.g.g.o.g.g. | o.g.g.o.g.g.",
        TAMBOURINE:  "......o..... | ......o.....",
    },
}

WORSHIP_CHORUS_INTENSE = {
    "drums": {
        KICK:     "X.....x...o. | X.....x...o.",
        SNARE:    "......X..... | ......X....g",
        CLAP:     "......o..... | ......o.....",
        HAT:      "x.o.o.x.o.o. | x.o.o.x.o.o.",
        OPEN_HAT: "............ | ..........x.",
    },
    "perc": {
        SHAKER_DOWN: "x.o.o.x.o.o. | x.o.o.x.o.o.",
        TAMBOURINE:  "o.....x..... | o.....x.....",
    },
}

WORSHIP_FILL_VERSE = {
    "drums": {
        KICK:       "X...........",
        HIGH_TOM:   "......o.....",
        LOW_TOM:    "........o...",
        FLOOR_HIGH: "..........x.",
    },
    "perc": {
        SHAKER_DOWN: "o.g.g.o.g...",
    },
}

WORSHIP_FILL_CHORUS = {
    "drums": {
        KICK:  "X.....x.....",
        SNARE: "......x.o.xX",
        HAT:   "o.g.g.......",
    },
    "perc": {
        SHAKER_DOWN: "x.o.o.x.o...",
    },
}

# Major: verse I-IV-I-V, chorus IV-I-V-vi. Minor: verse i-iv-VI-V (with the major dominant
# of church harmony), chorus VI-VII-i-i.
WORSHIP_PROGRESSIONS = {
    ("verse", "major"): [(0, "maj"), (5, "maj"), (0, "maj"), (7, "maj")],
    ("chorus", "major"): [(5, "maj"), (0, "maj"), (7, "maj"), (9, "min")],
    ("verse", "minor"): [(0, "min"), (5, "min"), (8, "maj"), (7, "maj")],
    ("chorus", "minor"): [(8, "maj"), (10, "maj"), (0, "min"), (0, "min")],
}
W_BAR = SIX_EIGHT.steps_per_bar


def worship_bass(progression, busy: bool) -> list[tuple[int, int, int, int]]:
    """The root on 1, the fifth on 4 and a step to the next chord on the last eighth; busy
    adds the octave on the third eighth."""
    notes = []
    for bar, (root, quality) in enumerate(progression):
        base = bar * W_BAR
        low = 36 + root if root <= 7 else 24 + root
        next_root = progression[(bar + 1) % len(progression)][0]
        target = 36 + next_root if next_root <= 7 else 24 + next_root
        approach = target - 1 if target > low else target + 2
        if busy:
            notes += [(base, 3, low, 96), (base + 3, 3, low + 12, 74)]
        else:
            notes.append((base, 6, low, 94))
        notes += [(base + 6, 4, low + 7, 82), (base + 10, 2, approach, 74)]
    return notes


WORSHIP_KEYS = {
    # (step, length, which chord note or "chord", velocity): eighth-note arpeggios in the verse,
    # chords on 1, 4 and the last eighth in the chorus, every eighth when intense.
    "arpeggio": [(0, 2, 0, 74), (2, 2, 1, 60), (4, 2, 2, 64), (6, 2, 1, 70), (8, 2, 2, 60), (10, 2, 1, 64)],
    "block": [(0, 5, "chord", 84), (6, 3, "chord", 78), (10, 2, "chord", 70)],
    "intense": [(0, 2, "chord", 90), (2, 2, "chord", 66), (4, 2, "chord", 72), (6, 2, "chord", 84), (8, 2, "chord", 66), (10, 2, "chord", 74)],
}


def worship_keys(progression, figure: str) -> list[tuple[int, int, int, int]]:
    notes = []
    for bar, (root, quality) in enumerate(progression):
        chord = voicing(root, quality, 64.0)
        for step, length, which, velocity in WORSHIP_KEYS[figure]:
            pitches = chord if which == "chord" else [chord[which]]
            notes += [(bar * W_BAR + step, length, n, velocity) for n in pitches]
    return notes


def worship_pad(progression, full: bool) -> list[tuple[int, int, int, int]]:
    notes = []
    for bar, (root, quality) in enumerate(progression):
        chord = voicing(root, quality, 55.0)
        if full:
            chord = chord + [chord[0] + 12]
        notes += [(bar * W_BAR, W_BAR - 1, n, 76 if full else 62) for n in chord]
    return notes


def worship_section(drums: dict, name: str, mode: str, intense: bool = False) -> dict:
    progression = WORSHIP_PROGRESSIONS[(name, mode)]
    chorus = name == "chorus"
    pattern = four_bars(drums)
    pattern["bass"] = worship_bass(progression, busy=intense)
    pattern["keys"] = worship_keys(progression, ("intense" if chorus else "block") if intense else ("block" if chorus else "arpeggio"))
    pattern["pad"] = worship_pad(progression, chorus or intense)
    return pattern


WORSHIP_STYLE = {
    "name": "Worship 6/8",
    "description": "Nigerian worship in 6/8, led by the pad and piano: soft drums with a kick on 1 and a rim "
                   "or snare on 4, shaker eighths, piano arpeggios in the verse.",
    "timeSignature": [6, 8],
    "tempo": {"default": 66, "min": 50, "max": 84},  # dotted quarters per minute
    "swing": 0.5,
    "layers": ["drums", "perc", "bass", "keys", "pad"],
    "pitchedLayers": ["bass", "keys", "pad"],
    "fillLayers": ["drums", "perc"],
    # Six sticks on the eighths, the two beats louder. Beats are dotted quarters.
    "countIn": [{"beat": round(i / 3, 6), "note": STICK, "velocity": 100 if i == 0 else 88 if i == 3 else 64} for i in range(6)],
    "entry": [{"note": CRASH, "velocity": 84}],
    "ending": [{"note": KICK, "velocity": 104}, {"note": CRASH, "velocity": 92},
               {"note": 36, "velocity": 100, "layer": "bass", "length": 2},
               *({"note": n, "velocity": 84, "layer": "keys", "length": 2} for n in (55, 60, 67)),
               *({"note": n, "velocity": 76, "layer": "pad", "length": 3} for n in (48, 55, 60))],
    "sections": [
        {"name": "Verse", "pattern": "verse.mid", "patternMinor": "verse-minor.mid",
         "patternIntense": "verse-intense.mid", "patternIntenseMinor": "verse-intense-minor.mid", "fill": "fill-verse.mid"},
        {"name": "Chorus", "pattern": "chorus.mid", "patternMinor": "chorus-minor.mid",
         "patternIntense": "chorus-intense.mid", "patternIntenseMinor": "chorus-intense-minor.mid", "fill": "fill-chorus.mid"},
    ],
    # Pad and piano first, then the bass and shaker, then the soft drums. Without a key: the
    # drums and shaker, and the harmony from the first four-bar boundary after the key locks.
    # Led by the piano and pad: with the soft drums at 0 LU, the piano plays at about +2 LU,
    # the pad and bass at -2 LU (the instruments' own levels are set for Afrobeats).
    "mix": {"keys": 4.0, "pad": 6.0, "bass": -2.0},
    "stagedEntry": {
        "keyKnown": [{"bar": 0, "layers": ["pad", "keys"]}, {"bar": 4, "layers": ["bass", "perc"]}, {"bar": 8, "layers": ["drums"]}],
        "keyUnknown": [{"bar": 0, "layers": ["drums", "perc"]}],
        "afterKeyLock": {"layers": ["bass", "keys", "pad"], "everyBars": 4},
    },
}

WORSHIP_PATTERNS = {
    "verse.mid": worship_section(WORSHIP_VERSE, "verse", "major"),
    "verse-minor.mid": worship_section(WORSHIP_VERSE, "verse", "minor"),
    "verse-intense.mid": worship_section(WORSHIP_VERSE_INTENSE, "verse", "major", intense=True),
    "verse-intense-minor.mid": worship_section(WORSHIP_VERSE_INTENSE, "verse", "minor", intense=True),
    "chorus.mid": worship_section(WORSHIP_CHORUS, "chorus", "major"),
    "chorus-minor.mid": worship_section(WORSHIP_CHORUS, "chorus", "minor"),
    "chorus-intense.mid": worship_section(WORSHIP_CHORUS_INTENSE, "chorus", "major", intense=True),
    "chorus-intense-minor.mid": worship_section(WORSHIP_CHORUS_INTENSE, "chorus", "minor", intense=True),
    "fill-verse.mid": WORSHIP_FILL_VERSE,
    "fill-chorus.mid": WORSHIP_FILL_CHORUS,
}

# Every style: (folder, style.json contents, patterns, meter).
STYLES = [
    ("afrobeats", STYLE, PATTERNS, FOUR_FOUR),
    ("worship-6-8", WORSHIP_STYLE, WORSHIP_PATTERNS, SIX_EIGHT),
]


def parse_grid(grid: str, meter: Meter = FOUR_FOUR) -> list[tuple[int, int]]:
    """(step, velocity) pairs of a grid string."""
    bars = [b.strip() for b in grid.split("|")]
    steps_per_bar = meter.steps_per_bar
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


def bar_count(pattern: dict, meter: Meter = FOUR_FOUR) -> int:
    counts = {len(grid.split("|")) for rows in pattern.values() if isinstance(rows, dict) for grid in rows.values()}
    if len(counts) != 1:
        raise ValueError(f"rows have different lengths: {counts}")
    bars = counts.pop()
    for rows in pattern.values():
        if isinstance(rows, list) and any(step + length > bars * meter.steps_per_bar for step, length, _, _ in rows):
            raise ValueError("a pitched part runs past the end of the pattern")
    return bars


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


def write_midi(path: Path, pattern: dict, bpm: float, meter: Meter = FOUR_FOUR) -> None:
    bars = bar_count(pattern, meter)
    length = round(bars * meter.beats_per_bar * meter.beat_quarters * PPQ)
    ticks_per_step = meter.ticks_per_step
    gate = ticks_per_step // 2  # drums are one-shot; the length only has to be short

    tempo = int(round(60_000_000 / (bpm * meter.beat_quarters)))  # MIDI counts quarter notes
    signature = bytes([meter.numerator, meter.denominator.bit_length() - 1, round(24 * meter.beat_quarters), 8])
    conductor = [
        (0, meta_text(0x03, path.stem)),
        (0, b"\xff\x58\x04" + signature),
        (0, b"\xff\x51\x03" + tempo.to_bytes(3, "big")),
        (length, meta_text(0x06, "loop end")),  # marks the pattern length even when the last steps are rests
    ]
    chunks = [track_chunk(conductor)]
    for layer, rows in pattern.items():
        events = [(0, meta_text(0x03, layer))]
        if isinstance(rows, dict):  # drum grids, channel 10
            for note, grid in rows.items():
                for step, velocity in parse_grid(grid, meter):
                    tick = step * ticks_per_step
                    events.append((tick, bytes([0x99, note, velocity])))
                    events.append((tick + gate, bytes([0x89, note, 0])))
        else:  # pitched notes, channel 1; a little shorter than written, so repeats re-strike
            for step, length, note, velocity in rows:
                tick = step * ticks_per_step
                events.append((tick, bytes([0x90, note, velocity])))
                events.append((tick + length * ticks_per_step - 10, bytes([0x80, note, 0])))
        chunks.append(track_chunk(events))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    path.write_bytes(header + b"".join(chunks))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "assets" / "grooves", help="the folder that holds the style folders")
    args = parser.parse_args()

    for folder, style, patterns, meter in STYLES:
        out = args.out / folder
        out.mkdir(parents=True, exist_ok=True)
        for section in style["sections"]:
            if bar_count(patterns[section["fill"]], meter) != 1:
                raise ValueError(f"{folder}/{section['fill']} must be one bar")
        for name, pattern in patterns.items():
            write_midi(out / name, pattern, style["tempo"]["default"], meter)
        (out / "style.json").write_text(json.dumps(style, indent=2) + "\n")
        print(f"wrote {len(patterns)} patterns and style.json to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
