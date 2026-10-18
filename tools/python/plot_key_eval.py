#!/usr/bin/env python3
"""Draws how the live key estimate improves with seconds of singing, one panel per test
set: the share of clips whose best key is exact, and exact, relative or a fifth away,
against seconds of voice, with the point where the estimator locks.

  python tools/python/plot_key_eval.py --analyzer <lb-analyze> \\
      --labels testdata/external/keyset-holdout/labels.csv \\
      --csd testdata/external/csd/CSD --vocadito testdata/external/vocadito \\
      --out docs/images/phase3/key-accuracy.png
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

import eval_key  # noqa: E402

# Reference palette, light mode (validated for colour-vision deficiency in Phase 1).
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRID = "#e1e0d9"
EXACT = "#2a78d6"
COMPATIBLE = "#eb6834"

SECONDS = np.arange(0.0, 12.01, 0.1)


def run(analyzer: Path, clips: dict[str, tuple[Path, str]], profile: str) -> dict[str, list[dict]]:
    with tempfile.TemporaryDirectory() as tmp:
        command = [str(analyzer), "--quiet", "--out-dir", tmp, "--key-profile", profile]
        subprocess.run(command + [str(path) for path, _ in clips.values()], check=True)
        return {clip: eval_key.read_key_rows(Path(tmp) / f"{clip}.key.csv") for clip in clips}


def curves(rows: dict[str, list[dict]], clips: dict[str, tuple[Path, str]]):
    exact, compatible = [], []
    for seconds in SECONDS:
        relations = [eval_key.relation(eval_key.estimate_at(rows[c], max(seconds, 1e-6)), clips[c][1]) for c in clips]
        exact.append(np.mean([r == "exact" for r in relations]))
        compatible.append(np.mean([r in ("exact", "relative", "fifth") for r in relations]))
    locks = [(eval_key.auto_lock(rows[c]), clips[c][1]) for c in clips]
    locked = [(k, t, ref) for (k, t), ref in locks if k]
    lock = {
        "reached": len(locked) / len(clips),
        "exact": float(np.mean([k == ref for k, _, ref in locked])),
        "compatible": float(np.mean([eval_key.relation(k, ref) in ("exact", "relative", "fifth") for k, _, ref in locked])),
        "median_s": float(np.median([t for _, t, _ in locked])),
    }
    return np.array(exact), np.array(compatible), lock


def draw(ax, title: str, exact, compatible, lock, clips: int) -> None:
    ax.set_facecolor(SURFACE)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    for spine in ("left", "bottom"):
        ax.spines[spine].set_color(GRID)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)

    # The plan's exit criterion: 80% exact after 3 s of voice.
    ax.plot([3.0], [0.8], marker="+", markersize=14, markeredgewidth=2, color=INK_SECONDARY, zorder=4)
    ax.annotate("plan's target:\n80% exact at 3 s", (3.0, 0.8), xytext=(0.25, 0.95), color=INK_SECONDARY, fontsize=9, va="center",
                arrowprops={"arrowstyle": "-", "color": INK_MUTED, "linewidth": 0.8})

    ax.plot(SECONDS, compatible, color=COMPATIBLE, linewidth=2, label="exact, relative or a fifth away")
    ax.plot(SECONDS, exact, color=EXACT, linewidth=2, label="exact key")
    ax.annotate(f"{exact[30]:.0%} at 3 s", (3.0, exact[30]), xytext=(3.3, exact[30] - 0.16), color=INK_SECONDARY, fontsize=9,
                arrowprops={"arrowstyle": "-", "color": INK_MUTED, "linewidth": 0.8})

    # Where the estimator locks: the median time, and how often that key is right.
    ax.plot([lock["median_s"]], [lock["exact"]], marker="o", markersize=9, color=EXACT, markeredgecolor=SURFACE, markeredgewidth=2, zorder=5)
    ax.annotate(f"locks on {lock['reached']:.0%} of clips,\nmedian {lock['median_s']:.1f} s of voice:\n"
                f"{lock['exact']:.0%} exact, {lock['compatible']:.0%} compatible",
                (lock["median_s"], lock["exact"]), xytext=(lock["median_s"] - 1.0, 0.05), color=INK, fontsize=9,
                arrowprops={"arrowstyle": "-", "color": INK_MUTED, "linewidth": 0.8})

    ax.set_xlim(0, 12)
    ax.set_ylim(0, 1.02)
    ax.yaxis.set_major_formatter(matplotlib.ticker.PercentFormatter(1.0))
    ax.set_xlabel("seconds of voiced singing", color=INK_SECONDARY)
    ax.tick_params(colors=INK_SECONDARY)
    ax.set_title(f"{title} ({clips} clips)", color=INK, loc="left", fontsize=12)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--analyzer", type=Path, required=True)
    parser.add_argument("--labels", type=Path, help="a synthetic set's labels.csv")
    parser.add_argument("--csd", type=Path, help="the Children's Song Dataset (its test half is drawn)")
    parser.add_argument("--vocadito", type=Path)
    parser.add_argument("--profile", default="temperley")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    panels = []
    if args.labels:
        with open(args.labels, newline="") as f:
            synthetic = {Path(r["file"]).stem: (args.labels.parent / r["file"], eval_key.normalize(r["key"])) for r in csv.DictReader(f)}
        panels.append(("Synthetic singing: hymn and folk tunes", synthetic))
    if args.csd:
        panels.append(("Real singing: children's songs (CSD, test half)", eval_key.csd_references(args.csd, "test")))
    if args.vocadito:
        references = eval_key.vocadito_references(args.vocadito)
        panels.append(("Real singing: vocadito", {clip: (args.vocadito / "Audio" / f"{clip}.wav", key) for clip, key in references.items()}))
    if not panels:
        parser.error("give at least one test set")

    fig, axes = plt.subplots(1, len(panels), figsize=(5.4 * len(panels), 4.8), sharey=True, facecolor=SURFACE, squeeze=False)
    axes = axes[0]
    for ax, (title, clips) in zip(axes, panels):
        exact, compatible, lock = curves(run(args.analyzer, clips, args.profile), clips)
        draw(ax, title, exact, compatible, lock, len(clips))
    axes[0].set_ylabel("clips whose best key is ...", color=INK_SECONDARY)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles[::-1], labels[::-1], loc="upper center", ncol=2, frameon=False, fontsize=10, bbox_to_anchor=(0.5, 1.0))
    fig.suptitle(f"Live key estimate against seconds of singing ({args.profile} profile)", color=INK, fontsize=13, y=1.07)
    fig.tight_layout()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, dpi=130, bbox_inches="tight", facecolor=SURFACE)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
