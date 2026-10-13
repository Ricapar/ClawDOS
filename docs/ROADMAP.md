# Claude-XT Roadmap

A DOS-based foundation for building apps on a real IBM 5160 that bridge to the
modern world. First flagship demo: a Claude chat client.

## Target hardware

| Component | Detail |
|-----------|--------|
| CPU | Intel 8088 @ 4.77 MHz (8086 instruction set only) |
| RAM | 640 KB conventional |
| Video | IBM CGA (text 80x25 @ B800:0000, 320x200x4, 640x200x2) |
| Storage | SD-card boot adapter (custom HDD adapter) |
| Network | PicoMEM v1.1 (FreddyV) — Pico W NE2000 emulation over WiFi, default I/O 300h, IRQ 3; also EMS (LoTech-compatible, up to 4 MB) and disk-image emulation |
| Other | Unidentified ISA cards — TBD |

## Software stack

- **OS:** FreeDOS 1.4 (8086-compatible kernel). Swappable for MS-DOS later.
- **Compiler:** OpenWatcom 2 (`wcl -0` for 8088 code), NASM for hand-written asm.
- **TCP/IP:** mTCP (Michael Brutman, GPLv3) — TCP library + DHCP/PING/HTGET tools.
  Note: anything we link against mTCP's library is GPLv3.
- **Packet driver:** NE2000 packet driver at 0x60 for the PicoMEM
  (`ne2000 0x60 0x3 0x300`, or PicoMEM's `pm2000 0x60`).
- **Emulators:** 86Box (cycle-accurate IBM XT + CGA + NE1000/NE2000 w/ SLiRP) for
  fidelity; DOSBox-X for fast iteration and headless test runs.

## Claude chat architecture

```
 IBM XT (CLAUDE.EXE)  --plain TCP, LAN-->  Bridge (Python, modern PC/Pi)  --HTTPS-->  Claude API
  CGA text UI, input,                       holds API key, TLS, history,
  scrollback, mTCP                          Unicode->CP437, streaming
```

Why a bridge: the Claude API requires TLS 1.2+. An 8088 at 4.77 MHz could in
principle do the handshake, but it would be extremely slow and need lots of
RAM for certificates and crypto state. The bridge keeps the XT side small and
fast, and it is a reusable pattern for other "modern world" apps (weather,
feeds, chat, etc.). Native TLS on the XT stays on the list as a stretch goal.

Bridge protocol (draft): line-oriented over TCP, CP437 text.
- Client → bridge: `MSG <text>\n`, `NEW\n` (reset conversation), `PING\n`
- Bridge → client: `TOK <chunk>\n` (streamed text), `END\n`, `ERR <reason>\n`

## Milestones

- [x] **M0 – Toolchain**: OpenWatcom 2, NASM, 86Box (+ROMs), DOSBox-X, FreeDOS 1.4,
      mTCP binaries + source. `hello.exe` built and run headless in DOSBox-X.
- [ ] **M1 – Bootable disk**: FreeDOS HDD image for the XT, booting in 86Box
      (IBM XT, CGA, 640K); write it to the SD card and boot on real hardware.
- [ ] **M2 – Networking**: Packet driver + mTCP DHCP/PING in the emulator, then
      on the real PicoMEM over WiFi.
- [ ] **M3 – Bridge server**: Python service talking to the Claude API, with the
      line protocol above, testable from a modern machine with `nc`.
- [ ] **M4 – CLAUDE.EXE**: CGA text chat UI on mTCP: input line, word-wrapped
      streaming output, scrollback.
- [ ] **M5 – Real hardware**: end-to-end on the XT over PicoMEM WiFi.
- [ ] **M6 – Foundation library**: factor shared code (CGA UI, net helpers,
      bridge client, EMS access) into `lib/` for future apps.

## Repo layout

```
scripts/   setup-tools.ps1 (fetch toolchain), env.ps1 (PATH setup)
src/       application sources (one dir per program)
docs/      notes and plans
tools/     downloaded toolchains (gitignored)
build/     build outputs (gitignored)
```
