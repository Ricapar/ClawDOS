# ClawDOS

DOS software for a real IBM PC 5160 (8088 @ 4.77 MHz, 640K, CGA, PicoMEM: disk images + NE2000 over WiFi).
README.md has the quick start; docs/ROADMAP.md the plan; docs/API.md what CLAWDOS sends to the API and expects back.

## Workflow
- Setup once: `python3 scripts/setup-tools.py` (Homebrew on macOS, winget on Windows). Toolchains live in `tools/`
  (gitignored), outputs in `build/`. The Python (tunnel/) is stdlib only: no venv.
- macOS: the commands below say `python`; use `python3`. Watcom runs natively (tools/openwatcom/armo64).
- The Makefile wraps the commands below (`make` lists targets): `make build`, `make test` (host unit tests: the mock API, the tunnel, the image updater, bench.py's table),
  `make boottests` (the DOSBox-X tests that run offline), `make boottests-online` (those that need the internet:
  clock.bat), `make check` (test and boottests, all offline), `make bench` (the 86Box benchmark), `make run`,
  `make tunnel`, `make mock`. Keep it in step with them.
- Update a real PC's image in place instead of replacing it: `python scripts/build.py --update PATH/CLAWDOS.IMG [--dry-run]
  [--memories] [--cfg] [--no-backup]` (`make update IMG=...`; scripts/imgupdate.py, tests scripts/test_imgupdate.py). Unless nothing would change, it copies
  the old image to IMG.bak (never overwritten: .bak.2, .bak.3, ...), then replaces/adds every file the fresh build ships (derived from the manifest, no directory list) and
  nothing else (SKILLS/ is the build's, replaced like any shipped file): MEMORY/ (`--memories`: only the memory files the build ships, none now; a memory named like a shipped skill, left by an older build, gets a note saying it can go with F4; a skill in SKILLS/ the build no longer ships gets a note too, and stays), CLAWDOS/CHATS/, CLAWDOS.LOG, CLAWDOS.CFG
  (`--cfg`), NET/MTCP.CFG (the DHCP lease), RUN.TXT/RUN.OUT/RESUME.TXT/SCREEN.SAV/SHOT.RAW/SHOT.PNG and files the build does not ship stay. Never test it on the real SD-card image.
- The API key can stay off the DOS PC: `build.py --no-key` (remembered, `--key` undoes it; image says `auth tunnel`, no key) with `python3 tunnel/tunnel.py
  --key-file PATH --allow ADDR`, which adds `x-api-key` to each request for api.anthropic.com (opt-in; the default tunnel still
  copies bytes and knows nothing of HTTP; docs/API.md "The tunnel"). Tests: `python3 tunnel/test_tunnel.py` against a local
  stand-in upstream, never the internet. `mockapi.py --listen` defaults to 127.0.0.1 (the emulators reach it as 10.0.2.2).
- Build everything: `python scripts/build.py [--tunnel host:port]` (apps -> build/*.EXE, disk -> build/CLAWDOS.IMG).
  The image's C:\CLAWDOS\CLAWDOS.CFG gets the tunnel address (remembered in build/settings.json) and the API key from
  `.env` (`ANTHROPIC_API_KEY=...`, gitignored); the test images boottest.py and test8088.py build get no key (the
  mock needs none). TOOLS.JSN goes onto the image minified; the repo's copy stays readable. Add DOS programs to `APPS` in scripts/build.py and to image/manifest.txt.
- Code navigation: the clangd-lsp and pyright-lsp plugins are enabled for this project (.claude/settings.json;
  clangd and pyright-langserver must be on PATH). clangd reads build/compile_commands.json, written by every build
  or by `python scripts/build.py --compiledb`; scripts/clangd/ holds the shims that let clang parse Watcom code.
- Headless boot test (DOSBox-X): `python scripts/boottest.py [image/tests/x.bat]`. Test batch files write results to COM1
  and end with `TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1` so CLAWDOS's own notes (retries, lost links, errors) reach the log;
  `image/tests/net.bat` checks networking. `--mock` runs the mock Messages API (tunnel/mockapi.py) alongside on port 18023, so
  tests pass `10.0.2.2:18023` to CLAWDOS (plain HTTP; log in build/mock.log; the test fails if that mock cannot start,
  say the port is taken (the message names the holder), or dies during the run). A boot test (and test8088.py) holds
  build/boottest.lock while it runs, so one started meanwhile waits instead of sharing build/ and the port.
  `--expect TEXT` fails unless TEXT is in the
  serial or mock log and `--reject TEXT` fails if it is. The mock checks every request against its own list of rules
  (docs/API.md, "The tunnel": headers, fields, the thinking object and its beta headers, cache markers agreeing on their
  ttl, tools equal to TOOLS.JSN, message structure, tool pairing, thinking blocks returned byte-identical, the message
  thread rules) and answers a violation with a 400 and a `VALIDATION:` line in its log, so every mock test rejects
  that text; a task that adds a request field adds its rule to tunnel/mockapi.py. The Makefile's `boottests` target lists the offline ones: `tools.bat --mock --expect "tool tour: PASS"` runs
  every DOS tool (its stdin step runs C:\TESTS\STDINEOF.COM, from tests/unit/stdineof.asm, started directly: it must
  read end-of-file from the NUL the run tool gives it; a second run starts C:\TESTS\STDINEOF.BAT, so COMMAND.COM is its
  parent: `STDINEOF under COMMAND.COM: ok`; and `progress note:` lines show the mock's progress notes reached the screen; `tool tour threads: PASS`
  that every request after the first continued a message thread, each small); `notes.bat` a progress note's blank
  lines trimmed and its break joined (`progress note: Two lines`), and a tool header after reply text starting in
  column 0 (`!headercol` peeks CGA memory: `tool header column: 0 (PASS)`); `threads.bat --timeout 300` a thread lost, a
  continue whose link drops (resent; with `burn` refused, then replayed), `thread_unsupported_request`, a refused
  replay ending the turn, a 529 or an overload answering a continue (the same continue again), a plain 404 or 400
  (the turn ends), and a relaunch that finds its thread gone (`thread replay after CODE: PASS` lines, and each run's
  `exit code`);
  `fixed.bat` a conversation keeping its thinking and cache settings across a relaunch whose run changed CLAWDOS.CFG
  (`thinking none, cache 1h` for the next one); `drop.bat` a lost link (`connection #2`); `memory.bat` memory tools never asking; `skills.bat`
  the system prompt listing C:\SKILLS (not as memories) and a skill read in two parts with `read` (`skills: PASS`); `clock.bat` (in
  `boottests-online`) NETUP's NTP clock from a 1980 boot (`System time set`; needs the internet); `retry.bat` a 529
  (`trying again`); `chats.bat --timeout 240` the chat list and reopening (`tail screen: PASS`: `!tailscreen` peeks
  CGA memory for the reopened transcript; `-k` types keys into the BIOS buffer to drive the list, whose rows go to
  CLAWDOS.LOG as `chat list:` lines); `chatends.bat` chats the PC left mid-turn, reopened with `-c` and continued
  (`reopened unans: ...` from the mock's `!reopened`); `shot.bat --timeout 400` the run tool's screenshots of
  C:\TESTS\QUADS.COM (tests/unit/quads.asm: mode 4 quadrants and a diagonal; QUADT.COM sets text mode again, so only
  `shot_at` sees it; QUAD6.COM is mode 6, TEXT40.COM a 40-column text screen) in the harness and through the launcher (the mock checks each PNG, saves it as
  build/mockshot-N.png and logs `image ok 320x200 (the call): palette ..., pixels CRC`; open the files to look), a
  text screen, `shot_mode`, `shot_color`, bad arguments refused, the first image ending its thread, and `!shotmem`
  (MEM in the harness before and after a launcher screenshot); `overload.bat` an `overloaded_error` event before any text, retried like a 529; `streamerr.bat` stream
  errors that are not retried (after text, or another type); `chunks.bat` awkward chunk
  boundaries; `bigevent.bat` an event over the 16 KB buffer (`event too long`); `bigsig.bat` a 24 KB thinking signature streamed into the history
  and echoed back whole (`big signature round trip: PASS`); `loop.bat` the 25-round cap (`!loop` calls a tool until a request says
  `tool_choice` none); `think.bat` thinking_delta text in the history, and `!ping` events; `thinktool.bat` thinking text with a quote, a backslash and accents sent back whole; `redacted.bat` a redacted_thinking block
  echoed back (`redacted thinking round trip: PASS`); `busy.bat` three 529s so the retries run out; `ends.bat` a refusal and a
  max_tokens cutoff; `relaunch.bat --timeout 150` the run
  tool through CLAWDOS.EXE (`!bigrun` sets full_memory; `plain run stayed in the harness` for one without) (`Largest executable program size` from MEM, `CLAWDOS.EXE exit code 0`
  for the resume, whose request continues the thread: `bigrun: the result came by thread continue`, then the tour and
  `resume state consumed`); `runrefuse.bat --timeout 120` a refusal on the round after a
  relaunch, which takes the whole turn with it (the resume carries the turn's start in RESUME.TXT); `units.bat --expect UNITS-OK` runs tests/unit/dosunit.c
  (every lib/ parser; tests/unit/shotunit.c the screenshot PNGs against tests/unit/pngref.py's, which the build puts in C:\TESTS, and the file tools on files it makes and removes; it is built with `DOSUNIT` defined,
  which turns on test seams such as lib/history.c's failing scan and full disk). Interactive DOSBox-X with networking: `python scripts/rundosbox.py`
  (cputype 80186: the Crynwr NE2000 driver garbles frames on an emulated 8086, see boottest.py).
- Real 8088 check: `python scripts/test8088.py image/tests/devtools.bat` boots a test batch in 86Box (DOSBox-X runs
  as an 80186 and can't catch 186/286 code) and prints the log it leaves in C:\ (`--log`, `--done` marker;
  `--mock` and `--expect` as in boottest.py, the guest reaching the mock at 10.0.2.2:18023). The screenshot timer hook
  on the real CPU: `python3 scripts/test8088.py image/tests/shot8088.bat --mock --timeout 1500 --expect "pixels B5BCA4D2"`
  (at exit, `shot_at` in the harness and through the launcher; DOSUNIT prints how long the PNG took).
- Benchmark: `python scripts/bench.py` (`make bench`, about six minutes) runs image/tests/bench.bat in 86Box: the mock's
  `!bench` (20 reads of 4 KB, a `full_memory` run, a 4 KB reply) through CLAWDOS.EXE, then a table from CLAWDOS.LOG's
  `api` lines (`up=` upload ms, `len=` body bytes), the relaunch and the `stack:` high-water mark. docs/ROADMAP.md's
  "Benchmark" holds the baseline; keep the scenario and format stable. `--dosbox` is a quick, not comparable, run.
- Accurate 8088 PC: `python scripts/run86box.py`, then `python scripts/shot.py [--delay N]` and read build/shot.png
  (macOS: the terminal needs Screen Recording permission; 86Box ignores SIGTERM, quit it with
  `osascript -e 'tell application id "net.86Box.86Box" to quit'`).
  `scripts/sendkeys.ps1` (Windows only) can't type unless 86Box can take focus; prefer `--autotest` batch files and app flags like
  `CLAWDOS -m msg` (send), `-i text` (pre-fill the input box), `-c NAME` (reopen a chat) and `-k KEYS` (typed ahead:
  `~e` Enter, `~x` Esc, `~d` Del, `~u`/`~n` Up/Down, `~q` F10; 15 at most). DOS command lines max out at 127 chars.
- CLAWDOS.EXE (src/clawdos) is the small launcher the user types: it runs HARNESS.EXE with `-launched`, and when a
  `run` tool call with `full_memory` makes the harness exit with code 200 it runs the commands itself (lib/shell.c) with the harness's
  memory free, then starts `HARNESS -resume -launched`, which reopens the conversation and carries the turn on. The
  files between them are in lib/relaunch.h (a screenshot's capture goes in SHOT.RAW, lib/shot.h: the launcher only
  copies the screen, the resumed harness makes the PNG); RESUME.TXT stays until the resume has connected, so `CLAWDOS -resume`
  retries one that failed. `HARNESS.EXE` run on its own keeps programs inside its memory (the boot tests do that).
  A relaunch lesson: a fresh process must not
  reuse the local TCP port its predecessor just closed (lib/net.cpp takes it from the BIOS time of day, not mTCP's
  tick count). mTCP's "End: heap is corrupted!" at exit is a false alarm: Open Watcom's `_fheapchk` flags any far
  heap in this build (DOSUNIT shows it after one malloc).
- HARNESS.EXE (src/harness) is the whole harness: it speaks the Messages API itself (src/harness/chat.cpp drives the turn;
  lib/apireq.c renders PROMPT.TXT (with the memories and the skills) and TOOLS.JSN once per conversation into CHATS\*.PRE, the frozen
  `system` and `tools` a conversation's creating requests send, and writes each request from that and the history file
  (with `threads 1`, a continue request sends only the history since the last reply: docs/API.md "Threads"), whose writes
  lib/history.c buffers; lib/apistream.c turns the SSE stream into screen text, history and tool calls). Only TLS is off the PC: `python3 tunnel/tunnel.py` copies bytes between a plain port
  (8443) and api.anthropic.com:443, with no knowledge of the API (socat/stunnel work too). Conversations live in
  C:\CLAWDOS\CHATS\*.JSN in the API's own message format, notes and per-round usage in C:\CLAWDOS\CLAWDOS.LOG.
  F6 or `/chats` (src/harness/chatui.cpp) lists them; reopening one (also `-c NAME`) goes through
  Chat::openConversation and lib/chatfile.c, which reads the file a piece at a time (never whole: a chat can be
  200 KB) to draw its tail and to find what it ends with (docs/API.md, "Reopening a chat").
  `python3 tunnel/mockapi.py` fakes the API offline on plain HTTP (point CLAWDOS at it: `CLAWDOS 10.0.2.2:18023`); in a chat,
  `!help` lists its test scenarios (`!scroll`, `!wide`, `!cp437`, `!error`, `!drop`, `!retry`, `!overload`, `!threadlost`, ...); `!tool NAME k=v` calls one
  DOS tool, `!tooltour` all; tests `python3 tunnel/test_mockapi.py`. Tool schemas: image/clawdos/TOOLS.JSN (the one source;
  the mock reads it too); executors dispatched by src/harness/tools.cpp. CLAWDOS `-unsafe` skips the y/n prompts, `-q` quits
  after the `-m` reply, `-p NAME` picks a provider section.
- The clock is the boot scripts' job, not the harness's: AUTOEXEC.BAT loads ASTCLOCK.COM /R (the PicoMEM 2's
  battery clock), NETUP.BAT sets the time by SNTP (log in C:\NET\SNTP.LOG) and, after a big jump, takes a fresh DHCP
  lease (mTCP refuses to run on a lease stamped with the old clock). DOSBox-X's DNS fails when the host's first
  nameserver is IPv6, hence NETUP's fallback NTP address.
- ISRs (e.g. lib/shell.c's INT 10h hook) run on foreign stacks: don't call our own functions from them, and make
  the state they change `volatile`. The status row's spinner and seconds run from INT 1Ch (lib/screen.c scr_ticker),
  so they move while the harness is busy; mTCP hooks INT 1Ch too, so scr_ticker_off() before Net::stopStack().
- Dev tools on the image (C:\DEVEL, C:\TC; Turbo C comes from the user's own floppies in external/, never commit
  them): see docs/research/ and image/skills/DEVTOOLS.TXT, the skill that tells Claude about them.
- Skills (image/skills -> C:\SKILLS, manifest `SKILLS/`) are knowledge written for Claude: the build owns them, the
  system prompt lists them (`{skills}`: path, size, first line, frozen with the prefix like `{memories}`) and the model
  reads them with `read`. Memories (C:\MEMORY, shipped empty) are what the model learned on the PC; the memory tools
  and F4 never see the skills. A new skill: a .TXT in image/skills whose first line (60 characters) says when to read it.
- Disk images: `python scripts/fatimg.py ls|tree|get|put IMG ...`. In Git Bash, don't start image paths with `/` (MSYS rewrites them).
- Commit to main freely as checkpoints.

## Constraints
- 8086 instruction set only (`wcc/wpp -0`, NASM `cpu 8086`).
- Screen output goes through lib/screen (retrace-synced writes avoid CGA snow; hardware scroll via CRTC start address).
  The real IBM CGA does NOT wrap its display at the end of its 16 KB (86Box's does): the window must stay inside it.
  A pinned footer (scr_footer) is rewritten during vertical blanking on every scroll; keep it small (each row costs ~0.3 ms).
- Code linking mTCP (lib/net, lib/http, HARNESS.EXE) is GPLv3.
- Watcom: no quoted `-D` values (use the generated force-include), and no `{}` file lists for wlink (use a .lnk file).
