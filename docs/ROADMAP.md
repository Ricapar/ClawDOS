# ClawDOS Roadmap

A DOS-based foundation for building apps on a real IBM 5160 that bridge to the
modern world. First flagship demo: a Claude chat client.

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
- **Emulators:** 86Box (cycle-accurate IBM XT + CGA + NE1000/NE2000 w/ SLiRP) for
  fidelity; DOSBox-X for fast iteration and headless test runs.

## Claude chat architecture

```
 DOS PC (CLAWDOS.EXE)  --plain TCP, LAN-->  Bridge (Python, modern PC/Pi)  --HTTPS-->  Claude API
  CGA text UI, input,                       holds API key, TLS, history,
  scrollback, mTCP                          Unicode->CP437, streaming
```

Why a bridge: the Claude API requires TLS 1.2+. An 8088 at 4.77 MHz could in
principle do the handshake, but it would be extremely slow and need lots of
RAM for certificates and crypto state. The bridge keeps the DOS PC side small and
fast, and it is a reusable pattern for other "modern world" apps (weather,
feeds, chat, etc.). Native TLS on the DOS PC stays on the list as a stretch goal.

The bridge protocol is in [PROTOCOL.md](PROTOCOL.md).

## Milestones

- [x] **M0 - Toolchain**: OpenWatcom 2, NASM, 86Box (+ROMs), DOSBox-X, FreeDOS 1.4,
      mTCP binaries + source.
- [x] **M1 - Bootable disk**: `scripts/build.py` makes a FreeDOS 1.4 HDD image
      (16 heads x 63 sectors, as PicoMEM expects) with our own MBR, the 8086
      FreeDOS kernel, DOS tools, mTCP and the PicoMEM drivers. Boots in DOSBox-X
      (`scripts/boottest.py`) and on an 8088 PC in 86Box.
- [x] **M2 - Networking (emulated)**: `NETUP.BAT` loads PM2000 on a PicoMEM, or
      Crynwr NE1000/NE2000 at 340h in emulators, then DHCP. Works in 86Box.
- [x] **M3 - Bridge server**: `bridge/dosbridge.py` (Claude API, streaming,
      refusal fallbacks, CP437) with an offline mock backend (`--mock`) for testing without an API key.
- [x] **M4 - CLAWDOS.EXE**: streaming chat with word wrap, hardware scrolling,
      scrollback, stop/new-chat. Verified against the mock bridge in 86Box.
- [x] **M4.5 - Tools**: Claude acts on the DOS PC through CLAWDOS.EXE: files, system
      info, DOS commands, memory and ports, PC speaker music (PLAY), text art;
      safe mode asks before changes (F3). The CGA drawing tool was dropped
      in October 2026: real compilers on the image made it redundant.
      Design in [superpowers/specs/2026-10-21-dos-tools-design.md](superpowers/specs/2026-10-21-dos-tools-design.md).
- [x] **M5 - Real hardware**: boot CLAWDOS.IMG from the PicoMEM, PM2000 over
      WiFi, chat with the live Claude API.
- [ ] **M6 - Building on the DOS PC**: better tools for Claude writing programs on
      the DOS PC itself (Turbo C, assemblers), from what the CGA demo and WarGames
      sessions of 2026-10-25/26 showed (bridge/logs/). Details below.
  - [x] **M6.1 - Dev tools pack**: `edit` (replace text in a file instead of
        rewriting all of it: 49 whole-file writes of up to 5.4 KB happened),
        `search` (grep with line numbers inside CLAWDOS: FIND took 10-79 s a
        call), `run` with a list of commands that stops at the first failure
        (FreeCOM has no `&`/`&&`; each TCC call was its own tool call), and the
        clock set from the bridge on connect (every file on the DOS PC is dated
        1980-01-01: SNTP at boot doesn't take on the real PC).
  - [ ] **M6.2 - Full-memory run**: programs Claude runs get nearly all of
        640 KB instead of ~285 KB. See [M6.2](#m62-full-memory-run) below.
  - [ ] **M6.3 - Screenshots**: Claude sees what its programs draw. Capture
        the CGA's 16 KB at exit (and optionally every few seconds into EMS
        pages during the run); the bridge renders PNGs and hands them to Claude
        as images. The CGA's mode and palette registers are write-only, so the
        bridge guesses from the BIOS data area unless the tool call says
        (e.g. the mode 5 / 3D9h tricks WarGames uses). 13 demo runs came back
        as "(screen text)"; fixes were "best guess, untested visually".
  - [ ] **M6.4 - Project history**: the bridge commits every file Claude
        writes to a git repo per DOS PC directory; `snapshot`/`restore` tools
        replace hand-made copies (XCOPY C:\WG C:\WG3 took 43 s), and the code
        can be browsed on the PC.
- [ ] **M7 - Foundation**: grow `lib/` (input line editor, windows/menus, more
      bridge services) and a launcher for ClawDOS apps.

## M6.2: full-memory run

**Problem.** CLAWDOS.EXE stays resident while the `run` tool's program runs:
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
   8086, a few KB; e.g. CLAW.COM, which the user starts instead of CLAWDOS)
   runs CLAWDOS.EXE. For a big run CLAWDOS saves its state (session token,
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
   still dies while the receiver is gone. Needs EMS (none in 86Box's XT
   unless an EMS board is emulated); a disk swap file would be the fallback.

Leaning toward 1: simpler, no EMS dependency, and CLAWDOS's exit/start paths
are already tested. The open questions either way:

- **Bridge: a tool call that outlives its connection.** Today a disconnect
  cancels the turn (bridge/dosbridge.py, `finally:` in the session loop) and
  RESUME only restores history, so the pending tool call is lost. The DOS PC
  needs to say it's going away for a tool (e.g. `AWAY <tool id>`), the bridge
  keeps the turn waiting (for longer than a normal lost link; builds and demo
  runs took up to 9 minutes), and after `RESUME` the DOS PC sends the tool result
  and the turn carries on. PROTOCOL.md and the mock bridge get the same.
- **When to go big.** Every run (simple, but DIR then costs a reconnect), a
  tool argument the model sets, or automatically for known heavy programs
  (TCC, TLINK, MAKE, batch files). DEVTOOLS.TXT must say which.
- **Where state goes.** A file in C:\CLAW (survives a crash or reboot, so
  CLAWDOS could even recover after the program hangs the DOS PC) or an EMS page.
- **Safe mode:** the y/n prompt stays before the exit; Esc and the
  exit-code hold screen move into the stub.

**Testing.** DOSBox-X with `--bridge`: a boottest that has Claude run stock
PRIMES.C with `time.h` through plain `TCC` (auto-linked) and checks the
resumed tool result in the bridge log; a mock `!tool` for a big run; a check
that a program's free memory (e.g. `MEM`) is above the goal. Then the 86Box
8088 (`scripts/test8088.py`) and the real PC, building WarGames with
stock headers and MAKE.

## Repo layout

See the README.
