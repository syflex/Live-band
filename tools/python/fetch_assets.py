#!/usr/bin/env python3
"""Downloads the third-party samples the instrument kits need.

Each kit folder under assets/sfz/ that has a samples.json lists its downloaded files
with their source and SHA-256. This script fetches the missing ones and checks every
file. It needs only Python 3.9 or newer, no extra packages:

  python tools/python/fetch_assets.py

The samples are not committed to this repository; see THIRD_PARTY.md for their licences.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch_one(kit: Path, base_url: str, name: str, entry: dict) -> str | None:
    """Downloads one file if it is missing or wrong. Returns an error message, or None."""
    target = (kit / name).resolve()
    if kit.resolve() not in target.parents:
        return f"{name}: refusing to write outside {kit}"
    if target.exists() and sha256(target) == entry["sha256"]:
        return None
    url = base_url + urllib.parse.quote(entry["source"])
    try:
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
    except OSError as error:
        return f"{name}: {error}"
    digest = hashlib.sha256(data).hexdigest()
    if digest != entry["sha256"]:
        return f"{name}: checksum mismatch (got {digest[:12]}, expected {entry['sha256'][:12]})"
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    partial.write_bytes(data)
    partial.replace(target)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--assets", type=Path, default=REPO_ROOT / "assets" / "sfz")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    failures = []
    for manifest_path in sorted(args.assets.glob("*/samples.json")):
        kit = manifest_path.parent
        manifest = json.loads(manifest_path.read_text())
        files = manifest["files"]
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            errors = list(pool.map(lambda item: fetch_one(kit, manifest["base_url"], *item), files.items()))
        errors = [e for e in errors if e]
        total = sum(entry["bytes"] for entry in files.values())
        status = "ok" if not errors else f"{len(errors)} failed"
        print(f"{kit.name}: {len(files)} files, {total / 1e6:.1f} MB, {status}")
        failures += errors

    for error in failures:
        print(f"  {error}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
