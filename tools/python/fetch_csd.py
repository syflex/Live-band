#!/usr/bin/env python3
"""Downloads the Children's Song Dataset (Choi, Kim, Park, Yong and Nam, ISMIR 2020): 50
English and 50 Korean children's songs sung by a professional singer, each in two keys,
with every note annotated. CC BY-NC-SA 4.0, https://doi.org/10.5281/zenodo.4916302
(1.85 GB) into testdata/external/csd.

It is used to evaluate key detection on real singing:
  python tools/python/eval_key.py --analyzer <lb-analyze> --csd testdata/external/csd/CSD --split test

The licence does not allow commercial use, so the data is only used for evaluation, never
built into the app, and not committed to this repository.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

RECORD_URL = "https://zenodo.org/api/records/4916302"
REPO_ROOT = Path(__file__).resolve().parents[2]
TARGET = REPO_ROOT / "testdata" / "external" / "csd"


def main() -> int:
    if (TARGET / "CSD" / "english" / "wav").is_dir():
        print(f"already present: {TARGET}")
        return 0

    with urllib.request.urlopen(RECORD_URL, timeout=60) as response:
        record = json.load(response)
    entry = next(f for f in record["files"] if f["key"] == "CSD.zip")
    expected_md5 = entry["checksum"].split(":", 1)[1]

    TARGET.mkdir(parents=True, exist_ok=True)
    archive = TARGET / "CSD.zip"
    print(f"downloading {entry['size'] / 1e6:.0f} MB ...")
    md5 = hashlib.md5()
    with urllib.request.urlopen(entry["links"]["self"], timeout=600) as response, open(archive, "wb") as out:
        while chunk := response.read(1 << 20):
            md5.update(chunk)
            out.write(chunk)
    if md5.hexdigest() != expected_md5:
        archive.unlink()
        print(f"checksum mismatch: got {md5.hexdigest()}, expected {expected_md5}", file=sys.stderr)
        return 1

    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            if member.filename.startswith("__MACOSX") or member.is_dir():
                continue
            destination = (TARGET / member.filename).resolve()
            if TARGET.resolve() not in destination.parents:
                continue  # never write outside the target folder
            destination.parent.mkdir(parents=True, exist_ok=True)
            with z.open(member) as source, open(destination, "wb") as out:
                shutil.copyfileobj(source, out)
    archive.unlink()
    print(f"extracted to {TARGET / 'CSD'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
