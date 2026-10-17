# Test data

Vocal recordings are **not** stored in this public repository. Keep them in
private storage and copy them here locally; everything in this folder except
this file is ignored by git.

Contents:

```
testdata/
  external/vocadito/   public evaluation dataset, fetched by tools/python/fetch_vocadito.py
  sessions/            copies of LiveBand session folders (input.wav, clicks.csv, session.json ...)
  notes.csv            one row per session: folder, singer code, song, key, meter, tempo, notes
```

[docs/RECORDING_GUIDE.md](../docs/RECORDING_GUIDE.md) explains how to record the
sessions. Use singer codes (S01, S02, …) and keep each singer's written consent
separately from the recordings.
