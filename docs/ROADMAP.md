# ClawDOS Roadmap

A DOS-based foundation for building apps on a real IBM 5160 that reach the
modern world. First flagship demo: a Claude chat client that is the whole
harness, running on the 8088.

## Target hardware

| Component | Detail |
|-----------|--------|
| CPU | Intel 8088 @ 4.77 MHz (8086 instruction set only) |
| RAM | 640 KB conventional |
| Video | IBM CGA (text 80x25 @ B800:0000, 320x200x4, 640x200x2) |
| Storage | PicoMEM disk emulation: raw `.IMG` files in `HDD/` on its microSD |
| Network | PicoMEM v1.1 (FreddyV) — Pico W NE2000 emulation over WiFi, default I/O 300h, IRQ 3; also EMS (LoTech-compatible, up to 4 MB) and disk-image emulation |
| Other | Unidentified ISA cards — TBD |

## Software stack

- **OS:** FreeDOS 1.4 (8086-compatible kernel). Swappable for MS-DOS later.
- **Compiler:** OpenWatcom 2 (`wcl -0` for 8088 code), NASM for hand-written asm.
- **TCP/IP:** mTCP (Michael Brutman, GPLv3) — TCP library + DHCP/PING/HTGET tools.
  Note: anything we link against mTCP's library is GPLv3.
- **Packet driver:** PicoMEM's `PM2000 0x60` (NE2000 over WiFi, auto-detected);
  Crynwr NE1000/NE2000 at 340h in emulators.
- **Emulators:** 86Box (cycle-accurate IBM PC 5160 + CGA + NE1000/NE2000 w/ SLiRP) for
  fidelity; DOSBox-X for fast iteration and headless test runs.

## Claude chat architecture

```
 DOS PC (HARNESS.EXE)                 --plain HTTP over TCP-->  TLS tunnel        --HTTPS-->  Claude API
  CGA text UI, scrollback, mTCP,                               (tunnel.py, socat
  Messages API requests, SSE + JSON,                            or stunnel: copies
  history on disk, API key, tools                               bytes, nothing else)
```

Why a tunnel: the Claude API requires TLS 1.2+. An 8088 at 4.77 MHz could in
principle do the handshake, but it would take minutes and need lots of RAM for
certificates and crypto state. Everything else, from building the request to
parsing the streamed reply and keeping the history, the PC does itself.
The tunnel has no knowledge of the API, so native TLS (BearSSL's i15 big-number
code is written for 16-bit multipliers) or a tunnel inside the PicoMEM firmware
could replace it later without touching CLAWDOS. What CLAWDOS sends and expects
is in [API.md](API.md).

## Milestones

- [x] **M0 - Toolchain**: OpenWatcom 2, NASM, 86Box (+ROMs), DOSBox-X, FreeDOS 1.4,
      mTCP binaries + source.
- [x] **M1 - Bootable disk**: `scripts/build.py` makes a FreeDOS 1.4 HDD image
      (16 heads x 63 sectors, as PicoMEM expects) with our own MBR, the 8086
      FreeDOS kernel, DOS tools, mTCP and the PicoMEM drivers. Boots in DOSBox-X
      (`scripts/boottest.py`) and on an 8088 PC in 86Box.
- [x] **M2 - Networking (emulated)**: `NETUP.BAT` loads PM2000 on a PicoMEM, or
      Crynwr NE1000/NE2000 at 340h in emulators, then DHCP. Works in 86Box.
- [x] **M3 - Bridge server** (retired October 2026): `bridge/dosbridge.py` spoke
      the API for the DOS PC and relayed tool calls over a line protocol. Replaced
      by M5.5; its offline mock lives on as `tunnel/mockapi.py`.
- [x] **M4 - HARNESS.EXE**: streaming chat with word wrap, hardware scrolling,
      scrollback, stop/new-chat. Verified against the mock in 86Box.
- [x] **M4.5 - Tools**: Claude acts on the DOS PC through HARNESS.EXE: files, system
      info, DOS commands, memory and ports, PC speaker music (PLAY), text art;
      safe mode asks before changes (F3). The CGA drawing tool was dropped
      in October 2026: real compilers on the image made it redundant.
- [x] **M5 - Real hardware**: boot CLAWDOS.IMG from the PicoMEM, PM2000 over
      WiFi, chat with the live Claude API.
