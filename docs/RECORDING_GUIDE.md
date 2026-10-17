# Recording the test set

The pitch and onset detection work well on a public dataset of singers from other
countries ([Phase 1 results](results/PHASE1_RESULTS.md)). Before building key detection,
we need recordings of **Nigerian singers singing Nigerian songs**, because that is who the
product is for. LiveBand records these itself; you do not need any other software.

## What to record

Aim for **15 to 20 clips**, each **30 to 60 seconds**:

- **At least 4 singers**, men and women.
- **Nigerian songs** that singers know well: Afrobeats and pop, and church worship and
  praise songs.
- **A mix of keys** (at least 10 different ones; this happens naturally with different
  songs and singers) and both happy-sounding (major) and sad-sounding (minor) songs.
- **A mix of tempos**, from slow worship (about 60 BPM) to upbeat (about 140 BPM).
- **Some 6/8 songs.** Many slow worship songs have a swaying "1-2-3-4-5-6" feel. Choose 6/8
  for those in the app.
- **About 5 clips with a changing tempo.** Set **Speed up 5%** or **Slow down 5%** and the
  click drifts slowly; the singer follows it.
- **About 3 clips sung freely**, with the click turned off.

## Before you start

1. Follow [WINDOWS_SETUP.md](WINDOWS_SETUP.md) once: headset settings, power settings,
   and the latency test.
2. Use a quiet room. Turn off fans and air conditioning if you can.
3. The singer **wears the headset**, so the click goes into their ears and not into the mic.
4. Ask each singer for permission to record them and to use the recording for testing
   (see "Consent" below).

## Recording a clip

1. In **Voice**, choose **Microphone**.
2. Ask the singer to sing a few loud notes. On the **Mic** meter, the loudest notes should
   reach about **-12 to -6 dB**, in the green or yellow, never red. If they are too quiet or
   too loud, change the mic level in Windows (see WINDOWS_SETUP.md, step 2).
3. In **Record**, set the tempo, the time signature (**4/4** or **6/8**) and **Steady tempo**,
   **Speed up 5%** or **Slow down 5%**. Tick **Click**, or untick it for a freely sung clip.
   - Not sure of the tempo? Play the song on your phone and tap along with a free tap-tempo
     website or app, then type that number in.
   - In 6/8, the tempo counts the slow swaying beat (two per bar), and the click plays all
     six eighth notes with the first one accented.
4. Press **Record**. The click plays one bar to count in (4 clicks in 4/4, 6 in 6/8), then
   the singer starts on the next beat.
5. Press **Stop** when the singer finishes.
6. Write a line in your notes for the clip (see below).

Each recording goes into its own folder in **Documents\LiveBand\Sessions**, named with the
date and time. **Show recordings** opens that folder.

## Notes for each clip

Keep a simple table, for example in a spreadsheet. Use **singer codes** (S01, S02, …)
rather than names.

| folder | singer | song | key (if known) | 4/4 or 6/8 | tempo | tempo change | notes |
|---|---|---|---|---|---|---|---|
| 2026-10-20_14-03-22 | S01 | Song title | G major | 4/4 | 100 | steady | |
| 2026-10-20_14-10-05 | S02 | Song title | unknown | 6/8 | 66 | speed up 5% | some coughing at 0:20 |

If someone who plays an instrument knows the key of each song as the singer sang it,
write it down. It becomes the right answer the key detector is scored against in
Phase 3. If not, write "unknown" and we will work it out from the recordings.

## What is in each folder

| File | What it is |
|---|---|
| `input.wav` | The singer's voice only (the click is not in it). |
| `output.wav` | What the singer heard: the click, plus their voice if "Hear my voice" was on. |
| `analysis.csv` | The live pitch and onset analysis, every 8 ms. |
| `onsets.csv`, `clicks.csv` | Detected note starts, and when each click played. |
| `session.json` | The settings: tempo, time signature, devices and so on. |

## Sharing the recordings

**Do not put recordings in the GitHub repository.** It is public, and the recordings are
of real people.

Put the session folders and your notes table in a **private** folder, for example on
Google Drive, and tell me its name. In a session where your Google Drive is connected, I
can read the files from there, with your approval, and run the evaluation on them.

## Consent

A short written note per singer is enough. For example:

> I agree that LiveBand may record my singing on (date) and use the recordings to test and
> improve its software. The recordings will be stored privately, will not be published,
> and will be identified only by a code. I can ask for them to be deleted at any time.
>
> Name, signature, date

Keep the signed notes separately from the recordings.
