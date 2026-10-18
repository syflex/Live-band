#!/usr/bin/env python3
"""Scores LiveBand's live key estimate against reference keys.

For each key profile, runs lb-analyze over the clips and reads the key estimate it makes
while listening (<name>.key.csv), then reports, after 1, 2, 3 and 5 seconds of voice and at
the end of the clip, the share of exact keys and the MIREX weighted score (mir_eval). The
Phase 3 target is the exact key on at least 80% of clips after 3 seconds of voice.

It also reports the confident lock: the key at the first moment, after at least 2 seconds of
voice, when the best key's correlation leads the second's by a margin. For each margin:
the share of clips that reach it, how often that key is right, and the median seconds of
voice it takes. The band locks its key this way.

Reference keys come from either:

  --labels <labels.csv>   a CSV with file and key columns (e.g. from make_key_testset.py),
                          audio next to it unless --audio-dir says otherwise
  --vocadito <folder>     vocadito, which has no key labels: the reference is the key that
                          at least three of the four profiles (music21's implementations)
                          give for the whole hand-annotated melody; other clips are skipped
  --csd <folder>          the Children's Song Dataset (fetch_csd.py): 100 children's songs,
                          each sung in two keys, with every note annotated; the reference is
                          found the same way. --split dev or test takes half of the songs
                          (odd or even song numbers; both keys of a song stay together)

  python tools/python/eval_key.py --analyzer <lb-analyze> --labels testdata/external/keyset/labels.csv

With --check it fails if, for any profile, the estimator's own lock falls below a floor:
locked on at least 65% of the clips, and exact, relative or a fifth away on at least 75% of
those. These floors guard against regressions; they sit a little below what the shipped
settings reach on vocadito (79% and 84%).
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

import mir_eval
import numpy as np

PROFILES = ["krumhansl-kessler", "aarden-essen", "temperley", "bellman-budge"]
NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
CHECKPOINTS = [1.0, 2.0, 3.0, 5.0]
MARGINS = [0.1, 0.15, 0.2, 0.25, 0.3, 0.4]
CHECK_LOCKED = 0.65
CHECK_COMPATIBLE = 0.75


def key_name(index: int) -> str | None:
    if index < 0:
        return None
    return f"{NOTE_NAMES[index % 12]} {'minor' if index >= 12 else 'major'}"


def normalize(name: str) -> str:
    tonic, mode = name.split()
    flats = {"Db": "C#", "D#": "Eb", "Gb": "F#", "G#": "Ab", "A#": "Bb"}
    return f"{flats.get(tonic, tonic)} {mode.lower()}"


def read_key_rows(path: Path) -> list[dict]:
    with open(path, newline="") as f:
        return [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]


def estimate_at(rows: list[dict], voiced: float | None) -> str | None:
    """The best key once `voiced` seconds of voice have been heard (None: at the end)."""
    if not rows:
        return None
    if voiced is not None:
        for row in rows:
            if row["voiced_s"] >= voiced:
                return key_name(int(row["best"]))
    return key_name(int(rows[-1]["best"]))


def relation(estimate: str | None, reference: str) -> str:
    """How an estimate relates to the reference: exact, fifth (up or down), relative, parallel, other."""
    if estimate is None:
        return "none"
    if estimate == reference:
        return "exact"
    (et, em), (rt, rm) = estimate.split(), reference.split()
    interval = (NOTE_NAMES.index(et) - NOTE_NAMES.index(rt)) % 12
    if em == rm and interval in (5, 7):
        return "fifth"
    if em != rm and interval == (9 if rm == "major" else 3):
        return "relative"
    if em != rm and interval == 0:
        return "parallel"
    return "other"


RELATIONS = ["exact", "relative", "fifth", "parallel", "other", "none"]


LOCKED = 3  # KeyEstimator::State::locked in key.csv


def auto_lock(rows: list[dict]):
    """The key the estimator locked by itself, and the seconds of voice it took."""
    for row in rows:
        if row["state"] == LOCKED and row["key"] >= 0:
            return key_name(int(row["key"])), row["voiced_s"]
    return None, None


def confident(rows: list[dict], margin: float, after: float = 2.0):
    """The key at the first moment with at least `after` seconds of voice and a lead of `margin`."""
    for row in rows:
        if row["voiced_s"] >= after and row["margin"] >= margin and row["best"] >= 0:
            return key_name(int(row["best"])), row["voiced_s"]
    return None, None


def vocadito_references(folder: Path) -> dict[str, str]:
    from music21 import analysis, note, stream

    analyzers = [analysis.discrete.KrumhanslKessler(), analysis.discrete.AardenEssen(),
                 analysis.discrete.TemperleyKostkaPayne(), analysis.discrete.BellmanBudge()]
    references = {}
    for path in sorted((folder / "Annotations" / "Notes").glob("*_notesA1.csv")):
        clip = path.name.replace("_notesA1.csv", "")
        rows = np.loadtxt(path, delimiter=",", ndmin=2)
        midi = 69 + 12 * np.log2(rows[:, 1] / 440.0)
        # Bin relative to the singer's own tuning (circular mean of the deviation).
        angle = 2 * np.pi * (midi - np.round(midi))
        offset = np.angle(np.sum(rows[:, 2] * np.exp(1j * angle))) / (2 * np.pi)
        s = stream.Stream()
        for m, duration in zip(np.round(midi - offset), rows[:, 2]):
            if duration > 0:
                s.append(note.Note(int(m), quarterLength=max(0.01, round(float(duration), 2))))
        votes = Counter()
        for a in analyzers:
            k = a.getSolution(s)
            votes[normalize(f"{k.tonic.name.replace('-', 'b')} {k.mode}")] += 1
        best, count = votes.most_common(1)[0]
        if count >= 3:
            references[clip] = best
    return references


def consensus_key(pitches, durations) -> tuple[str | None, int]:
    """The key that most of music21's four profiles give for a melody, and how many agree."""
    from music21 import analysis, note, stream

    analyzers = [analysis.discrete.KrumhanslKessler(), analysis.discrete.AardenEssen(),
                 analysis.discrete.TemperleyKostkaPayne(), analysis.discrete.BellmanBudge()]
    s = stream.Stream()
    for m, duration in zip(pitches, durations):
        if duration > 0:
            s.append(note.Note(int(m), quarterLength=max(0.01, round(float(duration), 2))))
    votes = Counter()
    for a in analyzers:
        k = a.getSolution(s)
        votes[normalize(f"{k.tonic.name.replace('-', 'b')} {k.mode}")] += 1
    best, count = votes.most_common(1)[0]
    return best, count


