# ClawDOS

DOS software for a real IBM PC 5160 (8088 @ 4.77 MHz, 640K, CGA, PicoMEM: disk images + NE2000 over WiFi).
README.md has the quick start; docs/ROADMAP.md the plan; docs/API.md what CLAWDOS sends to the API and expects back.

## Workflow
- Setup once: `python3 scripts/setup-tools.py` (Homebrew on macOS, winget on Windows). Toolchains live in `tools/`
  (gitignored), outputs in `build/`. The Python (tunnel/) is stdlib only: no venv.
- macOS: the commands below say `python`; use `python3`. Watcom runs natively (tools/openwatcom/armo64).
- The Makefile wraps the commands below (`make` lists targets): `make build`, `make test` (mock API unit tests),
  `make boottests` (every DOSBox-X test), `make check` (both), `make run`, `make tunnel`, `make mock`. Keep it in step with them.
- Build everything: `python scripts/build.py [--tunnel host:port]` (apps -> build/*.EXE, disk -> build/CLAWDOS.IMG).
  The image's C:\CLAWDOS\CLAWDOS.CFG gets the tunnel address (remembered in build/settings.json) and the API key from
  `.env` (`ANTHROPIC_API_KEY=...`, gitignored). Add DOS programs to `APPS` in scripts/build.py and to image/manifest.txt.
- Code navigation: the clangd-lsp and pyright-lsp plugins are enabled for this project (.claude/settings.json;
  clangd and pyright-langserver must be on PATH). clangd reads build/compile_commands.json, written by every build
  or by `python scripts/build.py --compiledb`; scripts/clangd/ holds the shims that let clang parse Watcom code.
- Headless boot test (DOSBox-X): `python scripts/boottest.py [image/tests/x.bat]`. Test batch files write results to COM1
  and end with `TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1` so CLAWDOS's own notes (clock, retries, lost links, errors) reach the log;
  `image/tests/net.bat` checks networking. `--mock` runs the mock Messages API (tunnel/mockapi.py) alongside on port 18023, so
  tests pass `10.0.2.2:18023` to CLAWDOS (plain HTTP; log in build/mock.log) and `--expect TEXT` fails unless TEXT is in the
  serial or mock log. The Makefile's `boottests` target lists them all: `tools.bat --mock --expect "tool tour: PASS"` runs
  every DOS tool; `drop.bat` a lost link (`connection #2`); `memory.bat` memory tools never asking; `clock.bat` the clock
  from the Date header (`Clock set from the API`); `retry.bat` a 529 (`trying again`); `chunks.bat` awkward chunk
  boundaries; `bigevent.bat` an event over the 16 KB buffer (`event too long`); `bigsig.bat` a 24 KB thinking signature streamed into the history
  and echoed back whole (`big signature round trip: PASS`); `relaunch.bat --timeout 150` the run
  tool through CLAWDOS.EXE with the clock back at 1980 (`Largest executable program size` from MEM, `CLAWDOS.EXE exit code 0`
  for the resume, then the tour and `resume state consumed`); `units.bat --expect UNITS-OK` runs tests/unit/dosunit.c
  (every lib/ parser). Interactive DOSBox-X with networking: `python scripts/rundosbox.py`
  (cputype 80186: the Crynwr NE2000 driver garbles frames on an emulated 8086, see boottest.py).
- Real 8088 check: `python scripts/test8088.py image/tests/devtools.bat` boots a test batch in 86Box (DOSBox-X runs
  as an 80186 and can't catch 186/286 code) and prints the log it leaves in C:\ (`--log`, `--done` marker).
- Accurate 8088 PC: `python scripts/run86box.py`, then `python scripts/shot.py [--delay N]` and read build/shot.png
  (macOS: the terminal needs Screen Recording permission; 86Box ignores SIGTERM, quit it with
  `osascript -e 'tell application id "net.86Box.86Box" to quit'`).
  `scripts/sendkeys.ps1` (Windows only) can't type unless 86Box can take focus; prefer `--autotest` batch files and app flags like
  `CLAWDOS -m msg` (send) and `-i text` (pre-fill the input box). DOS command lines max out at 127 chars.
- CLAWDOS.EXE (src/clawdos) is the small launcher the user types: it runs HARNESS.EXE with `-launched`, and when a
  `run` tool call makes the harness exit with code 200 it runs the commands itself (lib/shell.c) with the harness's
  memory free, then starts `HARNESS -resume -launched`, which reopens the conversation and carries the turn on. The
  files between them are in lib/relaunch.h; RESUME.TXT stays until the resume has connected, so `CLAWDOS -resume`
  retries one that failed. `HARNESS.EXE` run on its own keeps programs inside its memory (the boot tests do that).
  Two relaunch lessons: a program started after the harness has set the clock meets mTCP's DHCP lease check (DHCP
  stamped the lease with the boot clock; lib/mtcpcfg.c moves the stamp with the clock), and a fresh process must not
  reuse the local TCP port its predecessor just closed (lib/net.cpp takes it from the BIOS time of day, not mTCP's
  tick count). mTCP's "End: heap is corrupted!" at exit is a false alarm: Open Watcom's `_fheapchk` flags any far
  heap in this build (DOSUNIT shows it after one malloc).
- HARNESS.EXE (src/harness) is the whole harness: it speaks the Messages API itself (src/harness/chat.cpp drives the turn; lib/apireq.c
  writes the request from C:\CLAWDOS\PROMPT.TXT, TOOLS.JSN and the history file; lib/apistream.c turns the SSE stream into
  screen text, history and tool calls). Only TLS is off the PC: `python3 tunnel/tunnel.py` copies bytes between a plain port
  (8443) and api.anthropic.com:443, with no knowledge of the API (socat/stunnel work too). Conversations live in
  C:\CLAWDOS\CHATS\*.JSN in the API's own message format, notes and per-round usage in C:\CLAWDOS\CLAWDOS.LOG.
  `python3 tunnel/mockapi.py` fakes the API offline on plain HTTP (point CLAWDOS at it: `CLAWDOS 10.0.2.2:18023`); in a chat,
  `!help` lists its test scenarios (`!scroll`, `!wide`, `!cp437`, `!error`, `!drop`, `!retry`, ...); `!tool NAME k=v` calls one
  DOS tool, `!tooltour` all; tests `python3 tunnel/test_mockapi.py`. Tool schemas: image/clawdos/TOOLS.JSN (the one source;
  the mock reads it too); executors dispatched by src/harness/tools.cpp. CLAWDOS `-unsafe` skips the y/n prompts, `-q` quits
  after the `-m` reply, `-p NAME` picks a provider section, `-noscroll` turns off hardware scrolling.
- ISRs (e.g. lib/shell.c's INT 10h hook) run on foreign stacks: don't call our own functions from them, and make
  the state they change `volatile`. The status row's spinner and seconds run from INT 1Ch (lib/screen.c scr_ticker),
  so they move while the harness is busy; mTCP hooks INT 1Ch too, so scr_ticker_off() before Net::stopStack().
- Dev tools on the image (C:\DEVEL, C:\TC; Turbo C comes from the user's own floppies in external/, never commit
  them): see docs/research/ and image/memory/DEVTOOLS.TXT, the memory that tells Claude about them.
- Disk images: `python scripts/fatimg.py ls|tree|get|put IMG ...`. In Git Bash, don't start image paths with `/` (MSYS rewrites them).
- Commit to main freely as checkpoints.

## Constraints
- 8086 instruction set only (`wcc/wpp -0`, NASM `cpu 8086`).
- Screen output goes through lib/screen (retrace-synced writes avoid CGA snow; hardware scroll via CRTC start address).
  The real IBM CGA does NOT wrap its display at the end of its 16 KB (86Box's does): the window must stay inside it.
  A pinned footer (scr_footer) is rewritten during vertical blanking on every scroll; keep it small (each row costs ~0.3 ms).
- Code linking mTCP (lib/net, lib/http, HARNESS.EXE) is GPLv3.
- Watcom: no quoted `-D` values (use the generated force-include), and no `{}` file lists for wlink (use a .lnk file).
