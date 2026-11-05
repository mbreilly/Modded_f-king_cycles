#!/usr/bin/env python3
"""Modded_f-king_cycles: builds the modded Model:Cycles firmware from YOUR official OS 1.13 file.

    python3 patch.py model-cycles_OS1.13.syx [-o modded-f-king-cycles.syx]

Download model-cycles_OS1.13.syx from elektron.se (Model:Cycles > Support > Downloads). The script checks that it is
the official file, applies the mods below, checks the result against the expected hash and writes the new .syx next
to it. Needs Python 3.8 or later, nothing else.
"""
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
MODS = ['rel-macro-acid-t9-t8-606-909-smp-gran-wave-rumbl-vacf-metl2-xmod-dx-growl-atmos-empty-bias', 'browser-scroll', 'boot-anim-mfc', 'multiline-browser', 'level-pan-values', 'mod-settings', 'cpu-monitor', 'track-filter-soft', 'filter-no-poli', 'usb-recover', 'lfo-polarity', 'sidechain', 'track-select', 'sidechain-project', 'machine-click', 'midi-dump']
EXPECT_MAIN_OS = "5d99eb283b1b3fe225e75bbb3c643badfe382d77d312b505476019ccc408039e"


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0 if args else 1
    src = pathlib.Path(args[0]).expanduser()
    out = pathlib.Path(args[2]).expanduser() if len(args) > 2 and args[1] == "-o" else \
        src.with_name("modded-f-king-cycles.syx")
    if not src.is_file():
        print(f"File not found: {src}")
        return 1
    r = subprocess.run([sys.executable, str(HERE / "tools" / "build.py"), "-i", str(src), "-t", ",".join(MODS),
                        "--expect-mainos", EXPECT_MAIN_OS, "-o", str(out)])
    if r.returncode:
        print("\nThe build stopped (see above). Is this the official model-cycles_OS1.13.syx from elektron.se?")
        return r.returncode
    print(f"\nDone: {out}\nSend it to the Model:Cycles with Elektron Transfer (see README.md, 'Flash it').")
    return 0


if __name__ == "__main__":
    sys.exit(main())
