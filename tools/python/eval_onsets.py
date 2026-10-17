#!/usr/bin/env python3
"""Scores lb-analyze's onsets against annotated note starts.

References come from either:
  --vocadito DIR    the vocadito dataset's note annotations, or
  --labels A B ...  pairs of audio file and label file (one onset time in seconds per line;
                    Audacity label exports work, as only the first column is read).

Example:
  python tools/python/eval_onsets.py --analyzer out/build/linux-release/lb-analyze_artefacts/Release/lb-analyze \\
      --vocadito testdata/external/vocadito
"""

from __future__ import annotations

import argparse
import json
import shlex
import sys
import tempfile
from pathlib import Path

import mir_eval
import numpy as np

from lbtools import load_onsets, run_analyzer

TARGET_F_MEASURE = 0.65  # Phase 1 exit criterion


def read_label_times(path: Path) -> np.ndarray:
    times = []
    for line in path.read_text().splitlines():
        fields = line.replace(",", "\t").split()
        if fields:
            try:
                times.append(float(fields[0]))
            except ValueError:
                pass  # header line
    return np.array(sorted(times))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--analyzer", type=Path, required=True, help="path to the lb-analyze executable")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--vocadito", type=Path, help="vocadito dataset folder")
    source.add_argument("--labels", nargs="+", type=Path, metavar="FILE", help="audio and label files, in pairs")
    parser.add_argument("--annotator", choices=["A1", "A2"], default="A1", help="which vocadito annotator to use")
    parser.add_argument("--window", type=float, default=0.05, help="match tolerance in seconds (default 0.05)")
    parser.add_argument("--analyzer-args", default="", help="extra options passed to lb-analyze")
    parser.add_argument("--json", type=Path, help="write the summary as JSON")
    parser.add_argument("--per-file", action="store_true", help="print a line per file")
    parser.add_argument("--check", action="store_true", help="exit with status 1 if the Phase 1 target is not met")
    args = parser.parse_args()

    if args.vocadito:
        audio = sorted((args.vocadito / "Audio").glob("*.wav"), key=lambda p: (len(p.stem), p.stem))
        references = [args.vocadito / "Annotations" / "Notes" / f"{p.stem}_notes{args.annotator}.csv" for p in audio]
        reference_times = [np.loadtxt(r, delimiter=",", ndmin=2)[:, 0] for r in references]
    else:
        if len(args.labels) % 2:
            parser.error("--labels needs audio and label files in pairs")
        audio = args.labels[0::2]
        reference_times = [read_label_times(p) for p in args.labels[1::2]]

    hits = refs = ests = 0
    errors: list[float] = []
    with tempfile.TemporaryDirectory() as tmp:
        run_analyzer(args.analyzer, audio, Path(tmp), shlex.split(args.analyzer_args))
        for path, ref in zip(audio, reference_times):
            est = load_onsets(Path(tmp) / f"{path.stem}.onsets.csv")
            matches = mir_eval.util.match_events(ref, est, args.window)
            hits += len(matches)
            refs += len(ref)
            ests += len(est)
            errors.extend(est[j] - ref[i] for i, j in matches)
            if args.per_file:
                f, p, r = mir_eval.onset.f_measure(ref, est, window=args.window)
                print(f"{path.stem:>14}: F {f:5.2f}  precision {p:5.2f}  recall {r:5.2f}  ({len(ref)} notes, {len(est)} onsets)")

    precision = hits / max(ests, 1)
    recall = hits / max(refs, 1)
    f_measure = 2 * precision * recall / max(precision + recall, 1e-9)
    summary = {
        "files": len(audio),
        "reference_onsets": refs,
        "detected_onsets": ests,
        "f_measure": f_measure,
        "precision": precision,
        "recall": recall,
        "mean_error_s": float(np.mean(errors)) if errors else float("nan"),
        "median_abs_error_s": float(np.median(np.abs(errors))) if errors else float("nan"),
        "window_s": args.window,
        "analyzer_args": args.analyzer_args,
        "meets_phase1_target": f_measure >= TARGET_F_MEASURE,
    }

    print(f"\n{len(audio)} files, {refs} annotated onsets, {ests} detected, window {args.window * 1000:.0f} ms")
    print(f"  F-measure: {f_measure:.3f}  (target >= {TARGET_F_MEASURE})")
    print(f"  precision: {precision:.3f}   recall: {recall:.3f}")
    print(f"  timing:    mean {summary['mean_error_s'] * 1000:+.1f} ms, median absolute {summary['median_abs_error_s'] * 1000:.1f} ms")
    print(f"  Phase 1 onset target: {'MET' if summary['meets_phase1_target'] else 'NOT MET'}")

    if args.json:
        args.json.write_text(json.dumps(summary, indent=2))
    return 1 if args.check and not summary["meets_phase1_target"] else 0


if __name__ == "__main__":
    sys.exit(main())
