#!/usr/bin/env python3
"""Builds a key-detection test set of synthesized singing with known keys.

Until our own recordings exist, this gives key detection melodies whose key is known for
certain, sung by a simple synthetic voice with vibrato, glides between notes, a random
tuning offset of up to 40 cents and a random transposition:

  hymns  soprano lines of Bach chorales, which are hymn tunes; the key comes from the
         full four-part harmony and must agree with the key signature
  folk   tunes from Ryan's Mammoth Collection (1883), whose key is stated in the tune

Both come with music21 (BSD licence; pip install music21). The clips and labels.csv go to
testdata/external/keyset/, which is not committed. Then:

  python tools/python/eval_key.py --analyzer <lb-analyze> --labels testdata/external/keyset/labels.csv
"""

from __future__ import annotations

import argparse
import csv
import random
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO_ROOT = Path(__file__).resolve().parents[2]
RATE = 32000
NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def key_label(tonic_pc: int, minor: bool) -> str:
    return f"{NOTE_NAMES[tonic_pc % 12]} {'minor' if minor else 'major'}"


def melody_events(part) -> list[tuple[float, float, int | None]]:
    """(start in quarter notes, length, MIDI or None for a rest) for a monophonic part."""
    events = []
    for element in part.flatten().notesAndRests:
        length = float(element.quarterLength)
        if length <= 0:
            continue
        if element.isRest:
            events.append((float(element.offset), length, None))
        elif element.isChord:
            events.append((float(element.offset), length, max(p.midi for p in element.pitches)))
        else:
            events.append((float(element.offset), length, element.pitch.midi))
    return sorted(events)


def bach_items(rng: random.Random, count: int):
    from music21 import corpus

    paths = sorted(str(p) for p in corpus.getComposer("bach") if str(p).endswith((".mxl", ".xml", ".krn")))
    rng.shuffle(paths)
    for path in paths:
        if count <= 0:
            return
        try:
            score = corpus.parse(path)
            if len(score.parts) < 4:
                continue
            key = score.analyze("key")
            signatures = list(score.parts[0].flatten().getElementsByClass("KeySignature"))
            if not signatures or key.mode not in ("major", "minor"):
                continue
            major_tonic = (signatures[0].sharps * 7) % 12
            expected = major_tonic if key.mode == "major" else (major_tonic + 9) % 12
            if key.tonic.pitchClass != expected:
                continue  # the harmony and the key signature disagree: leave it out
            events = melody_events(score.parts[0])
        except Exception:  # noqa: BLE001 - some corpus files do not parse; skip them
            continue
        if len(events) < 12:
            continue
        count -= 1
        yield Path(path).stem, events, key.tonic.pitchClass, key.mode == "minor", (0.45, 0.75)


def folk_items(rng: random.Random, count: int):
    from music21 import corpus

    paths = sorted(str(p) for p in corpus.getComposer("ryansMammoth"))
    rng.shuffle(paths)
    for path in paths:
        if count <= 0:
            return
        try:
            parsed = corpus.parse(path)
            score = parsed.scores[0] if hasattr(parsed, "scores") else parsed
            keys = list(score.flatten().getElementsByClass("Key"))
            if not keys or keys[0].mode not in ("major", "minor"):
                continue  # modal tunes (Dorian, Mixolydian) have no major or minor answer
            key = keys[0]
            part = score.parts[0] if score.parts else score
            events = melody_events(part)
        except Exception:  # noqa: BLE001
            continue
        if len(events) < 16:
            continue
        count -= 1
        yield f"ryan-{Path(path).stem}", events, key.tonic.pitchClass, key.mode == "minor", (0.25, 0.4)


def sing(events, quarter_seconds: float, transpose: int, tuning_cents: float, seconds: float, rng: random.Random) -> np.ndarray:
    """A synthetic sung rendering: harmonics shaped by an 'ah' vowel, vibrato, glides, breaths."""
    total = int(seconds * RATE)
    f0 = np.zeros(total)
    gain = np.zeros(total)
    glide = int(0.04 * RATE)
    previous_end_midi = None
    for start_q, length_q, midi in events:
        start = int(start_q * quarter_seconds * RATE)
        end = min(total, int((start_q + length_q) * quarter_seconds * RATE))
        if start >= total:
            break
        if midi is None:
            previous_end_midi = None
            continue
        pitch = midi + transpose + tuning_cents / 100.0
        n = end - start
        contour = np.full(n, pitch)
        if previous_end_midi is not None and n > glide:
            contour[:glide] = previous_end_midi + (pitch - previous_end_midi) * (0.5 - 0.5 * np.cos(np.linspace(0, np.pi, glide)))
        t = np.arange(n) / RATE
        depth = 0.3 * np.clip((t - 0.25) / 0.3, 0.0, 1.0)  # vibrato grows in on longer notes
        contour += depth * np.sin(2 * np.pi * rng.uniform(5.0, 6.2) * t + rng.uniform(0, 2 * np.pi))
        f0[start:end] = 440.0 * 2 ** ((contour - 69.0) / 12.0)
        envelope = np.minimum(1.0, t / 0.03) * np.minimum(1.0, (n - np.arange(n)) / (0.06 * RATE))
        gain[start:end] = envelope * rng.uniform(0.6, 1.0)
        previous_end_midi = pitch

    phase = 2 * np.pi * np.cumsum(f0) / RATE
    voice = np.zeros(total)
    formants = [(700, 110, 1.0), (1220, 120, 0.5), (2600, 160, 0.25)]
    for k in range(1, 25):
        frequency = k * f0
        amplitude = np.zeros(total)
        for centre, width, level in formants:
            amplitude += level / (1 + ((frequency - centre) / width) ** 2)
        amplitude = (amplitude + 0.05) / k ** 0.5
        amplitude[frequency > 0.45 * RATE] = 0.0
        voice += amplitude * np.sin(k * phase)
    voice *= gain
    voice /= max(1e-9, np.max(np.abs(voice)))
    noise = np.random.default_rng(rng.randrange(1 << 30)).standard_normal(total) * 0.003
    return (0.5 * voice + noise).astype(np.float32)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "testdata" / "external" / "keyset")
    parser.add_argument("--hymns", type=int, default=100)
    parser.add_argument("--folk", type=int, default=100)
    parser.add_argument("--seconds", type=float, default=15.0)
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for source, items in (("hymn", bach_items(rng, args.hymns)), ("folk", folk_items(rng, args.folk))):
        for name, events, tonic, minor, quarter_range in items:
            transpose = rng.randint(-6, 5)
            pitches = [m for _, _, m in events if m is not None]
            median = float(np.median(pitches)) + transpose
            octave = 12 * round((rng.choice([60.0, 67.0]) - median) / 12)  # a lower or a higher voice
            transpose += octave
            tuning = rng.uniform(-40.0, 40.0)
            quarter = rng.uniform(*quarter_range)
            audio = sing(events, quarter, transpose, tuning, args.seconds, rng)
            file_name = f"{source}-{name}.flac"
            sf.write(args.out / file_name, audio, RATE)
            label_tonic = (tonic + transpose) % 12
            rows.append({"file": file_name, "key": key_label(label_tonic, minor), "source": source,
                         "transpose": transpose, "tuning_cents": round(tuning, 1), "quarter_s": round(quarter, 3)})
            print(f"{file_name}: {key_label(label_tonic, minor)}")

    with open(args.out / "labels.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"{len(rows)} clips in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
