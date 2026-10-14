# Windows setup and Phase 0 test

This guide gets LiveBand running on the Lenovo laptop with the Jedel headset,
and walks through the Phase 0 measurements.

## 1. Get the app

### Option A: download a ready-made build (easiest)

1. On GitHub, open the repository, then the **Actions** tab.
2. Click the latest **CI** run with a green tick.
3. Under **Artifacts**, download **LiveBand-windows-x64** (you must be signed in
   to GitHub) and unzip it.
4. Double-click `LiveBand.exe`. Windows may warn that the app is unrecognised,
   because it is not signed. Click **More info**, then **Run anyway**.

Keep the `assets` folder next to `LiveBand.exe`; the test instrument lives there.

### Option B: build it yourself

1. Install [Visual Studio 2022 Community](https://visualstudio.microsoft.com/)
   with the **Desktop development with C++** workload. It includes CMake and
   Ninja.
2. Install [Git for Windows](https://git-scm.com/download/win).
3. In a terminal:

   ```
   git clone --recursive --shallow-submodules https://github.com/syflex/Live-band.git
   ```

4. In Visual Studio: **File → Open → Folder…** and choose the `Live-band`
   folder. Wait for CMake to finish.
5. Pick the **Windows Release (MSVC)** configuration in the toolbar, then
   **Build → Build All**.
6. Choose **LiveBand.exe** as the startup item and press the green play button.

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
3. Press the keys **A S D F G H J K**. You should hear test tones in the
   headset. **Esc** stops them.
4. Put the headset on and tick **Hear my voice**. Sing or talk. Listen for any
   echo or "doubling" of your own voice, which means the latency is too high.
5. Try smaller buffers in Audio settings (64, then 32). Stop at the first size
   that crackles and go back one step. That is the smallest working buffer.
6. **Measure latency**: take the headset **off**, hold the mic boom against one
   ear cup, click **Measure latency**, then **Start**. The app plays seven short
   chirps. The result appears as **Measured round trip**. If it says the chirps
   were not heard, hold the mic closer and turn the volume up.
7. Leave the app running for 10–30 minutes, with test notes now and then.
   Watch **Overruns** and **Driver-reported dropouts**; both should stay at 0.
8. Click **Copy report** and paste the text into the chat or a GitHub issue.

## 5. What to send back

- The copied report, taken at the smallest buffer that works without crackles.
- Whether you heard crackles, or an echo of your own voice with **Hear my
  voice** on.
- The LatencyMon verdict.
- Whether the headset uses USB or the 3.5 mm jack, and the laptop model
  (**Settings → System → About**).

## What the numbers mean

| Report line | Good value | Meaning |
|-------------|------------|---------|
| Measured round trip | under 15 ms | Time from the mic, through the app, back to the headset. Only affects how the singer hears their own voice; the band is scheduled ahead and plays on time anyway. |
| Buffer | 128 samples or less | Smaller means less latency but more risk of crackles. |
| Audio callback load | under 30 % average | How much of each buffer's time the app needs for its audio work. |
| Overruns | 0 | Times the app was too slow and the sound had a gap. |
| Driver-reported dropouts | 0 | Gaps the driver noticed. Some drivers do not report this. |
| Real-time allocations | 0 | Debug builds only: memory allocations in the audio thread, which must never happen. |
