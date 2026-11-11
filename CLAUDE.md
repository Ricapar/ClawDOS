# ClawDOS

DOS software for a real IBM PC 5160 (8088 @ 4.77 MHz, 640K, CGA, PicoMEM: disk images + NE2000 over WiFi).
README.md has the quick start; docs/ROADMAP.md the plan; docs/API.md what CLAWDOS sends to the API and expects back.

## Workflow
- Setup once: `python3 scripts/setup-tools.py` (Homebrew on macOS, winget on Windows). Toolchains live in `tools/`
  (gitignored), outputs in `build/`. The Python (tunnel/) is stdlib only: no venv.
- macOS: the commands below say `python`; use `python3`. Watcom runs natively (tools/openwatcom/armo64).
- The Makefile wraps the commands below (`make` lists targets): `make build`, `make test` (host unit tests: the mock API, the tunnel, the image updater, bench.py's table),
  `make boottests` (the DOSBox-X tests that run offline), `make boottests-online` (those that need the internet:
  clock.bat), `make check` (test and boottests, offline: no connection beyond the mocks on the host), `make bench` (the 86Box benchmark), `make run`,
  `make tunnel`, `make mock`. Keep it in step with them.
- Update a real PC's image in place instead of replacing it: `python scripts/build.py --update PATH/CLAWDOS.IMG [--dry-run]
  [--memories] [--cfg] [--no-backup]` (`make update IMG=...`; scripts/imgupdate.py, tests scripts/test_imgupdate.py). Unless nothing would change, it copies
  the old image to IMG.bak (never overwritten: .bak.2, .bak.3, ...), then replaces/adds every file the fresh build ships (derived from the manifest, no directory list) and
  nothing else (SKILLS/ is the build's, replaced like any shipped file): MEMORY/ (`--memories`: only the memory files the build ships, none now; a memory named like a shipped skill, left by an older build, gets a note saying it can go with F4; a skill in SKILLS/ the build no longer ships gets a note too, and stays), CLAWDOS/CHATS/, CLAWDOS.LOG, CLAWDOS.CFG
  (`--cfg`), NET/MTCP.CFG (the DHCP lease), RUN.TXT/RUN.OUT/RESUME.TXT/SCREEN.SAV/SHOT.RAW/SHOT.PNG and files the build does not ship stay. The new image is
  read back before it replaces the old (`verify`: written files as shipped, every other file unchanged, FATs agreeing, no
  cross-links); a damaged image is refused (`fs_problems`). Never test it on the real SD-card image.
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
- Headless boot test (DOSBox-X): `python scripts/boottest.py [image/tests/x.bat]`. The Makefile's `boottests` target is
  the list of tests and what each must log; each batch file's REM header says what it checks. How a test reports: it
  writes to COM1 (build/serial.log), ends with `TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1` (CLAWDOS's own notes: retries,
  lost links, errors) and `ECHO CLAWDOS-TEST-DONE > COM1`, and echoes `HARNESS.EXE exit code %ERRORLEVEL%`, which the
  Makefile expects. `--expect TEXT` fails unless TEXT is in the serial or a mock's log, `--reject TEXT` if it is;
  `--timeout` (default 90 s) is in the test's header and its Makefile line. `--mock` runs tunnel/mockapi.py on port
  18023 (tests pass `10.0.2.2:18023`; log build/mock.log; the test fails if it cannot start, the message naming the
  port's holder, or dies); `--mock2` a second, separate mock on 18024 (a second provider section: provider.bat);
  `--key-tunnel` tunnel.py's key mode on 18025 in front of the first, over plain TCP with a made-up key
  (keytunnel.bat). The mock checks every request against its rules (docs/API.md, "The tunnel") and answers a
  violation with a 400 and a `VALIDATION:` line, so every mock test rejects that text; a task that adds a request
  field adds its rule to tunnel/mockapi.py. Every test calls `NETUP NOCLOCK` (DHCP, no NTP) and reaches nothing but
  the mocks: boottest.py fails one whose image has C:\NET\SNTP.LOG unless `--online` (clock.bat, the one test in
  `boottests-online`). A boot test (and test8088.py) holds build/boottest.lock, so one started meanwhile waits.
  `units.bat` runs DOSUNIT.EXE (tests/unit/dosunit.c with shotunit.c, memunit.c and apiunit.c, modules of their own
  for room in the code segment), built with `DOSUNIT` defined for test seams such as lib/history.c's full disk. To
  add a test: a batch file in image/tests, a line in the Makefile's `boottests`, a sentence here only if it needs
  a new switch. Interactive DOSBox-X with networking: `python scripts/rundosbox.py`
  (cputype 80186: the Crynwr NE2000 driver garbles frames on an emulated 8086, see boottest.py).
- Real 8088 check: `python scripts/test8088.py image/tests/devtools.bat` boots a test batch in 86Box (DOSBox-X runs
  as an 80186 and can't catch 186/286 code) and prints the log it leaves in C:\ (`--log`, `--done` marker;
  `--mock`, `--expect` and `--reject` as in boottest.py, the guest reaching the mock at 10.0.2.2:18023). The screenshot
  timer hook on the real CPU: `python3 scripts/test8088.py image/tests/shot8088.bat --mock --timeout 1500 --expect
  "shot_at=1.5): palette 0000AA 55FF55 FF5555 FFFF55, pixels B5BCA4D2" --expect "shot_at=1.5, full_memory=True): palette
  0000AA 55FF55 FF5555 FFFF55, pixels B5BCA4D2" --reject "VALIDATION:"` (at exit, `shot_at` in the harness and through
  the launcher; DOSUNIT prints how long the PNG took).
