# Modded_f-king_cycles

A free, modded firmware for the **Elektron Model:Cycles** (OS 1.13), with a companion plug-in (VST3 / AU /
standalone) for Mac, Windows and Linux.

- **25 machines**: Elektron's six, plus 19 added ones: classic drum machines (909, 808, 606), a rumble kick, modal
  metal, an additive drum voice, acid and growl basses, Braids, virtual-analog, cross-mod, 6-operator FM, a pad, and
  a sampler, granular and wavetable engine that play your own samples.
- **A filter and an insert effect on every track**, a sidechain, a master DJ EQ, 6-channel USB audio, an LFO
  polarity switch, a CPU monitor and CPU budget, and quality-of-life changes to the screens.
- **The plug-in** shows every knob of every machine, follows the track and machine you pick on the Cycles, reads the
  sound back from the machine, picks samples and projects, and records automation in your DAW.

This repository never contains Elektron's firmware. You build the modded firmware yourself, on your computer, from
the official OS file you download from Elektron. The patcher checks that file and the result byte for byte.

> **Experimental firmware.** Every part is tested in emulation, running the Model:Cycles' own code, and the
> firmware has been played on real hardware, but use it at your own risk. Back up your projects and samples with
> Elektron Transfer before flashing. The official OS can always be put back (see [Going back](#going-back-to-the-official-os)).

**Manual:** the full owner's manual (49 pages, every machine, every button, the plug-in, and how the firmware
manages the CPU) is **[docs/Modded_f-king_cycles-Manual.pdf](docs/Modded_f-king_cycles-Manual.pdf)**.

## Contents

- [Build the firmware](#build-the-firmware)
- [Flash it](#flash-it)
- [Going back to the official OS](#going-back-to-the-official-os)
- [The machines](#the-machines)
- [What else changes on the Cycles](#what-else-changes-on-the-cycles)
- [Samples](#samples)
- [MIDI](#midi)
- [The plug-in](#the-plug-in)
- [Credits](#credits) and [licenses](#licenses)

## Build the firmware

You need **Python 3.8 or later** (nothing else) and the official Model:Cycles OS 1.13 file.

1. Download `model-cycles_OS1.13.syx` from [elektron.se](https://www.elektron.se/support-downloads/modelcycles)
   (Model:Cycles › Support › Downloads; unzip it if it comes as a `.zip`).
2. Download `Modded_f-king_cycles-firmware-patcher.zip` from the latest [release](../../releases) and unzip it
   (or download this repository and use its `firmware/` folder).
3. In a terminal, in that folder:

   ```sh
   python3 patch.py path/to/model-cycles_OS1.13.syx
   ```

   On Windows, type `py` instead of `python3`. You can also drag the `.syx` file onto the terminal window to type
   its path.

The patcher checks that your file is the official one, applies the mods, checks the result against the expected
SHA-256 and writes **`modded-f-king-cycles.syx`** next to your file. If anything does not match, it stops and
writes nothing.

## Flash it

Back up your projects and samples with Elektron Transfer first.

**Over USB (simplest):** open [Elektron Transfer](https://www.elektron.se/support-downloads/transfer), connect the
Cycles, and drop `modded-f-king-cycles.syx` on it, as for an official OS update.

**Over MIDI (startup menu):** connect a MIDI interface's MIDI OUT to the Cycles' MIDI IN. Power off, hold `FUNC`,
power on, release, press `TRIG 4` (OS UPGRADE): the screen shows READY TO RECEIVE. Send the `.syx` to the
interface's port with any SysEx tool. Do not switch off during the update (about 5–7 minutes); the Cycles restarts
by itself.

At power-on the new firmware shows a short logo animation and *modded f\*\*king cycles 1.0*.

## Going back to the official OS

Only the main OS is changed: the bootloader and the startup menu stay exactly as Elektron made them. To go back, send
Elektron's official `model-cycles_OS1.13.syx` the same way (Transfer, or the startup menu over MIDI IN). Before that,
set any track that uses an added machine (7–25) back to one of the six original machines, so your projects open
cleanly on the stock OS.

If the Cycles ever fails to start, the startup menu (`FUNC` + power on, then `TRIG 4`) over MIDI IN always brings
the official OS back.

## The machines

Press `MACHINES`: a list with a `>` on the selected machine and a counter. Turn `LEVEL/DATA` to move, **press**
`LEVEL/DATA` to load (so a bump of the encoder never changes your machine; set `Mch Clk` to OFF in
SETTINGS › Mods for the stock behaviour).

| # | Machine | What it is |
|---|---|---|
| 1–6 | KICK, SNARE, METAL, PERC, TONE, CHORD | Elektron's own machines, unchanged |
| 7 | 909BD | TR-909 kick from circuit models, tuned against real 909 recordings |
| 8 | 808BD | TR-808 kick from a circuit model, tuned against real 808 recordings |
| 9 | RUMBL | A rumble kick: kick into a dark feedback network, for long rolling low ends |
| 10 | 909 | The TR-909 kit: toms, rim, clap, hats, ride, crash and snare, picked per trig with SHAPE |
| 11 | 606 | The TR-606 kit: kick, snare, toms, hats and cymbal, picked per trig with SHAPE |
| 12 | 808 | The TR-808 hi-hat and cymbal |
| 13 | METL2 | Modal metal: bells, pipes, plates and bars |
| 14 | BIAS | Additive drum and drone voice with a wavefolder |
| 15 | ACID | TB-303 style bass with slides and Devil Fish style extras |
| 16 | GROWL | A dark growling bass |
| 17 | MACRO | Émilie Gillet's Braids, 47 models |
| 18 | VACF | Virtual-analog oscillator and filter voice |
| 19 | XMOD | Cross-modulation of two oscillators |
| 20 | DX | 6-operator FM with 32 sounds of its own |
| 21 | ATMOS | Dark chord pad, playable as a drone |
| 22 | SAMPLER | Plays your own sample |
| 23 | GRANULAR | Granular playback of your sample |
| 24 | WAVETABLE | Your sample as a wavetable |
| 25 | EMPTY | Silence at almost no CPU; a new project starts with EMPTY on all six tracks |

Every knob of every machine, with its defaults, is in **[docs/MACHINES.md](docs/MACHINES.md)**.

**Extras and the knob layer.** Added machines have up to four more parameters. **Double-tap `PUNCH`** (the LED
blinks) to turn on the knob layer, then turn `PITCH`, `DECAY`, `COLOR`, `SHAPE` for Extra 1–4; the other knobs play
the filter and insert effect (below). Double-tap `PUNCH` again to leave. Punch itself moves to a long press (half a
second). At 0 (the default) each machine plays its plain sound. Extras save with the sound and take p-locks. Holding
`MACHINES` and turning a knob does the same, and SETTINGS › Mods › `Knb Lyr` = MACH puts the layer on the open
MACHINES screen instead of `PUNCH`.

**Drum machine extras** follow the real machines: **Accent** (accented hits up to 6 dB louder, like the 909's TOTAL
ACCENT knob; plain hits unchanged) and **Drift** (each hit a little different in pitch, up to ±25 cents, and level,
about ±1 dB, like the parts of a real drum machine), plus tuning, decay and tone mods.

## What else changes on the Cycles

**Filter on every track.** On the knob layer (double-tap `PUNCH`), turn:

| Knob | Does |
|---|---|
| `LFO SPEED` | Filter Type: OFF / LP / HP / BP (12 dB) / LP24 / HP24 (24 dB) |
| `SWEEP` | Cutoff, 20 Hz–20 kHz |
| `CONTOUR` | Resonance |
| `DELAY` | Envelope amount (centre = none) |
| `REVERB` | Envelope decay, 5 ms–10 s |

**Insert effect on every track,** after the filter: on the knob layer, `SWING` picks OFF / DRV (drive) / CRS (bitcrush) /
FLD (wavefold) / RNG (ring mod) / RES (resonator) / RAT (RAT-pedal style distortion), `CHANCE` sets the
amount. Both save with the sound and take p-locks.

**Settings › Mods** (a new ninth row of SETTINGS):

| Row | Does |
|---|---|
| `USB Out` | 2CH (stereo mix, stock) or 6CH (each track on its own USB channel, to record six stems) |
| `SC Src`, `SC Amt`, `SC Atk`, `SC Hold`, `SC Rel`, `Duck T1–T6` | Sidechain: one track's trigs duck the others (kick pumps the bass). Saved with the project |
| `Trk Trg` | OFF: the track buttons only select the track; `TRK` + button plays it |
| `Mch Clk` | ON: press `LEVEL/DATA` to load a machine; OFF: turning loads, as stock |
| `Knb Lyr` | PUNCH double tap (default) or the MACHINES screen for the knob layer |
| `CPU Lim` | Machines that would push the CPU past this limit are greyed out in the list (70 % default; OFF) |

**Master DJ EQ.** Double-tap `GATE`: `PITCH` / `DECAY` / `COLOR` are LOW / MID / HIGH on the whole mix (centre =
0 dB, full left = kill, right = +6 dB). Double-tap again to leave. GATE itself moves to a long press. Saved with the
project.

**LFO polarity.** `FUNC` + `LFO`, last row `Pol`: BI (stock) or UNI (the LFO only adds to the knob's position).

**CPU monitor.** SETTINGS › Device › `CPU`: load and peak bars; turn `LEVEL/DATA` right for each track's share.

**Lighter on the CPU.** The send delay and reverb stop by themselves after about 11 s with nothing sent to them
(and start again on the very block a track sends them sound), and part of the OS runs from fast internal RAM.

**Screens.** The sound browser shows three names at once and scrolls long names; turning `LEVEL/DATA` shows the
track level (and pan, with `FUNC`) as numbers.

## Samples

SAMPLER, GRANULAR and WAVETABLE play your own WAV files.

1. Connect the Cycles over USB and set SETTINGS › Device › `Transfer` to **SMP**: Elektron Transfer now sees a
   Model:Samples. Drag your WAV files onto it. (Transfer goes back to CYC at every power-on. Never update the OS while
   it is on SMP.)
2. On the Cycles, choose SAMPLER, GRANULAR or WAVETABLE on a track, press `FUNC` + `MACHINES`, find the sample in the
   browser and load it.
3. Save the sound or the project: the track finds its sample again by name when the project opens.

One sample per track, up to about 71 seconds each. The three machines share the track's sample, so you can switch
between them.

## MIDI

Every parameter is reachable by MIDI (enable Receive CC/NRPN in SETTINGS › MIDI CONFIG › SYNC). Added: extras on CC
20–23, filter on CC 74 / 71 / 24 / 25 / 26, insert effect on CC 27 / 28, and NRPNs on the FX channel for USB Out,
the sidechain, LFO polarity, the master EQ, sample and project selection, and a "send me everything" request. The
complete list is [plugin/midi-map.json](plugin/midi-map.json); the stock Model:Cycles CCs are unchanged.

## The plug-in

`plugin/` is a JUCE plug-in (VST3, AU on Mac, and a standalone app) that talks to the Cycles over USB MIDI.

**Download** the build for your system from the latest [release](../../releases):

- **Mac** (`Modded_f-king_cycles-plugin-macOS.zip`, Apple Silicon and Intel): copy `VST3/Modded_f-king_cycles.vst3` to
  `~/Library/Audio/Plug-Ins/VST3/` and `AU/Modded_f-king_cycles.component` to `~/Library/Audio/Plug-Ins/Components/`.
  The build is **not signed by Apple**, so macOS blocks it at first. In Terminal, run once:

  ```sh
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Modded_f-king_cycles.vst3 \
      ~/Library/Audio/Plug-Ins/Components/Modded_f-king_cycles.component
  ```

  then restart your DAW (in Logic, rescan in the Plug-in Manager). For the standalone app, right-click it › Open the
  first time.
- **Windows** (`Modded_f-king_cycles-plugin-Windows.zip`): copy `VST3\Modded_f-king_cycles.vst3` to
  `C:\Program Files\Common Files\VST3\`.
- **Linux** (`Modded_f-king_cycles-plugin-Linux.zip`): copy `VST3/Modded_f-king_cycles.vst3` to `~/.vst3/`. Each zip also
  has the standalone app in `Standalone/`.

**Set up the Cycles:** USB MODE (SETTINGS › Device › USB) = Audio + MIDI; SETTINGS › MIDI CONFIG › SYNC: receive
CC/NRPN on, send CC/NRPN on; note the track channels and the FX channel and set the same ones on the plug-in's SETUP
page.

**Build it yourself:** CMake 3.22+ and a C++ compiler; JUCE is downloaded by CMake.

```sh
cd plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

(Linux needs the JUCE dependencies: `libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev
libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxi-dev libxinerama-dev libxrandr-dev libxrender-dev
libglu1-mesa-dev mesa-common-dev`.)

## How the firmware is built

`firmware/patch.py` runs `firmware/tools/build.py` with the list of mods in `firmware/tweaks/model-cycles_OS1.13/`.
Each mod is a table of byte changes, each carrying the original bytes it replaces, so the build refuses any file that
is not the official OS 1.13. The added machines are compiled code stored compressed in the mods. The MAIN OS SHA-256
of the result is checked before anything is written.

## Credits

Made by [m_reilly](https://soundcloud.com/m_reilly).

Built on the work of:

- **[18nelli18/Modded-Cycles](https://github.com/18nelli18/Modded-Cycles)** (MIT): the build toolchain, the
  per-track filter, the machine list, MACRO and ACID ports, and much of the reverse engineering.
- **[drumkilla/elektron-model-tweaks](https://github.com/drumkilla/elektron-model-tweaks)** (MIT): scrolling names
  and the `mtlib` firmware toolkit.
- **Marcel Bierling, [elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool)** (MIT): the
  firmware container format.
- **[scottmetoyer/ms-multi-output](https://github.com/scottmetoyer/ms-multi-output)** (MIT): the original 6-channel
  USB audio mod.
- **[TinyGregAudio/Model-TG](https://github.com/TinyGregAudio/Model-TG)** (MIT): the CPU measurement, the sample
  transfer identity and the delay/reverb tricks.
- **[DMDaudio/model909](https://github.com/DMDaudio/model909)** (MIT): the 909 kick, toms, rim and clap.
- **Émilie Gillet, [Mutable Instruments eurorack](https://github.com/pichenettes/eurorack)** (MIT): Braids (MACRO),
  and the designs behind METL2, VACF, XMOD and DX's algorithms.
- **Robin Schmidt, [Open303](https://github.com/RobinSchmidt/Open303)** (MIT): the model behind ACID.
- **[100demons/bias](https://github.com/100demons/bias)** (MIT): BIAS.
- **Matthew Fecher's** TR-606 analysis: the 606.
- **djd_oz**: volume and pan as numbers.
- **[JUCE](https://juce.com)** (AGPL-3.0 / commercial): the plug-in framework.

## Licenses

- Everything outside `plugin/` is **MIT** ([LICENSE](LICENSE)). The third-party licenses are in
  [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and next to the mods in `firmware/tweaks/model-cycles_OS1.13/`.
- `plugin/` is **AGPL-3.0** ([plugin/LICENSE](plugin/LICENSE)), as required by the open-source JUCE license.
- The tweak files contain short runs of the official OS bytes (the `old` values each change checks against your
  file) so the patcher can refuse the wrong file; they contain no Elektron firmware image.

Elektron, Model:Cycles, Model:Samples and Elektron Transfer are trademarks of Elektron Music Machines MAV AB. TR-606,
TR-808, TR-909 and TB-303 are trademarks of Roland Corporation. This project is not affiliated with or endorsed by
either company.
