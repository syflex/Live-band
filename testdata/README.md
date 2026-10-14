# Test data

Vocal recordings are **not** stored in this public repository. Keep them in
private storage and copy them here locally; everything in this folder except
this file is ignored by git.

Expected layout once recordings exist (Phase 1):

```
testdata/
  manifest.csv     one row per clip: id, singer_id, key, mode, bpm or ramp, click file, notes
  clips/*.wav      48 kHz, 24-bit, mono, dry
  clicks/*.wav     the click each singer heard, for beat ground truth
```

Use anonymised singer IDs (S01, S02, …) and keep each singer's written
consent with the recordings.
