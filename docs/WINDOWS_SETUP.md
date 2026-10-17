# Windows setup and first tests

This guide gets LiveBand running on the Lenovo laptop with the Jedel headset,
walks through the Phase 0 measurements, shows the Phase 1 features (the live
note display, voice files and recording) and the Phase 2 band: drums and
percussion that start when you tap the beat.

## 1. Get the app

### Option A: download a ready-made build (easiest)

1. On GitHub, open the repository, then the **Actions** tab.
2. Click the latest **CI** run with a green tick.
3. Under **Artifacts**, download **LiveBand-windows-x64** (you must be signed in
   to GitHub) and unzip it.
4. Double-click `LiveBand.exe`. Windows may warn that the app is unrecognised,
   because it is not signed. Click **More info**, then **Run anyway**.

Keep the `assets` folder next to `LiveBand.exe`: the drum kit, the grooves and the
test instrument live there. The download already includes the whole drum kit.

### Option B: build it yourself

1. Install [Visual Studio 2022 Community](https://visualstudio.microsoft.com/)
   with the **Desktop development with C++** workload. It includes CMake and
   Ninja.
2. Install [Git for Windows](https://git-scm.com/download/win).
3. Install [Python 3](https://www.python.org/downloads/) (tick **Add python.exe
   to PATH**). It is only needed to download the drum samples.
4. In a terminal:

   ```
   git clone --recursive --shallow-submodules https://github.com/syflex/Live-band.git
   cd Live-band
   python tools/python/fetch_assets.py
   ```

   The last command downloads the drum and percussion samples (41 MB, free CC0
   samples from the Versilian Community Sample Library). Without them only the kick
   plays, and the app says so.

5. In Visual Studio: **File → Open → Folder…** and choose the `Live-band`
   folder. Wait for CMake to finish.
6. Pick the **Windows Release (MSVC)** configuration in the toolbar, then
   **Build → Build All**. Use Release: a Debug build is too slow for the drum kit
   and will crackle.
7. Choose **LiveBand.exe** as the startup item and press the green play button.

## 2. Prepare the headset

Windows applies "voice effects" to headset mics that would confuse pitch
detection, and other apps can block exclusive mode. Do this once:

1. Plug the headset straight into the laptop, not through a hub. Note whether it
   uses **USB** or the **3.5 mm jack**.
2. **Settings → System → Sound**: choose the headset for both output and input.
3. Scroll down to **More sound settings**. This opens the classic Sound window.
   - **Playback** tab → the headset → **Properties**:
     - **Advanced**: set the default format to **48000 Hz** (16 or 24 bit),
       and tick both **exclusive mode** boxes.
     - Turn **audio enhancements** off (on the Advanced or Enhancements tab,
       depending on the Windows version).
   - **Recording** tab → the headset mic → **Properties**:
     - **Advanced**: **48000 Hz**, and tick both exclusive mode boxes.
     - **Levels**: about 70–80. Use at most +10 dB of mic boost.
     - Turn enhancements off.
   - **Communications** tab: choose **Do nothing**.
4. On Windows 11, also open **Settings → System → Sound → (the headset mic)**
   and turn off **Audio enhancements** and **Voice clarity** if they appear.
5. If the headset came with its own software, turn off virtual surround, EQ and
   noise cancelling.
6. Close every other app that might use the headset: Zoom, Teams, Discord,
   WhatsApp, and browser tabs that play sound. Exclusive mode needs the headset
   to itself.

## 3. Prepare the laptop

1. Plug the laptop into mains power.
2. **Settings → System → Power**: set **Power mode** to **Best performance**.
   In Lenovo Vantage, choose the Performance (or Extreme Performance) mode if
   it has one.
3. **Control Panel → Power Options → Change plan settings → Change advanced
   power settings → USB settings → USB selective suspend**: **Disabled**.
4. Run [LatencyMon](https://www.resplendence.com/latencymon) (free for home
   use) as administrator for 10 minutes while playing some audio. Note its
   verdict. If it says the system is not suitable for real-time audio, note
   the driver it highlights. Wi-Fi and Bluetooth drivers are common culprits;
   turning them off often helps.

## 4. Run the Phase 0 test

1. Start LiveBand. The report at the bottom shows which driver it opened. It
   tries **Windows Audio (Exclusive Mode)** first.
2. Click **Audio settings…** and check the choices:
   - Audio device type **Windows Audio (Exclusive Mode)**.
   - Output: the headset. Input: the headset mic.
   - Sample rate **48000**, buffer **128** samples.
3. Press the number keys **1** to **8**. You should hear test tones in the
   headset. **Esc** stops them.
4. Put the headset on and tick **Hear my voice**. Sing or talk. Listen for any
   echo or "doubling" of your own voice, which means the latency is too high.
5. Try smaller buffers in Audio settings (64, then 32). Stop at the first size
   that crackles and go back one step. That is the smallest working buffer.
6. **Measure latency**: take the headset **off**, hold the mic boom against one
   ear cup, click **Measure latency**, then **Start**. The app plays seven short
   chirps. The result appears as **Measured round trip**. If it says the chirps
   were not heard, hold the mic closer and turn the volume up.
7. Leave the app running for 30 minutes with the band playing (section 6; press **Enter**
   to start it, and **N** now and then to change section). Click **Reset peaks**
   first. Watch **Overruns** and **Driver-reported dropouts**; both should stay
   at 0.
8. Click **Copy report** and paste the text into the chat or a GitHub issue.

## 5. Try the live note display (Phase 1)

![The note display and pitch graph](images/phase1/app-live-pitch.png)

1. Put the headset on and sing a few long notes. The big note name shows what you
   are singing, the needle shows how sharp or flat you are, and the moving graph
   shows your pitch over the last 6 seconds.
2. Lines on the graph mark where the app thinks each new note started: orange when
   it heard a burst of sound, lilac when it heard the pitch change.
3. **Voice file**: click **Load voice file…** (or drop a WAV file on the window) and
   the app listens to that file, in a loop, instead of the mic. Click **Microphone**
   to go back.
4. **Recording**: press **Record**, sing, then press **Stop**. **Show recordings** opens
   the folder. [RECORDING_GUIDE.md](RECORDING_GUIDE.md) explains how to record the
   test songs.

## 6. Play with the band (Phase 2)

![The band playing the Afrobeats verse while the app listens to a voice file](images/phase2/app-band-playing.png)

The band row is under the audio settings. The lights below it show the beat you are
hearing: green while the band plays, orange during a fill, grey during a count-in.

1. **Start by tapping.** Put the headset on and tap **Space** four times, in time
   with the song in your head. The band comes in on the fifth beat, the one your
   taps predict. Tapping half or double speed is fine: the tempo is moved into the
   style's range (90 to 125 BPM for Afrobeats).
2. **Or start with a count-in.** Press **Enter**: you hear one bar of sticks at the
   tempo shown, then the band.
3. **Change section.** Press **N** (or **Next**): the drums play a fill to the end of
   the bar, and the chorus starts on the next bar. Press **N** again to go back to
   the verse. **F** (or **Fill**) plays a fill and stays in the same section.
4. **Correct the tempo while it plays.** Tap four or more times on the beat: the band
   moves onto your taps over a few beats, without a jump. The **BPM** box sets the
   tempo directly.
5. **Stop.** Press **Enter** (or **Stop**): a fill, then a final kick and crash on the
   next downbeat. **Esc** stops everything at once.
6. **Mix.** **Drums** and **Perc** switch the two parts on and off; **Band volume**
   sets their level against your voice. Nothing louder than -1 dBFS reaches the
   headphones.
7. **MIDI.** A MIDI pedal or pad plugged in before or after starting the app works
   too: any note, or the sustain pedal, taps. Controllers 81, 82 and 83 are start/stop,
   next and fill. MIDI taps are timed more precisely than key presses.

Do the latency test (section 4, step 6) before playing: the app uses the measured
latency to bring the band in exactly on the beat you hear.

### Measuring the tap entry (optional)

This checks, on the real laptop, that the band comes in on the fifth beat:

1. Take the headset **off** and hold the mic against one ear cup, as for the latency
   test. Turn **Hear my voice** off and the **Click** off.
2. Press **Record**. Wait two seconds, then tap **Space** four times, firmly, close to
   the mic so it hears the key clicks. Let the band play for a few bars, press
   **Enter** to stop it, then **Stop** the recording.
3. Send the session folder (from **Show recordings**), or run:

   ```
   python tools/python/check_tap_entry.py <session folder>
   ```

   It finds your four taps and the band's first beat in `input.wav` and prints how
   far the band came in from the beat your taps predicted. The target is within
   10 ms. Keyboards take some milliseconds to report a key press, so a consistent
   lateness here measures the keyboard rather than the band.

## 7. What to send back

- The copied report, taken at the smallest buffer that works without crackles, with
  the band playing.
- How the band felt: did it come in where you expected after four taps? Did the
  Afrobeats groove sound right? What would a Nigerian drummer change?
- Whether you heard crackles, or an echo of your own voice with **Hear my
  voice** on.
- The LatencyMon verdict.
- Whether the headset uses USB or the 3.5 mm jack, and the laptop model
  (**Settings → System → About**).

## What the numbers mean

| Report line | Good value | Meaning |
|-------------|------------|---------|
| Measured round trip | under 15 ms | Time from the mic, through the app, back to the headset. It affects how the singer hears their own voice. The band is scheduled ahead, and the app uses half this time as the output delay when it brings the band in on a tapped beat. |
| Tap timing allows for | about half the round trip | The output delay the band's entry is corrected for. Without a latency measurement it is the driver's figure plus one buffer. |
| Buffer | 128 samples or less | Smaller means less latency but more risk of crackles. |
| Audio callback load | under 30 % average | How much of each buffer's time the app needs for its audio work. |
| Overruns | 0 | Times the app was too slow and the sound had a gap. |
| Driver-reported dropouts | 0 | Gaps the driver noticed. Some drivers do not report this. |
| Real-time allocations | 0 | Debug builds only: memory allocations in the audio thread, which must never happen. |
