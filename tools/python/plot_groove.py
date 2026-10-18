#!/usr/bin/env python3
"""Draws a style's patterns as drum grids, one row per instrument and one column per
sixteenth note, for musicians to review.

  python tools/python/plot_groove.py --out docs/images/phase2/groove-afrobeats.png
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

import make_grooves as groove  # noqa: E402

# Reference palette, light mode (validated for colour-vision deficiency in Phase 1).
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRID = "#e1e0d9"
BAR_LINE = "#c3c2b7"
LAYER_COLOURS = {"drums": "#2a78d6", "perc": "#eb6834"}

NAMES = {
    groove.KICK: "Kick", groove.STICK: "Side stick", groove.SNARE: "Snare", groove.CLAP: "Clap",
    groove.HAT: "Closed hi-hat", groove.OPEN_HAT: "Open hi-hat", groove.HIGH_TOM: "High tom",
    groove.LOW_TOM: "Low tom", groove.FLOOR_HIGH: "Floor tom", groove.FLOOR_LOW: "Low floor tom",
    groove.SHAKER_DOWN: "Shaker (down)", groove.SHAKER_UP: "Shaker (up)", groove.TAMBOURINE: "Tambourine",
    groove.CONGA_MUTE: "Conga, muted", groove.CONGA_OPEN: "Conga, open", groove.CONGA_LOW: "Low conga",
    groove.AGOGO_HIGH: "Agogo, high", groove.AGOGO_LOW: "Agogo, low",
}
ORDER = list(NAMES)

PANELS = [
    ("Verse", "verse.mid"),
    ("Chorus", "chorus.mid"),
    ("Verse fill", "fill-verse.mid"),
    ("Chorus fill", "fill-chorus.mid"),
]


def rows_of(pattern: dict) -> list[tuple[int, str, str]]:
    rows = [(note, layer, grid) for layer, notes in pattern.items() for note, grid in notes.items()]
    return sorted(rows, key=lambda r: ORDER.index(r[0]))


def draw_panel(ax, title: str, pattern: dict) -> None:
    rows = rows_of(pattern)
    steps = groove.bar_count(pattern) * groove.STEPS_PER_BEAT * groove.BEATS_PER_BAR
    ax.set_facecolor(SURFACE)
    for spine in ax.spines.values():
        spine.set_visible(False)

    for step in range(steps + 1):
        bar = step % (groove.STEPS_PER_BEAT * groove.BEATS_PER_BAR) == 0
        beat = step % groove.STEPS_PER_BEAT == 0
        if bar or beat:
            ax.axvline(step - 0.5, color=BAR_LINE if bar else GRID, linewidth=1.6 if bar else 1.0, zorder=0)
    for y in range(len(rows)):
        ax.axhline(y, color=GRID, linewidth=0.6, zorder=0)

    for y, (note, layer, grid) in enumerate(rows):
        hits = groove.parse_grid(grid)
        xs = [step for step, _ in hits]
        sizes = [18 + 150 * (velocity / 127.0) ** 2 for _, velocity in hits]
        ax.scatter(xs, [y] * len(xs), s=sizes, color=LAYER_COLOURS[layer], edgecolors=SURFACE, linewidths=1.5, zorder=3)

    ax.set_yticks(range(len(rows)), [NAMES[r[0]] for r in rows])
    ax.set_ylim(len(rows) - 0.4, -0.6)
    ax.set_xlim(-0.6, steps - 0.4)
    beats = range(0, steps, groove.STEPS_PER_BEAT)
    ax.set_xticks(list(beats), [str(b // groove.STEPS_PER_BEAT % groove.BEATS_PER_BAR + 1) for b in beats])
    ax.tick_params(colors=INK_MUTED, labelcolor=INK_SECONDARY, length=0, labelsize=9)
    ax.set_title(title, loc="left", color=INK, fontsize=12, fontweight="bold", pad=6)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    rows = [len(rows_of(groove.PATTERNS[name])) for _, name in PANELS]
    ratios = [rows[0], rows[1], max(rows[2:])]
    inches = 0.27 * sum(ratios) + 3.2  # rows plus titles, tick labels and the heading
    fig = plt.figure(figsize=(11, inches), dpi=150, facecolor=SURFACE)
    grid = fig.add_gridspec(3, 2, height_ratios=ratios, left=0.14, right=0.98, top=1 - 1.25 / inches, bottom=0.35 / inches,
                            hspace=0.9 / (0.27 * sum(ratios) / 3), wspace=0.28)
    draw_panel(fig.add_subplot(grid[0, :]), "Verse (two bars)", groove.PATTERNS["verse.mid"])
    draw_panel(fig.add_subplot(grid[1, :]), "Chorus (two bars)", groove.PATTERNS["chorus.mid"])
    draw_panel(fig.add_subplot(grid[2, 0]), "Verse fill (one bar)", groove.PATTERNS["fill-verse.mid"])
    draw_panel(fig.add_subplot(grid[2, 1]), "Chorus fill (one bar)", groove.PATTERNS["fill-chorus.mid"])

    style = groove.STYLE
    fig.text(0.14, 1 - 0.45 / inches, f"{style['name']} groove: every hit, by sixteenth note", color=INK, fontsize=15, fontweight="bold")
    fig.text(0.14, 1 - 0.75 / inches, f"{style['tempo']['default']} BPM (range {style['tempo']['min']} to {style['tempo']['max']}), "
             f"swing {style['swing']:.2f}. Numbers are beats; bigger dots are louder.", color=INK_SECONDARY, fontsize=10)
    handles = [plt.Line2D([], [], marker="o", linestyle="", markersize=8, color=colour, label=f"{layer} layer")
               for layer, colour in LAYER_COLOURS.items()]
    legend = fig.legend(handles=handles, loc="upper right", bbox_to_anchor=(0.98, 1 - 0.3 / inches), ncol=2, frameon=False, fontsize=10)
    for text in legend.get_texts():
        text.set_color(INK_SECONDARY)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