- Benchmark: `python scripts/bench.py` (`make bench`, about six minutes) runs image/tests/bench.bat in 86Box: the mock's
  `!bench` (20 reads of 4 KB, a `full_memory` run, a 4 KB reply) through CLAWDOS.EXE, then a table from CLAWDOS.LOG's
  `api` lines (`up=` upload ms, `len=` body bytes), the relaunch and the `stack:` high-water mark. docs/ROADMAP.md's
  "Benchmark" holds the baseline; keep the scenario and format stable. `--dosbox` is a quick, not comparable, run.
- Accurate 8088 PC: `python scripts/run86box.py`, then `python scripts/shot.py [--delay N]` and read build/shot.png
  (macOS: the terminal needs Screen Recording permission; 86Box ignores SIGTERM, quit it with
  `osascript -e 'tell application id "net.86Box.86Box" to quit'`).
  `scripts/sendkeys.ps1` (Windows only) can't type unless 86Box can take focus; prefer `--autotest` batch files and app flags like
  `CLAWDOS -m msg` (send), `-i text` (pre-fill the input box), `-c NAME` (reopen a chat) and `-k KEYS` (typed ahead:
  `~e` Enter, `~x` Esc, `~d` Del, `~u`/`~n` Up/Down, `~q` F10, `~4`/`~6` F4/F6; 15 at most). DOS command lines max out at 127 chars.
- CLAWDOS.EXE (src/clawdos) is the small launcher the user types: it runs HARNESS.EXE with `-launched`, and when a
  `run` tool call with `full_memory` makes the harness exit with code 200 it runs the commands itself (lib/shell.c) with the harness's
  memory free, then starts `HARNESS -resume -launched`, which reopens the conversation and carries the turn on. The
  files between them are in lib/relaunch.h (a screenshot's capture goes in SHOT.RAW, lib/shot.h: the launcher only
  copies the screen, the resumed harness makes the PNG; the `shot_at` copy from INT 1Ch goes to a DOS block held
  for the run, in both programs, never to EMS: the program may map its own pages into the frame); RESUME.TXT stays until the resume has connected, so `CLAWDOS -resume`
  retries one that failed. `HARNESS.EXE` run on its own keeps programs inside its memory (the boot tests do that).
  Around such an in-harness run the harness lends DOS the buffers it does not need meanwhile (lib/runmem.h: one
  block taken last-fit at the top of memory, so freed it joins the child's free block; owners ask runmem_slot each
  time, never keep a pointer across a run). Memory lessons: DOS gives a program the largest free block, so where a
  freed block lies matters, not the total; Open Watcom's startup keeps DGROUP at 64 KB for a near heap the large
  model never uses (harness.cpp's trimNearHeap gives it back but for the 2 KB the C library's spawn takes);
  FreeCOM below 100,096 bytes free asks on the keyboard for its strings and, with stdin on NUL, hangs: lib/shell.c
  refuses a COMMAND.COM run under 100 KB (SHELL_NEEDS).
  A relaunch lesson: a fresh process must not
  reuse the local TCP port its predecessor just closed (lib/net.cpp takes it from the BIOS time of day, not mTCP's
  tick count). mTCP's "End: heap is corrupted!" at exit is a false alarm: Open Watcom's `_fheapchk` flags any far
  heap in this build (DOSUNIT shows it after one malloc).
- HARNESS.EXE (src/harness) is the whole harness: it speaks the Messages API itself (src/harness/chat.cpp drives the turn;
  lib/apireq.c renders PROMPT.TXT (with the memories and the skills) and TOOLS.JSN once per conversation into CHATS\*.PRE, the frozen
  `system` and `tools` a conversation's creating requests send, and writes each request from that and the history file
  (with `threads 1`, a continue request sends only the history since the last reply: docs/API.md "Threads"), whose writes
  lib/history.c buffers; a request is written from the turn's copy of the provider's settings (`api_req_settings`),
  since the socket's flushes handle keys and F5 must change nothing already counted: it applies from the next message;
  `/model` to another provider section chooses the conversation's fixed settings again from it, drops the thread and
  rewrites only the `.PRE`'s cache marker (`api_fixed_switch`), within a section only the model changes; lib/apistream.c turns the SSE stream into screen text, history and tool calls). Only TLS is off the PC: `python3 tunnel/tunnel.py` copies bytes between a plain port
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
  battery clock), NETUP.BAT sets the time by SNTP (log in C:\NET\SNTP.LOG; `NETUP NOCLOCK` skips it) and, after a big jump, takes a fresh DHCP
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
