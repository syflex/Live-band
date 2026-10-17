#!/usr/bin/env python3
"""Measures how far from the predicted beat the band came in after four taps.

Record a session with the headset off and its mic held against an ear cup (acoustic
loopback), the click off, and four firm taps on the space bar close to the mic (see
docs/WINDOWS_SETUP.md, section 6). The mic then hears both the key clicks and the band,
through the same input path, so the input latency cancels out:

  python tools/python/check_tap_entry.py <session folder>

It finds the last four clicks before the band starts in input.wav, fits them with a
straight line, predicts the fifth, finds the band's first hit in input.wav and prints
the difference. Positive means the band came in late.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt


def envelope(x: np.ndarray, rate: int, low_hz: float | None, smooth_ms: float = 1.0) -> np.ndarray:
    if low_hz is not None:
        x = sosfiltfilt(butter(4, low_hz, "high", fs=rate, output="sos"), x)
    n = max(1, int(smooth_ms * 1e-3 * rate))
    return np.convolve(np.abs(x), np.ones(n) / n, mode="same")


def find_clicks(env: np.ndarray, rate: int, end: int, min_gap_s: float = 0.15) -> list[float]:
    """Times of sharp peaks before sample `end`: the key clicks."""
    region = env[:end]
    if len(region) == 0:
        return []
    floor = np.median(region)
    threshold = floor + 0.3 * (np.max(region) - floor)
    clicks: list[float] = []
    i = 0
    gap = int(min_gap_s * rate)
    while i < len(region):
        if region[i] > threshold:
            clicks.append(i / rate)
            i += gap
        else:
            i += 1
    return clicks


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("session", type=Path)
    args = parser.parse_args()

    audio, rate = sf.read(args.session / "input.wav", always_2d=True)
    audio = audio.mean(axis=1)
    session = json.loads((args.session / "session.json").read_text())
    with open(args.session / "beats.csv", newline="") as f:
        beats = [row for row in csv.DictReader(f)]
    downbeats = [row for row in beats if row["bar"] == "0" and row["beat"] == "0" and row["count_in"] == "0"]
    if not downbeats:
        print("no band entry in beats.csv: did the band start during the recording?", file=sys.stderr)
        return 1
    sent = float(downbeats[0]["time_s"])
    band_bpm = float(downbeats[0]["bpm"])

    # The band cannot reach the mic before it was sent; the taps all came before that.
    taps = find_clicks(envelope(audio, rate, 2000.0), rate, int((sent - 0.02) * rate))
    if len(taps) < 4:
        print(f"found {len(taps)} taps before the band came in; four are needed", file=sys.stderr)
        return 1
    taps = np.array(taps[-4:])
    index = np.arange(4)
    period, phase = np.polyfit(index, taps, 1)
    predicted = phase + 4 * period

    # The band's first hit: the first time the level rises past a third of its first bar's peak.
    env = envelope(audio, rate, None, smooth_ms=0.5)
    start = int(sent * rate)
    window = env[start : start + int(0.3 * rate)]
    if len(window) == 0 or window.max() <= 0:
        print("the band is silent in input.wav: was the mic against the ear cup?", file=sys.stderr)
        return 1
    arrival = (start + int(np.argmax(window > window.max() / 3))) / rate

    tapped_bpm = 60.0 / period
    print(f"taps at {', '.join(f'{t:.3f}' for t in taps)} s: {tapped_bpm:.1f} BPM")
    print(f"band sent at {sent:.3f} s, heard at the mic at {arrival:.3f} s, at {band_bpm:.1f} BPM")
    print(f"entry error: {(arrival - predicted) * 1000:+.1f} ms against the predicted fifth tap at {predicted:.3f} s")
    folded = band_bpm / tapped_bpm
    octave = 2.0 ** round(np.log2(folded))
    print(f"tempo error: {(folded / octave - 1) * 100:+.2f} %" + (f" (the band plays {octave:g}x the tapped tempo)" if octave != 1 else ""))
    if session.get("measuredRoundTripMs") is None:
        print("note: no latency measurement was saved, so the app used the driver's latency figure")
    return 0


if __name__ == "__main__":
    sys.exit(main())
