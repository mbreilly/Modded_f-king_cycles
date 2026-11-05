# License scope and third-party notices

## What the licenses cover

Everything in this repository outside `plugin/` is under the MIT License ([LICENSE](LICENSE)): the patcher, the
tweak files and the compiled code in them, and the documentation. `plugin/` is under the GNU Affero General Public
License v3.0 ([plugin/LICENSE](plugin/LICENSE)), because it is built with the open-source edition of JUCE. A file or
folder that names another license is under that license instead (table below).

## What they do not cover

- **Elektron firmware.** No firmware image is included in this repository or its releases. You supply your own
  official OS file from elektron.se, and a firmware built from it contains Elektron's software: it is for your own
  use on your own machine. Do not redistribute built `.syx` files.
- **Bytes copied from Elektron firmware** that appear in the tweak files: the `old` bytes used to check your file,
  relocation values and short fragments inside patches. They remain Elektron's and are included only so the patcher
  can check and patch your own file.
- **Trademarks.** Elektron, Model:Cycles, Model:Samples and Elektron Transfer are trademarks of Elektron Music
  Machines MAV AB. TR-606, TR-808, TR-909 and TB-303 are trademarks of Roland Corporation. This project is
  independent, not affiliated with or endorsed by either company.

Modifying firmware is at your own risk. The software is provided "as is", without warranty, as the licenses say.

## Third-party code

| Component | Where it is used here | Author | License | License text |
|---|---|---|---|---|
| [Modded-Cycles](https://github.com/18nelli18/Modded-Cycles) | the build toolchain, the per-track filter and insert effects, the machine list and screens, most mods | 18nelli18 and contributors | MIT | [LICENSE](LICENSE) |
| [elektron-model-tweaks](https://github.com/drumkilla/elektron-model-tweaks) | `firmware/tools/mtlib/` (unchanged); the scrolling-names tweak (`03-browser-scroll.json`) | drumkilla | MIT | [mtlib/LICENSE](firmware/tools/mtlib/LICENSE), [LICENSE-elektron-model-tweaks](firmware/tweaks/model-cycles_OS1.13/LICENSE-elektron-model-tweaks) |
| [elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool) | the basis of mtlib's container, codec and signature code | Marcel Bierling | MIT | [LICENSE-elektron-firmware-tool](firmware/tools/mtlib/LICENSE-elektron-firmware-tool) |
| [ms-multi-output](https://github.com/scottmetoyer/ms-multi-output) | the 6-channel USB audio of `12-mod-settings.json` | Scott Metoyer | MIT | [LICENSE-ms-multi-output](firmware/tweaks/model-cycles_OS1.13/LICENSE-ms-multi-output) |
| [Model-TG](https://github.com/TinyGregAudio/Model-TG) | the CPU monitor (`61-cpu-monitor.json`); in the machines tweak (`24-rel-…json`): the Transfer identity (CYC / SMP), the sample browser loading, the delay/reverb stop and early delay read | TinyGregAudio | MIT | [LICENSE-Model-TG](firmware/tweaks/model-cycles_OS1.13/LICENSE-Model-TG) |
| [eurorack](https://github.com/pichenettes/eurorack) (Braids) | MACRO: Braids' macro oscillator compiled unchanged | Émilie Gillet | MIT | [LICENSE-Braids](firmware/tweaks/model-cycles_OS1.13/LICENSE-Braids) |
| [eurorack](https://github.com/pichenettes/eurorack) (Plaits) | DX: the table of the 32 DX7 algorithms in Plaits' encoding. METL2, VACF and XMOD are written from scratch after ideas of Rings, Plaits and Warps | Émilie Gillet | MIT | [LICENSE-Plaits](firmware/tweaks/model-cycles_OS1.13/LICENSE-Plaits) |
| [Open303](https://github.com/RobinSchmidt/Open303) | ACID: the structure and measured constants of its TB-303 model, rewritten in fixed point | Robin Schmidt | MIT | [LICENSE-Open303](firmware/tweaks/model-cycles_OS1.13/LICENSE-Open303) |
| [model909](https://github.com/DMDaudio/model909) | 909BD and the toms, rim, clap and snare of 909 | DMDaudio | MIT | [LICENSE-model909](firmware/tweaks/model-cycles_OS1.13/LICENSE-model909) |
| [bias](https://github.com/100demons/bias) | BIAS (version 2.0), with added extras and CPU changes; a clean-room voice, no Noise Engineering code | 100demons | MIT | [LICENSE-bias](firmware/tweaks/model-cycles_OS1.13/LICENSE-bias) |
| [JUCE](https://github.com/juce-framework/JUCE) | the plug-in framework, downloaded by CMake at build time | Raw Material Software | AGPL-3.0 (or commercial) | [plugin/LICENSE](plugin/LICENSE) |

The 808BD, 808, 606, RUMBL, GROWL, ATMOS, SAMPLER, GRANULAR, WAVETABLE and EMPTY machines, the sidechain, master EQ,
LFO polarity, machine selection, CPU budget and the plug-in are original work of this project. The 606 follows
Matthew Fecher's published TR-606 circuit analysis (no code).

## Contributed mods

- **Volume and pan as numbers** (`46-level-pan-values.json`): rewritten from mods by djd_oz.
- **Multi-line browser** (`44-multiline-browser.json`): written from the idea and display choices of a community
  member's script; none of its code is used.
