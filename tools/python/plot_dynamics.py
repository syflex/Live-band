#!/usr/bin/env python3
"""Draws how the band followed the singer's loudness in a recorded session: the voice level
(analysis.csv) above, and the band's velocity scale and intense bars (beats.csv) below.

  python tools/python/plot_dynamics.py <session folder> --out dynamics.png
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# Reference palette, light mode (validated for colour-vision deficiency in Phase 1).
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRID = "#e1e0d9"
VOICE = "#2a78d6"
BAND = "#eb6834"
INTENSE_FILL = "#fbe1d6"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("session", type=Path)
    parser.add_argument("--title", default="The band following the singer's loudness")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    with open(args.session / "analysis.csv", newline="") as f:
        frames = [(float(r["time_s"]), float(r["rms_db"])) for r in csv.DictReader(f) if r["voiced"] == "1"]
    with open(args.session / "beats.csv", newline="") as f:
        beats = [r for r in csv.DictReader(f) if r["count_in"] == "0"]
    if not frames or not beats:
        print("no voiced frames or band beats in this session", file=sys.stderr)
        return 1

    # The voice level, averaged over half a second (voiced frames only).
    times = np.array([t for t, _ in frames])
    levels = np.array([db for _, db in frames])
    edges = np.arange(0.0, times.max() + 0.5, 0.5)
    centres, means = [], []
    for a, b in zip(edges[:-1], edges[1:]):
        inside = (times >= a) & (times < b)
        if inside.any():
            centres.append((a + b) / 2)
            means.append(levels[inside].mean())

    beat_times = np.array([float(r["time_s"]) for r in beats])
    scales = np.array([float(r["velocity_scale"]) for r in beats])
    intense = np.array([r["intense"] == "1" for r in beats])

    fig, (top, bottom) = plt.subplots(2, 1, figsize=(12, 5.6), sharex=True, facecolor=SURFACE,
                                      gridspec_kw={"height_ratios": [1, 1], "hspace": 0.12})
    for ax in (top, bottom):
        ax.set_facecolor(SURFACE)
        for spine in ("top", "right"):
            ax.spines[spine].set_visible(False)
        for spine in ("left", "bottom"):
            ax.spines[spine].set_color(GRID)
        ax.grid(True, color=GRID, linewidth=0.8)
        ax.set_axisbelow(True)
        ax.tick_params(colors=INK_SECONDARY)

    # Shade the bars the band played intense, on both panels.
    period = np.median(np.diff(beat_times)) if len(beat_times) > 1 else 0.5
    start = None
    for t, on in zip(beat_times, intense):
        if on and start is None:
            start = t
        if not on and start is not None:
            for ax in (top, bottom):
                ax.axvspan(start, t, color=INTENSE_FILL, zorder=0, linewidth=0)
            start = None
    if start is not None:
        for ax in (top, bottom):
            ax.axvspan(start, beat_times[-1] + period, color=INTENSE_FILL, zorder=0, linewidth=0)

    top.plot(centres, means, color=VOICE, linewidth=2)
    top.set_ylabel("voice level (dBFS)", color=INK_SECONDARY)
    top.set_title(args.title, color=INK, loc="left", fontsize=13)

    bottom.step(beat_times, scales, where="post", color=BAND, linewidth=2)
    bottom.axhline(1.0, color=INK_MUTED, linewidth=1, linestyle=(0, (4, 3)))
    bottom.set_ylabel("band velocity scale", color=INK_SECONDARY)
    bottom.set_ylim(0.75, 1.2)
    bottom.set_xlabel("seconds into the session", color=INK_SECONDARY)
    if intense.any():
        first = beat_times[intense.argmax()]
        bottom.annotate("intense variation (shaded)", (first, 1.17), xytext=(first + 0.3, 1.17), color=INK, fontsize=9.5, va="center")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, dpi=130, bbox_inches="tight", facecolor=SURFACE)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
