# Claude-XT

DOS software for a real IBM PC XT (8088 @ 4.77 MHz, 640K, CGA, PicoMEM: disk images + NE2000 over WiFi).
README.md has the quick start; docs/ROADMAP.md the plan; docs/PROTOCOL.md the bridge protocol.

## Workflow
- Setup once: `.\scripts\setup-tools.ps1`. Toolchains live in `tools/` (gitignored), outputs in `build/`.
- Build everything: `python scripts/build.py` (apps -> build/*.EXE, disk -> build/CLAUDEXT.IMG).
  Add DOS programs to `APPS` in scripts/build.py and to image/manifest.txt.
- Headless boot test (DOSBox-X): `python scripts/boottest.py [image/tests/x.bat]`. Test batch files write results to COM1.
- Accurate XT: `python scripts/run86box.py`, then `.\scripts\winshot.ps1 [-Delay N]` and read build\shot.png.
  `scripts/sendkeys.ps1` can't type unless 86Box can take focus; prefer `--autotest` batch files and app flags like `CLAUDE -m`.
- Bridge: `python bridge/xtbridge.py [--echo]`; test client `python bridge/chat.py`.
- Disk images: `python scripts/fatimg.py ls|tree|get|put IMG ...`. In Git Bash, don't start image paths with `/` (MSYS rewrites them).
- Commit to main freely as checkpoints.

## Constraints
- 8086 instruction set only (`wcc/wpp -0`, NASM `cpu 8086`).
- Screen output goes through lib/screen (retrace-synced writes avoid CGA snow; hardware scroll via CRTC start address).
- Code linking mTCP (lib/xtbridge, CLAUDE.EXE) is GPLv3.
- Watcom on Windows: no quoted `-D` values (use the generated force-include), and no `{}` file lists for wlink (use a .lnk file).
