# ClawDOS

DOS software for a real IBM PC XT (8088 @ 4.77 MHz, 640K, CGA, PicoMEM: disk images + NE2000 over WiFi).
README.md has the quick start; docs/ROADMAP.md the plan; docs/PROTOCOL.md the bridge protocol.

## Workflow
- Setup once: `.\scripts\setup-tools.ps1`. Toolchains live in `tools/` (gitignored), outputs in `build/`.
- Build everything: `python scripts/build.py` (apps -> build/*.EXE, disk -> build/CLAWDOS.IMG).
  Add DOS programs to `APPS` in scripts/build.py and to image/manifest.txt.
- Code navigation: the clangd-lsp and pyright-lsp plugins are enabled for this project (.claude/settings.json;
  clangd and pyright-langserver must be on PATH). clangd reads build/compile_commands.json, written by every build
  or by `python scripts/build.py --compiledb`; scripts/clangd/ holds the shims that let clang parse Watcom code.
- Headless boot test (DOSBox-X): `python scripts/boottest.py [image/tests/x.bat]`. Test batch files write results to COM1;
  `image/tests/net.bat` checks networking. `--bridge` runs the mock bridge alongside on port 18023, so tests use `10.0.2.2:18023` (log in build/bridge.log) and
  `--expect TEXT` fails unless TEXT is in the serial or bridge log: `image/tests/tools.bat --bridge --expect
  "tool tour: PASS"` runs every XT tool; `image/tests/drop.bat --bridge --expect "connection #2"` checks a lost link; `image/tests/memory.bat --bridge --expect "memory_write -> ok"` checks memory tools never ask; `image/tests/clock.bat --bridge --expect "Clock set from the bridge"` checks the clock from the HELLO; `image/tests/alias.bat --bridge --expect "exit code 0"` checks that the old CLAUDE name (src/alias/claude.asm -> CLAUDE.COM) still starts CLAWDOS.EXE; `image/tests/units.bat --expect UNITS-OK` runs tests/unit/cxtunit.c. Interactive DOSBox-X with networking: `python scripts/rundosbox.py`
  (cputype 80186: the Crynwr NE2000 driver garbles frames on an emulated 8086, see boottest.py).
- Real 8088 check: `python scripts/xttest.py image/tests/devtools.bat` boots a test batch in 86Box (DOSBox-X runs
  as an 80186 and can't catch 186/286 code) and prints the log it leaves in C:\ (`--log`, `--done` marker).
- Accurate XT: `python scripts/run86box.py`, then `.\scripts\winshot.ps1 [-Delay N]` and read build\shot.png.
  `scripts/sendkeys.ps1` can't type unless 86Box can take focus; prefer `--autotest` batch files and app flags like
  `CLAWDOS -m msg` (send) and `-i text` (pre-fill the input box). DOS command lines max out at 127 chars.
- Bridge: `python bridge/xtbridge.py [--mock]` (debug log with a wire trace in bridge/xtbridge.log, conversations as JSONL in bridge/logs/; `--prompt NAME`
  makes bridge/prompts/NAME.txt the system prompt, with the tools add-on appended); test client `python bridge/chat.py`; tests `python bridge/test_bridge.py`.
  `--mock` (bridge/mockchat.py) fakes Claude offline; in a chat, `!help` lists its test scenarios (`!scroll`, `!wide`, `!cp437`, `!error`, `!drop`, ...).
  Tools: schemas in bridge/tools.py, executors dispatched by src/clawdos/tools.cpp; mock `!tool NAME k=v` calls one,
  `!tooltour` all. CLAWDOS `-unsafe` skips the y/n prompts, `-q` quits after the `-m` reply.
  `--replay LOG` plays a logged session back to the XT (bridge/replay.py); CLAWDOS `-noscroll` turns off
  hardware scrolling.
- ISRs (e.g. lib/shell.c's INT 10h hook) run on foreign stacks: don't call our own functions from them, and make
  the state they change `volatile`.
- Dev tools on the image (C:\DEVEL, C:\TC; Turbo C comes from the user's own floppies in external/, never commit
  them): see docs/research/ and image/memory/DEVTOOLS.TXT, the memory that tells Claude about them.
- Disk images: `python scripts/fatimg.py ls|tree|get|put IMG ...`. In Git Bash, don't start image paths with `/` (MSYS rewrites them).
- Commit to main freely as checkpoints.

## Constraints
- 8086 instruction set only (`wcc/wpp -0`, NASM `cpu 8086`).
- Screen output goes through lib/screen (retrace-synced writes avoid CGA snow; hardware scroll via CRTC start address).
  The real IBM CGA does NOT wrap its display at the end of its 16 KB (86Box's does): the window must stay inside it.
  A pinned footer (scr_footer) is rewritten during vertical blanking on every scroll; keep it small (each row costs ~0.3 ms).
- Code linking mTCP (lib/xtbridge, CLAWDOS.EXE) is GPLv3.
- Watcom on Windows: no quoted `-D` values (use the generated force-include), and no `{}` file lists for wlink (use a .lnk file).