- [x] **M5.5 - Native API client**: HARNESS.EXE speaks the Messages API itself
      through a dumb TLS tunnel (`tunnel/tunnel.py`, or socat/stunnel): request
      writer, SSE and JSON parsing, thinking blocks echoed back, the tool loop,
      the history as a file on the DOS PC, retries and lost-link recovery,
      `/model` across providers that speak the API. The Python bridge is gone.
      First live run (86Box 8088, 2026-10-31, before buffered upload in
      `766f1f8`, so no longer what the code does): a fresh conversation's first
      request (11.7 KB up: prompt, tool schemas, one message) reached the first
      token after 19 s and finished at 21 s, with the system prompt and tools
      served from the prompt cache on the second run (`cr=4061`). Current
      figures, up to a 135 KB request: [Benchmark](#benchmark).
- [ ] **M6 - Building on the DOS PC**: better tools for Claude writing programs on
      the DOS PC itself (Turbo C, assemblers), from what the CGA demo and WarGames
      sessions of 2026-10-25/26 showed (the bridge's JSONL logs of that time). Details below.
  - [x] **M6.1 - Dev tools pack**: `edit` (replace text in a file instead of
        rewriting all of it: 49 whole-file writes of up to 5.4 KB happened),
        `search` (grep with line numbers inside CLAWDOS: FIND took 10-79 s a
        call), `run` with a list of commands that stops at the first failure
        (FreeCOM has no `&`/`&&`; each TCC call was its own tool call), and a
        clock (every file on the DOS PC was dated 1980-01-01). The clock first
        came from the API's Date header; since 2026-11 the boot scripts own it:
        the PicoMEM 2's battery clock (ASTCLOCK) and SNTP in NETUP.BAT, which
        failed before on DNS and mTCP's DHCP lease check.
  - [ ] **M6.2 - Full-memory run**: programs Claude runs get nearly all of
        640 KB instead of ~285 KB. See [M6.2](#m62-full-memory-run) below.
  - [ ] **M6.3 - Screenshots**: Claude sees what its programs draw. Capture
        the CGA's 16 KB at exit (and optionally every few seconds into EMS
        pages during the run) and send them as images. With no bridge, the
        PNG has to be made on the DOS PC (a 16 KB CGA frame is small) and go
        into the request as a base64 image block. The CGA's mode and palette
        registers are write-only, so the mode comes from the BIOS data area
        unless the tool call says (e.g. the mode 5 / 3D9h tricks WarGames
        uses). 13 demo runs came back as "(screen text)"; fixes were "best
        guess, untested visually".
  - [ ] **M6.4 - Project history**: `snapshot`/`restore` tools on the DOS PC
        replace hand-made copies (XCOPY C:\WG C:\WG3 took 43 s); the
        conversation files in C:\CLAWDOS\CHATS already record what was written.
- [ ] **M7 - Foundation**: grow `lib/` (input line editor, windows/menus, the
      HTTP client for other web APIs through the same tunnel) and a launcher for
      ClawDOS apps.

## Benchmark

`python3 scripts/bench.py` (`make bench`, about half an hour) runs
`image/tests/bench.bat` in 86Box against the mock API: the mock's `!bench` has
CLAWDOS.EXE do 20 `read`s of a 4 KB file, one `run` of `VER` with
`full_memory` (a real relaunch) and a 3.9 KB reply. Per round: `gap` the
seconds since the previous round ended (the tool call, its result written to
the history, the request's length counted), `len` the request body, `up` the
time to send it, `ttft-up` the wait for the (mock) API, `total` the round.
Taken 2026-11-10 at `f521559`, 86Box IBM PC 5160 (8088 4.77 MHz, NE1000 on
SLiRP). Later changes rerun it and compare; the scenario and the format stay.

| round | gap s | len | up ms | up KB/s | ttft-up ms | total ms |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | - | 12926 | 2530 | 5.0 | 1210 | 4235 |
| 1 | 67 | 19063 | 2970 | 6.3 | 1100 | 4620 |
| 2 | 68 | 25200 | 3355 | 7.3 | 1100 | 5005 |
| 3 | 69 | 31337 | 3630 | 8.4 | 1155 | 5335 |
| 4 | 68 | 37474 | 4070 | 9.0 | 1155 | 5775 |
| 5 | 69 | 43613 | 4565 | 9.3 | 1100 | 6215 |
| 6 | 68 | 49752 | 4950 | 9.8 | 1155 | 6655 |
| 7 | 69 | 55891 | 5225 | 10.4 | 1155 | 6985 |
| 8 | 69 | 62030 | 5610 | 10.8 | 1155 | 7370 |
| 9 | 69 | 68169 | 6050 | 11.0 | 1155 | 7810 |
| 10 | 70 | 74309 | 6435 | 11.3 | 1155 | 8195 |
| 11 | 69 | 80449 | 6875 | 11.4 | 1210 | 8635 |
| 12 | 69 | 86589 | 7205 | 11.7 | 1155 | 8965 |
| 13 | 71 | 92729 | 7645 | 11.8 | 1210 | 9405 |
| 14 | 69 | 98869 | 8030 | 12.0 | 1210 | 9845 |
| 15 | 71 | 105009 | 8470 | 12.1 | 1100 | 10175 |
| 16 | 70 | 111149 | 8800 | 12.3 | 1210 | 10615 |
| 17 | 71 | 117289 | 9240 | 12.4 | 1210 | 11000 |
| 18 | 71 | 123429 | 9680 | 12.5 | 1155 | 11440 |
| 19 | 70 | 129569 | 10065 | 12.6 | 1155 | 11825 |
| 20 | 72 | 135709 | 10395 | 12.7 | 1155 | 12155 |
| 21 | 32 | 137848 | 10945 | 12.3 | 1210 | 33880 |
| all | | 1698402 | 146740 | 11.3 | | 206140 |

The turn took 1627 s from the first request to the last reply.

Final round: 3948 characters streamed in 21725 ms after the first, 182 characters/s (stop=end_turn).
Relaunch round trip: program 3.1 s, CLAWDOS away 25.7 s.
Stack: used 1347 of 4027 bytes; used 1557 of 4055 bytes (one line per HARNESS.EXE).

Upload runs at 5 KB/s for the first request and 12-13 KB/s for big ones, so
the 136 KB request of round 20 takes 10 s before the API sees it. The biggest
cost is outside the requests, though: about 68 s per `read` round (the run
round, with a short result, has ~6 s besides the relaunch), most likely the
4 KB tool_result going into the history a character at a time
(`json_escape_cp437` into `hist_write`, an `lseek` and a `write` per
character). The stack never came within 2.4 KB of its end.

## M6.2: full-memory run

**Problem.** HARNESS.EXE stays resident while the `run` tool's program runs:
about 355 KB of it (mTCP and its buffers, the chat, the heap), leaving ~285 KB
(Stage 1 memory, `dc21363`, already won ~90 KB). On the DOS PC this cost:

- TCC "Out of memory" on `time.h`/`stdlib.h` and on big functions; Claude wrote
  its own tiny DOS.H and an empty STDLIB.H (C:\TESTS\DEV) to get by, and once
  compiled with "Available memory 0".
- TCC can't spawn TLINK ("Unable to execute command 'tlink.exe'"), so every
  build is TCC -c on one or two files per call (~30 s each) plus a hand TLINK.
- Batch files and MAKE are out: a second COMMAND.COM costs ~71 KB.
- Claude's memories (TCCMEM, DEMOGUID, WARGAMES) are full of these workarounds.

**Goal.** A program `run` starts has at least ~560 KB free, so stock Turbo C
compiles with its own headers, spawns TLINK itself, and Turbo C's MAKE.EXE
(already on the image in C:\TC) can drive builds. CLAWDOS comes back afterwards
on the same conversation with the program's output as the tool result, the chat
screen as it was, and the link restored.

**Approaches** (pick one in the design session):

1. **Exit and relaunch through a stub.** A tiny resident launcher (NASM,
   8086, a few KB; e.g. CLAWDOS.COM, which the user starts instead of CLAWDOS)
   runs HARNESS.EXE. For a big run CLAWDOS saves its state (conversation file,
   pending tool id, flags like `-unsafe`, the scrollback/screen, the input
   line), shuts mTCP down cleanly (releasing the packet driver) and exits
   with a code that tells the stub to run the command. The stub runs it with
   output capture (the INT 10h hook moves into the stub), writes the output,
   then runs CLAWDOS again in resume mode. No vectors point into freed memory,
   because CLAWDOS really exits. Costs a reload of the ~120 KB EXE and a
   reconnect (a few seconds) per big run.
2. **Swap CLAWDOS out to EMS.** Like Ralf Brown's SPAWNO: copy CLAWDOS's memory
   into EMS pages (PicoMEM offers up to 4 MB), shrink to a small stub, exec,
   copy back. No reload, but every hooked vector (INT 10h capture, 1Bh/23h
   Ctrl-Break, the packet driver's receive upcall into mTCP) must be unhooked
   first and the packet driver released and re-acquired; the TCP connection
   still dies while the receiver is gone. Needs EMS (none in 86Box's IBM PC 5160
   unless an EMS board is emulated); a disk swap file would be the fallback.

**Status (branch `relaunch`, a spike of approach 1).** The chat client is now
HARNESS.EXE, and CLAWDOS.EXE (src/clawdos, a small-model C program linking
lib/shell.c) is the launcher the user types: it starts HARNESS with `-launched`;
the run tool then writes the commands to C:\CLAWDOS\RUN.TXT and the harness
saves its state (RESUME.TXT: chat file, pending tool_use id, round, convo flags,
switches; SCREEN.SAV: the 16 KB of video memory plus the scroll and console
state) and exits with code 200. The launcher runs the commands exactly as the
in-process tool would, writes RUN.OUT and starts `HARNESS -resume`, which
reopens the history file, restores the screen, shows the result under the Run
header and continues the rounds (lib/relaunch.h, Chat::resume/continueTurn).
Measured in DOSBox-X (`image/tests/relaunch.bat`): a program gets 449 KB,
against 220 KB in-process; the launcher's block is 50 KB; the harness is away
1.7 s for a 1.0 s program. Only a `run` with `full_memory: true` goes through
the launcher (the model decides: TOOLS.JSN and DEVTOOLS.TXT say to set it for
compilers, linkers, assemblers, freshly built programs and batch files); the
rest run inside the harness, with no reconnect. `HARNESS` started on its own
runs everything in-process and tells the model the flag had no effect. The
Up/Down input history travels in SCREEN.SAV. The launcher's 50 KB was left as
it is: most of it is the output capture's buffers and the C runtime that
shell.c's formatting needs. Still to do: timing on the real PC, and building
WarGames with stock headers and MAKE through `full_memory`.

First live runs on the 86Box 8088 (2026-10-31, before buffered upload; the
relaunch's current cost is in [Benchmark](#benchmark)): a 35 s `dir /s | find` kept
CLAWDOS away 46 s, so a relaunch costs about 11 s there (the 133 KB EXE reloads,
reconnects and reopens the history); `TCC -c VOYAGER.C` reported "Available
memory 215298" and TLINK ran, where the in-process run had no room. Two faults
came out of it, both fixed: the resumed CLAWDOS failed mTCP's DHCP lease check
once the clock had been set from the API (DHCP had stamped the lease with the
1980 boot clock; fixed then by moving the stamp in MTCP.CFG, and since 2026-11
by NETUP taking a fresh lease after SNTP), and it could draw the local TCP port its predecessor had just
closed, because mTCP's tick counter starts at 0 in every process (the port now
comes from the BIOS time of day). `CLAWDOS -resume` retries a resume that failed.

Leaning toward 1: simpler, no EMS dependency, and CLAWDOS's exit/start paths
are already tested. The open questions either way:

- **A tool call that outlives the program.** With the history on disk this is
  simpler than it was with the bridge: the conversation file already holds the
  assistant message with the pending tool_use, so the relaunched CLAWDOS only
  has to write the tool_result message and send the next request. The state to
  save is the conversation file name, the pending tool id and the flags.
- **When to go big.** Every run (simple, but DIR then costs a reconnect), a
  tool argument the model sets, or automatically for known heavy programs
  (TCC, TLINK, MAKE, batch files). DEVTOOLS.TXT must say which.
- **Where state goes.** A file in C:\CLAWDOS (survives a crash or reboot, so
  CLAWDOS could even recover after the program hangs the DOS PC) or an EMS page.
- **Safe mode:** the y/n prompt stays before the exit; Esc and the
  exit-code hold screen move into the stub.

**Testing.** DOSBox-X with `--mock`: a boottest that has Claude run stock
PRIMES.C with `time.h` through plain `TCC` (auto-linked) and checks the
resumed tool result in the mock's log; a mock `!tool` for a big run; a check
that a program's free memory (e.g. `MEM`) is above the goal. Then the 86Box
8088 (`scripts/test8088.py`) and the real PC, building WarGames with
stock headers and MAKE.

## Repo layout

See the README.
