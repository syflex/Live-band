#!/usr/bin/env python3
"""Scores lb-analyze's pitch against reference pitch tracks.

References come from either:
  --vocadito DIR   the vocadito dataset's hand-corrected f0 annotations, or
  --pyin           librosa's pYIN run on the same audio (for our own recordings).

Example:
  python tools/python/eval_pitch.py --analyzer out/build/linux-release/lb-analyze_artefacts/Release/lb-analyze \\
      --vocadito testdata/external/vocadito
"""

from __future__ import annotations

import argparse
import json
import shlex
import sys
import tempfile
from pathlib import Path

import numpy as np

from lbtools import load_frames, load_two_columns, resample_to, run_analyzer

# Phase 1 exit criteria (docs/IMPLEMENTATION_PLAN.md).
TARGET_WITHIN_50_CENTS = 0.95
TARGET_OCTAVE_ERRORS = 0.02


def pyin_reference(audio: Path) -> tuple[np.ndarray, np.ndarray]:
    import librosa

    y, sr = librosa.load(audio, sr=16000, mono=True)
    f0, voiced, _ = librosa.pyin(y, fmin=65.0, fmax=1050.0, sr=sr, frame_length=1024, hop_length=128)
    times = librosa.times_like(f0, sr=sr, hop_length=128)
    return times, np.where(voiced, np.nan_to_num(f0), 0.0)


def score(ref_times, ref_hz, est_times, est_hz) -> dict:
    est_at_ref = resample_to(ref_times, est_times, est_hz)
    ref_voiced = ref_hz > 0
    est_voiced = est_at_ref > 0
    both = ref_voiced & est_voiced

    cents = 1200.0 * np.log2(est_at_ref[both] / ref_hz[both])
    octaves = np.round(cents / 1200.0)
    within = np.abs(cents) <= 50.0
    octave_error = (octaves != 0) & (np.abs(cents - 1200.0 * octaves) <= 50.0)

    return {
        "ref_voiced": int(ref_voiced.sum()),
        "ref_unvoiced": int((~ref_voiced).sum()),
        "both_voiced": int(both.sum()),
        "est_voiced_on_ref_voiced": int(both.sum()),
        "false_alarms": int((est_voiced & ~ref_voiced).sum()),
        "within_50": int(within.sum()),
        "octave_errors": int(octave_error.sum()),
        "abs_cents": np.abs(cents[within]).tolist(),
    }


def summarise(counts: dict) -> dict:
    both = max(counts["both_voiced"], 1)
    return {
        "voicing_recall": counts["est_voiced_on_ref_voiced"] / max(counts["ref_voiced"], 1),
        "voicing_false_alarm": counts["false_alarms"] / max(counts["ref_unvoiced"], 1),
        "within_50_cents": counts["within_50"] / both,
        "octave_errors": counts["octave_errors"] / both,
        "raw_pitch_accuracy": counts["within_50"] / max(counts["ref_voiced"], 1),
        "median_abs_cents": float(np.median(counts["abs_cents"])) if counts["abs_cents"] else float("nan"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--analyzer", type=Path, required=True, help="path to the lb-analyze executable")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--vocadito", type=Path, help="vocadito dataset folder (contains Audio/ and Annotations/)")
    source.add_argument("--pyin", nargs="+", type=Path, metavar="AUDIO", help="audio files to compare against pYIN")
    parser.add_argument("--pitch", choices=["raw", "smoothed"], default="raw", help="which lb-analyze pitch to score")
    parser.add_argument("--analyzer-args", default="", help="extra options passed to lb-analyze")
    parser.add_argument("--out-dir", type=Path, help="keep lb-analyze's CSVs here")
    parser.add_argument("--json", type=Path, help="write the summary as JSON")
    parser.add_argument("--per-file", action="store_true", help="print a line per file")
    parser.add_argument("--check", action="store_true", help="exit with status 1 if the Phase 1 target is not met")
    args = parser.parse_args()

    if args.vocadito:
        audio = sorted((args.vocadito / "Audio").glob("*.wav"), key=lambda p: (len(p.stem), p.stem))
    else:
        audio = args.pyin
    if not audio:
        print("no audio files found", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        out_dir = args.out_dir or Path(tmp)
        run_analyzer(args.analyzer, audio, out_dir, shlex.split(args.analyzer_args))

        totals: dict = {}
        for path in audio:
            frames = load_frames(out_dir / f"{path.stem}.frames.csv")
            est_hz = frames.f0_hz if args.pitch == "raw" else frames.smoothed_hz
            if args.vocadito:
                ref_times, ref_hz = load_two_columns(args.vocadito / "Annotations" / "F0" / f"{path.stem}_f0.csv")
            else:
                ref_times, ref_hz = pyin_reference(path)

            counts = score(ref_times, ref_hz, frames.time, est_hz)
            for key, value in counts.items():
                totals[key] = totals.get(key, 0 if not isinstance(value, list) else []) + value

            if args.per_file:
                s = summarise(counts)
                print(f"{path.stem:>14}: within 50 c {s['within_50_cents']:6.1%}  octave {s['octave_errors']:5.1%}  "
                      f"recall {s['voicing_recall']:6.1%}  false alarm {s['voicing_false_alarm']:6.1%}  "
                      f"median {s['median_abs_cents']:4.1f} c")

    summary = summarise(totals)
    summary["files"] = len(audio)
    summary["pitch"] = args.pitch
    summary["analyzer_args"] = args.analyzer_args
    passed = summary["within_50_cents"] >= TARGET_WITHIN_50_CENTS and summary["octave_errors"] < TARGET_OCTAVE_ERRORS
    summary["meets_phase1_target"] = passed

    print(f"\n{len(audio)} files, {totals['both_voiced']} frames voiced in both ({args.pitch} pitch)")
    print(f"  within 50 cents:      {summary['within_50_cents']:.1%}  (target >= {TARGET_WITHIN_50_CENTS:.0%})")
    print(f"  octave errors:        {summary['octave_errors']:.1%}  (target < {TARGET_OCTAVE_ERRORS:.0%})")
    print(f"  median error:         {summary['median_abs_cents']:.1f} cents (frames within 50 cents)")
    print(f"  voicing recall:       {summary['voicing_recall']:.1%}")
    print(f"  voicing false alarm:  {summary['voicing_false_alarm']:.1%}")
    print(f"  raw pitch accuracy:   {summary['raw_pitch_accuracy']:.1%}  (MIREX-style: unvoiced counts as wrong)")
    print(f"  Phase 1 pitch target: {'MET' if passed else 'NOT MET'}")

    if args.json:
        args.json.write_text(json.dumps(summary, indent=2))
    return 1 if args.check and not passed else 0


if __name__ == "__main__":
    sys.exit(main())