def csd_references(folder: Path, split: str) -> dict[str, tuple[Path, str]]:
    clips = {}
    for csv_path in sorted(folder.glob("*/csv/*.csv")):
        clip = csv_path.stem  # en001a: song 1, first key
        song = int(clip[2:5])
        if split == "dev" and song % 2 == 0 or split == "test" and song % 2 == 1:
            continue
        with open(csv_path, newline="") as f:
            rows = list(csv.DictReader(f))
        key, votes = consensus_key([int(r["pitch"]) for r in rows], [float(r["end"]) - float(r["start"]) for r in rows])
        if votes >= 3:
            clips[clip] = (csv_path.parent.parent / "wav" / f"{clip}.wav", key)
    return clips


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--analyzer", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--labels", type=Path)
    source.add_argument("--vocadito", type=Path)
    source.add_argument("--csd", type=Path)
    parser.add_argument("--split", choices=["dev", "test", "all"], default="all", help="with --csd: which songs")
    parser.add_argument("--audio-dir", type=Path)
    parser.add_argument("--profiles", default=",".join(PROFILES))
    parser.add_argument("--extra", default="", help="more lb-analyze options, e.g. \"--phrase-bonus 2\"")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--per-file", action="store_true")
    parser.add_argument("--check", action="store_true", help="fail below the lock floors")
    args = parser.parse_args()

    if args.labels:
        audio_dir = args.audio_dir or args.labels.parent
        with open(args.labels, newline="") as f:
            labelled = {row["file"]: normalize(row["key"]) for row in csv.DictReader(f)}
        clips = {Path(name).stem: (audio_dir / name, key) for name, key in labelled.items()}
        groups = {Path(name).stem: name.split("-")[0] for name in labelled}
    elif args.csd:
        clips = csd_references(args.csd, args.split)
        total = len([p for p in args.csd.glob("*/csv/*.csv")
                     if args.split == "all" or (int(p.stem[2:5]) % 2 == 1) == (args.split == "dev")])
        print(f"CSD ({args.split}): {len(clips)} of {total} recordings have a reference key that three or four profiles agree on")
        groups = {clip: "english" if clip.startswith("en") else "korean" for clip in clips}
    else:
        references = vocadito_references(args.vocadito)
        total = len(list((args.vocadito / "Audio").glob("*.wav")))
        print(f"vocadito: {len(references)} of {total} clips have a reference key that three or four profiles agree on")
        clips = {clip: (args.vocadito / "Audio" / f"{clip}.wav", key) for clip, key in references.items()}
        groups = {clip: "vocadito" for clip in clips}

    results = {}
    for profile in args.profiles.split(","):
        with tempfile.TemporaryDirectory() as tmp:
            command = [str(args.analyzer), "--quiet", "--out-dir", tmp, "--key-profile", profile, *args.extra.split()]
            subprocess.run(command + [str(path) for path, _ in clips.values()], check=True)
            rows = {clip: read_key_rows(Path(tmp) / f"{clip}.key.csv") for clip in clips}

        summary = {}
        for group in sorted(set(groups.values())) + ["all"]:
            members = [c for c in clips if group == "all" or groups[c] == group]
            entry = {"clips": len(members)}
            for checkpoint in CHECKPOINTS + [None]:
                label = "end" if checkpoint is None else f"{checkpoint:g}s"
                estimates = [estimate_at(rows[c], checkpoint) for c in members]
                refs = [clips[c][1] for c in members]
                exact = np.mean([e == r for e, r in zip(estimates, refs)])
                mirex = np.mean([mir_eval.key.weighted_score(r, e) if e else 0.0 for e, r in zip(estimates, refs)])
                entry[label] = {"exact": float(exact), "mirex": float(mirex)}
            for label, estimates in (("3s", [estimate_at(rows[c], 3.0) for c in members]),
                                     ("end", [estimate_at(rows[c], None) for c in members])):
                counts = Counter(relation(e, clips[c][1]) for e, c in zip(estimates, members))
                entry[label]["relations"] = {r: counts.get(r, 0) for r in RELATIONS}
            found = [(auto_lock(rows[c]), clips[c][1]) for c in members]
            locked = [(k, t, r) for (k, t), r in found if k]
            counts = Counter(relation(k, r) for k, _, r in locked)
            entry["auto_lock"] = {
                "relations": {r: counts.get(r, 0) for r in RELATIONS},
                "reached": len(locked) / max(len(members), 1),
                "correct": float(np.mean([k == r for k, _, r in locked])) if locked else None,
                "compatible": float(np.mean([relation(k, r) in ("exact", "relative", "fifth") for k, _, r in locked])) if locked else None,
                "median_voiced_s": float(np.median([t for _, t, _ in locked])) if locked else None,
            }
            entry["lock"] = {}
            for margin in MARGINS:
                found = [(confident(rows[c], margin), clips[c][1]) for c in members]
                locked = [(k, t, r) for (k, t), r in found if k]
                counts = Counter(relation(k, r) for k, _, r in locked)
                entry["lock"][f"{margin:g}"] = {
                    "relations": {r: counts.get(r, 0) for r in RELATIONS},
                    "reached": len(locked) / max(len(members), 1),
                    "correct": float(np.mean([k == r for k, _, r in locked])) if locked else None,
                    "median_voiced_s": float(np.median([t for _, t, _ in locked])) if locked else None,
                }
            summary[group] = entry
        results[profile] = summary

        if args.per_file:
            for c in sorted(clips):
                print(f"  {profile:<18} {c:<40} ref {clips[c][1]:<9} 3s {estimate_at(rows[c], 3.0) or '-':<9} end {estimate_at(rows[c], None) or '-'}")

    groups_sorted = sorted({g for s in results.values() for g in s})
    for group in groups_sorted:
        n = next(iter(results.values()))[group]["clips"]
        print(f"\n{group} ({n} clips): exact key (MIREX score) after this much voice")
        print(f"  {'profile':<18}" + "".join(f"{c:>15}" for c in ["1s", "2s", "3s", "5s", "end"]))
        for profile, summary in results.items():
            cells = "".join(f"{summary[group][c]['exact']:>8.0%} ({summary[group][c]['mirex']:.2f})" for c in ["1s", "2s", "3s", "5s", "end"])
            print(f"  {profile:<18}{cells}")
        print("  confident lock, by margin: clips that reach it / correct when reached / median seconds of voice")
        for profile, summary in results.items():
            cells = "".join(
                f"  {m}: {p['reached']:.0%}/{p['correct']:.0%}/{p['median_voiced_s']:.1f}s" if p["correct"] is not None else f"  {m}: 0%"
                for m, p in summary[group]["lock"].items())
            print(f"  {profile:<18}{cells}")
        print("  the estimator's own lock: clips locked / exact / exact, relative or fifth / median seconds of voice")
        for profile, summary in results.items():
            a = summary[group]["auto_lock"]
            if a["correct"] is None:
                print(f"  {profile:<18}  none locked")
            else:
                print(f"  {profile:<18}  {a['reached']:.0%} / {a['correct']:.0%} / {a['compatible']:.0%} / {a['median_voiced_s']:.1f} s")
        print("  what the estimate is, against the reference (exact / relative / fifth / parallel / other / none)")
        for profile, summary in results.items():
            row = summary[group]
            three = "/".join(str(row["3s"]["relations"][r]) for r in RELATIONS)
            lock = "/".join(str(row["lock"]["0.2"]["relations"][r]) for r in RELATIONS)
            print(f"  {profile:<18}  at 3 s: {three}    at a lock with lead 0.2: {lock}")

    if args.json:
        args.json.write_text(json.dumps(results, indent=2))

    if args.check:
        failed = False
        for profile, summary in results.items():
            lock = summary["all"]["auto_lock"]
            compatible = lock["compatible"] or 0.0
            ok = lock["reached"] >= CHECK_LOCKED and compatible >= CHECK_COMPATIBLE
            failed = failed or not ok
            print(f"\ncheck {profile}: locked {lock['reached']:.0%} (floor {CHECK_LOCKED:.0%}), "
                  f"exact, relative or fifth {compatible:.0%} (floor {CHECK_COMPATIBLE:.0%}): {'ok' if ok else 'FAILED'}")
        return 1 if failed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
