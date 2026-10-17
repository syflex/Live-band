#!/usr/bin/env python3
"""Downloads the vocadito dataset (40 solo-singing clips with f0 and note annotations,
CC BY 4.0, https://zenodo.org/records/5578807) into testdata/external/vocadito.

It is used to evaluate pitch and onset detection on real singing:
  python tools/python/eval_pitch.py  --analyzer <lb-analyze> --vocadito testdata/external/vocadito
  python tools/python/eval_onsets.py --analyzer <lb-analyze> --vocadito testdata/external/vocadito

The data is not committed to this repository.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

RECORD_URL = "https://zenodo.org/api/records/5578807"
REPO_ROOT = Path(__file__).resolve().parents[2]
TARGET = REPO_ROOT / "testdata" / "external" / "vocadito"


def main() -> int:
    if (TARGET / "Audio").is_dir():
        print(f"already present: {TARGET}")
        return 0

    with urllib.request.urlopen(RECORD_URL, timeout=60) as response:
        record = json.load(response)
    entry = next(f for f in record["files"] if f["key"] == "vocadito.zip")
    expected_md5 = entry["checksum"].split(":", 1)[1]

    TARGET.parent.mkdir(parents=True, exist_ok=True)
    archive = TARGET.parent / "vocadito.zip"
    print(f"downloading {entry['size'] / 1e6:.0f} MB ...")
    with urllib.request.urlopen(entry["links"]["self"], timeout=600) as response, open(archive, "wb") as out:
        shutil.copyfileobj(response, out)

    md5 = hashlib.md5(archive.read_bytes()).hexdigest()
    if md5 != expected_md5:
        archive.unlink()
        print(f"checksum mismatch: got {md5}, expected {expected_md5}", file=sys.stderr)
        return 1

    with zipfile.ZipFile(archive) as z:
        for member in z.namelist():
            if member.startswith("__MACOSX") or member.endswith("/"):
                continue
            destination = (TARGET / member).resolve()
            if TARGET.resolve() not in destination.parents:
                continue  # never write outside the target folder
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(z.read(member))
    archive.unlink()
    print(f"extracted to {TARGET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
