#!/usr/bin/env python3
"""Draws what the band played, from lb-render's --events file: the bass, piano and pad notes
as a piano roll, and the drum and percussion hits as a strip below, with bar lines and
labels. It shows the staged entry, the transposition and key changes at a glance.

  lb-render ... --key-at 1:Em --key-at 9:G --plan Verse:8,Chorus:6 --events band.csv
  python tools/python/plot_harmony.py --events band.csv --bpm 104 \\
      --mark "4:E minor: bass, piano and pad come in" --mark "10:G major" --out band.png
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

# Reference palette, light mode (validated for colour-vision deficiency in Phase 1).
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRID = "#e1e0d9"
BAR_LINE = "#c3c2b7"
PITCHED = {"bass": "#2a78d6", "keys": "#eb6834", "pad": "#1baf7a"}
NAMES = {"bass": "Bass", "keys": "Piano", "pad": "Pad"}
NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def read_notes(path: Path):
    """(start, end, note, velocity, layer) for every note in the events file."""
    open_notes, notes = {}, []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            key = (row["layer"], int(row["note"]))
            t = float(row["time_s"])
            if row["on"] == "1":
                open_notes[key] = (t, int(row["velocity"]))
            elif key in open_notes:
                start, velocity = open_notes.pop(key)
                notes.append((start, t, key[1], velocity, key[0]))
    return notes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--events", type=Path, required=True)
    parser.add_argument("--bpm", type=float, required=True)
    parser.add_argument("--downbeat", type=float, default=128 / 48000, help="seconds to the first downbeat (lb-render: one block)")
    parser.add_argument("--beats-per-bar", type=int, default=4)
    parser.add_argument("--mark", action="append", default=[], help="bar:text, a label at a bar line (may repeat)")
    parser.add_argument("--title", default="What the band played")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    notes = read_notes(args.events)
    bar_seconds = 60.0 / args.bpm * args.beats_per_bar
    to_bar = lambda t: (t - args.downbeat) / bar_seconds  # noqa: E731
    bars = int(max(to_bar(end) for _, end, *_ in notes)) + 1

    fig, (roll, hits) = plt.subplots(2, 1, figsize=(13, 6.2), sharex=True, facecolor=SURFACE,
                                     gridspec_kw={"height_ratios": [5, 1], "hspace": 0.08})
    pitched = [n for n in notes if n[4] in PITCHED]
    low = min(n[2] for n in pitched) - 2
    high = max(n[2] for n in pitched) + 2
    for ax in (roll, hits):
        ax.set_facecolor(SURFACE)
        for spine in ax.spines.values():
            spine.set_visible(False)
        for bar in range(bars + 1):
            ax.axvline(bar, color=BAR_LINE if bar % 4 else INK_MUTED, linewidth=0.8 if bar % 4 else 1.2, zorder=1)

    # Octave lines and C labels on the piano roll.
    for c in range((low // 12) * 12, high + 1, 12):
        if low <= c <= high:
            roll.axhline(c, color=GRID, linewidth=0.8, zorder=0)
    roll.set_yticks([c for c in range(0, 128, 12) if low <= c <= high])
    roll.set_yticklabels([f"C{c // 12 - 1}" for c in range(0, 128, 12) if low <= c <= high])
    for start, end, note, velocity, layer in pitched:
        roll.add_patch(Rectangle((to_bar(start), note - 0.4), to_bar(end) - to_bar(start), 0.8, facecolor=PITCHED[layer],
                                 edgecolor=SURFACE, linewidth=0.4, alpha=0.55 + 0.45 * velocity / 127, zorder=3))
    roll.set_ylim(low, high + 5)  # room for the labels above the notes
    roll.tick_params(colors=INK_SECONDARY, length=0)
    roll.set_ylabel("pitch", color=INK_SECONDARY)
    handles = [Rectangle((0, 0), 1, 1, facecolor=colour) for colour in PITCHED.values()]
    roll.legend(handles, [NAMES[layer] for layer in PITCHED], loc="upper left", ncol=3, frameon=False, fontsize=10,
                bbox_to_anchor=(0.0, 1.12))
    roll.set_title(args.title, color=INK, loc="left", fontsize=13, pad=30)

    # The drum and percussion hits, one row each.
    for row, layer in enumerate(("drums", "perc")):
        times = [to_bar(start) for start, _, _, _, l in notes if l == layer]
        hits.vlines(times, row + 0.15, row + 0.85, color=INK_SECONDARY if layer == "drums" else INK_MUTED, linewidth=0.7, zorder=3)
    hits.set_yticks([0.5, 1.5])
    hits.set_yticklabels(["Drums", "Percussion"])
    hits.set_ylim(0, 2)
    hits.tick_params(colors=INK_SECONDARY, length=0)
    hits.set_xlim(0, bars)
    hits.set_xticks([b + 0.5 for b in range(bars)])
    hits.set_xticklabels([str(b + 1) for b in range(bars)])
    hits.set_xlabel("bar", color=INK_SECONDARY)

    for mark in args.mark:
        bar, text = mark.split(":", 1)
        roll.annotate(text, (int(bar), high + 5), xytext=(int(bar) + 0.08, high + 4.2), color=INK, fontsize=9.5, va="top")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, dpi=130, bbox_inches="tight", facecolor=SURFACE)
    print(f"wrote {args.out}: {bars} bars, {len(pitched)} pitched notes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
