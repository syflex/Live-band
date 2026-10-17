#!/usr/bin/env python3
"""Draws the Phase 1 evaluation charts from vocadito.

  clip    LiveBand's pitch and onsets on one clip, against the hand annotations
  errors  the share of frames more than 50 cents off, for every clip

Example:
  python tools/python/plot_results.py --analyzer <lb-analyze> --vocadito testdata/external/vocadito \\
      clip --clip vocadito_17 --seconds 12 --out docs/images/phase1/pitch-vs-annotation.png
  python tools/python/plot_results.py --analyzer <lb-analyze> --vocadito testdata/external/vocadito \\
      errors --out docs/images/phase1/pitch-errors-per-clip.png

"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import mir_eval  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.patches import FancyBboxPatch, Rectangle  # noqa: E402

from eval_pitch import score  # noqa: E402
from lbtools import load_frames, load_onsets, load_two_columns, run_analyzer  # noqa: E402

# Reference palette, light mode (validated: blue and orange pass every check).
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
LIVEBAND = "#2a78d6"  # categorical slot 1
REFERENCE = "#eb6834"  # categorical slot 2

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def style_axes(ax) -> None:
    ax.set_facecolor(SURFACE)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(AXIS)
    ax.tick_params(colors=INK_MUTED, labelcolor=INK_SECONDARY, length=0, labelsize=10)


def hz_to_midi(hz: np.ndarray) -> np.ndarray:
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.where(hz > 0, 69.0 + 12.0 * np.log2(hz / 440.0), np.nan)


def plot_clip(args, out_dir: Path) -> None:
    clip = args.clip
    frames = load_frames(out_dir / f"{clip}.frames.csv")
    onsets = load_onsets(out_dir / f"{clip}.onsets.csv")
    ref_t, ref_hz = load_two_columns(args.vocadito / "Annotations" / "F0" / f"{clip}_f0.csv")
    notes = np.loadtxt(args.vocadito / "Annotations" / "Notes" / f"{clip}_notesA1.csv", delimiter=",", ndmin=2)

    counts = score(ref_t, ref_hz, frames.time, frames.f0_hz)
    within = counts["within_50"] / max(counts["both_voiced"], 1)
    f_measure, _, _ = mir_eval.onset.f_measure(notes[:, 0], onsets, window=0.05)

    end = args.seconds
    ref_midi = hz_to_midi(ref_hz)
    our_midi = np.where(frames.midi > 0, frames.midi, np.nan)
    ref_keep = ref_t <= end
    our_keep = frames.time <= end

    fig = plt.figure(figsize=(11, 5.6), dpi=150, facecolor=SURFACE)
    grid = fig.add_gridspec(2, 1, height_ratios=[4.2, 1], hspace=0.08, left=0.15, right=0.97, top=0.80, bottom=0.11)
    ax = fig.add_subplot(grid[0])
    strip = fig.add_subplot(grid[1], sharex=ax)

    # Pitch: the reference as a wide translucent band, LiveBand as a 2px line on top.
    ax.plot(ref_t[ref_keep], ref_midi[ref_keep], color=REFERENCE, linewidth=7, alpha=0.35, solid_capstyle="round", label="Hand-corrected pitch")
    ax.plot(frames.time[our_keep], our_midi[our_keep], color=LIVEBAND, linewidth=2, solid_capstyle="round", solid_joinstyle="round",
            label="LiveBand (live analysis)")

    shown = np.concatenate([ref_midi[ref_keep], our_midi[our_keep]])
    low, high = int(np.floor(np.nanmin(shown))) - 2, int(np.ceil(np.nanmax(shown))) + 2
    # C, D, E, G and A: F and B sit a semitone from a neighbour and their labels would collide.
    naturals = [m for m in range(low, high + 1) if NOTE_NAMES[m % 12] in ("C", "D", "E", "G", "A")]
    ax.set_yticks(naturals, [f"{NOTE_NAMES[m % 12]}{m // 12 - 1}" for m in naturals])
    ax.set_ylim(low, high)
    ax.grid(axis="y", color=GRID, linewidth=1)
    ax.set_axisbelow(True)
    style_axes(ax)
    ax.tick_params(labelbottom=False)
    ax.spines["bottom"].set_visible(False)

    # Onsets: annotated note starts against detected onsets, one labelled row each.
    note_starts = notes[notes[:, 0] <= end, 0]
    detected = onsets[onsets <= end]
    strip.vlines(note_starts, 1.62, 2.38, color=REFERENCE, linewidth=2)
    strip.vlines(detected, 0.62, 1.38, color=LIVEBAND, linewidth=2)
    strip.set_yticks([2, 1], ["Annotated note starts", "LiveBand onsets"])
    strip.set_ylim(0.4, 2.6)
    strip.set_xlim(0, end)
    strip.set_xlabel("Time (seconds)", color=INK_SECONDARY, fontsize=10)
    style_axes(strip)

    fig.text(0.15, 0.93, f"{clip}: LiveBand follows the hand-corrected pitch", color=INK, fontsize=15, fontweight="bold")
    fig.text(0.15, 0.885,
             f"Whole clip: {within:.1%} of voiced frames within 50 cents; onset F-measure {f_measure:.2f}. "
             f"First {end:g} seconds shown.", color=INK_SECONDARY, fontsize=10.5)
    legend = ax.legend(loc="lower left", bbox_to_anchor=(0.0, 1.0), ncol=2, frameon=False, fontsize=10, borderaxespad=0.2)
    for text in legend.get_texts():
        text.set_color(INK_SECONDARY)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(f"wrote {args.out}")


def rounded_bar(ax, x: float, width: float, height: float, radius_px: float = 4.0) -> None:
    """A bar with a rounded data end and a square baseline."""
    if height <= 0:
        return
    bbox = ax.get_window_extent()
    x_per_px = (ax.get_xlim()[1] - ax.get_xlim()[0]) / bbox.width
    y_per_px = (ax.get_ylim()[1] - ax.get_ylim()[0]) / bbox.height
    rx = min(radius_px * x_per_px, width / 2)
    ry = min(radius_px * y_per_px, height)
    ax.add_patch(FancyBboxPatch((x, 0), width, height, boxstyle=f"round,pad=0,rounding_size={rx}",
                                mutation_aspect=ry / rx, linewidth=0, facecolor=LIVEBAND))
    ax.add_patch(Rectangle((x, 0), width, min(height, ry), linewidth=0, facecolor=LIVEBAND))  # square the baseline corners


def plot_errors(args, out_dir: Path) -> None:
    audio = sorted((args.vocadito / "Audio").glob("*.wav"), key=lambda p: (len(p.stem), p.stem))
    rows = []
    totals_off = totals_both = 0
    for path in audio:
        frames = load_frames(out_dir / f"{path.stem}.frames.csv")
        ref_t, ref_hz = load_two_columns(args.vocadito / "Annotations" / "F0" / f"{path.stem}_f0.csv")
        counts = score(ref_t, ref_hz, frames.time, frames.smoothed_hz)
        off = counts["both_voiced"] - counts["within_50"]
        rows.append((path.stem.split("_")[1], 100.0 * off / max(counts["both_voiced"], 1)))
        totals_off += off
        totals_both += counts["both_voiced"]
    rows.sort(key=lambda r: r[1])
    overall = 100.0 * totals_off / max(totals_both, 1)

    fig, ax = plt.subplots(figsize=(11, 4.8), dpi=150, facecolor=SURFACE)
    fig.subplots_adjust(left=0.06, right=0.97, top=0.78, bottom=0.14)
    top = max(20.0, max(r[1] for r in rows) * 1.08)
    ax.set_xlim(-0.6, len(rows) - 0.4)
    ax.set_ylim(0, top)
    style_axes(ax)
    fig.canvas.draw()  # so the axes size is known for the rounded corners

    width = 0.62
    for i, (_, value) in enumerate(rows):
        rounded_bar(ax, i - width / 2, width, value)

    ax.axhline(5.0, color=INK_SECONDARY, linewidth=1)
    ax.text(-0.4, 5.25, "Target for the whole set: at most 5% off", color=INK_SECONDARY, fontsize=9.5, ha="left", va="bottom")
    ax.axhline(overall, color=LIVEBAND, linewidth=1, alpha=0.6)
    ax.text(-0.4, overall - 0.25, f"All 40 clips together: {overall:.1f}% off", color=INK_SECONDARY, fontsize=9.5, ha="left", va="top")

    ax.set_xticks(range(len(rows)), [r[0] for r in rows], fontsize=8)
    ax.set_yticks(range(0, int(top) + 1, 5), [f"{v}%" for v in range(0, int(top) + 1, 5)])
    ax.grid(axis="y", color=GRID, linewidth=1)
    ax.set_axisbelow(True)
    ax.set_xlabel("vocadito clip number, sorted", color=INK_SECONDARY, fontsize=10)

    fig.text(0.06, 0.92, "Pitch frames more than 50 cents off, per clip", color=INK, fontsize=15, fontweight="bold")
    fig.text(0.06, 0.865, "Smoothed pitch as shown in the app, against the hand-corrected pitch track. Lower is better.",
             color=INK_SECONDARY, fontsize=10.5)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(f"wrote {args.out}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--analyzer", type=Path, required=True)
    parser.add_argument("--vocadito", type=Path, required=True)
    sub = parser.add_subparsers(dest="chart", required=True)
    clip = sub.add_parser("clip")
    clip.add_argument("--clip", default="vocadito_17")
    clip.add_argument("--seconds", type=float, default=12.0)
    clip.add_argument("--out", type=Path, required=True)
    errors = sub.add_parser("errors")
    errors.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        if args.chart == "clip":
            run_analyzer(args.analyzer, [args.vocadito / "Audio" / f"{args.clip}.wav"], out_dir)
            plot_clip(args, out_dir)
        else:
            run_analyzer(args.analyzer, sorted((args.vocadito / "Audio").glob("*.wav")), out_dir)
            plot_errors(args, out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
