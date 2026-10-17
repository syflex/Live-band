"""Shared helpers for the LiveBand evaluation scripts."""

from __future__ import annotations

import csv
import subprocess
from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass
class Frames:
    time: np.ndarray
    f0_hz: np.ndarray  # raw YIN estimate, 0 when unvoiced
    midi: np.ndarray  # smoothed pitch, 0 when unvoiced
    voiced: np.ndarray  # bool

    @property
    def smoothed_hz(self) -> np.ndarray:
        return np.where(self.midi > 0, 440.0 * 2.0 ** ((self.midi - 69.0) / 12.0), 0.0)


def run_analyzer(analyzer: Path, audio: list[Path], out_dir: Path, extra_args: list[str] | None = None) -> None:
    """Runs lb-analyze over the audio files, writing CSVs into out_dir."""
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(analyzer), "--quiet", "--out-dir", str(out_dir), *(extra_args or []), *map(str, audio)]
    subprocess.run(cmd, check=True)


def load_frames(path: Path) -> Frames:
    data = np.genfromtxt(path, delimiter=",", names=True)
    return Frames(time=data["time_s"], f0_hz=data["f0_hz"], midi=data["midi"], voiced=data["voiced"] > 0.5)


def load_onsets(path: Path) -> np.ndarray:
    with open(path, newline="") as f:
        return np.array([float(row["time_s"]) for row in csv.DictReader(f)])


def load_two_columns(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Loads a headerless two-or-more-column CSV (time, value, ...)."""
    data = np.loadtxt(path, delimiter=",", ndmin=2)
    return data[:, 0], data[:, 1]


def resample_to(times: np.ndarray, source_times: np.ndarray, source_values: np.ndarray) -> np.ndarray:
    """Nearest-neighbour lookup of source_values at the given times (0 outside the source range)."""
    idx = np.clip(np.searchsorted(source_times, times), 1, len(source_times) - 1)
    left, right = source_times[idx - 1], source_times[idx]
    idx = np.where(np.abs(times - left) <= np.abs(right - times), idx - 1, idx)
    out = source_values[idx]
    out[(times < source_times[0] - 0.02) | (times > source_times[-1] + 0.02)] = 0.0
    return out
