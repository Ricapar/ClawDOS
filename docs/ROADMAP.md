# Claude-XT Roadmap

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
 IBM XT (CLAUDE.EXE)  --plain TCP, LAN-->  Bridge (Python, modern PC/Pi)  --HTTPS-->  Claude API
  CGA text UI, input,                       holds API key, TLS, history,
  scrollback, mTCP                          Unicode->CP437, streaming
```

Why a bridge: the Claude API requires TLS 1.2+. An 8088 at 4.77 MHz could in
principle do the handshake, but it would be extremely slow and need lots of
RAM for certificates and crypto state. The bridge keeps the XT side small and
fast, and it is a reusable pattern for other "modern world" apps (weather,
feeds, chat, etc.). Native TLS on the XT stays on the list as a stretch goal.

The bridge protocol is in [PROTOCOL.md](PROTOCOL.md).

## Milestones

- [x] **M0 - Toolchain**: OpenWatcom 2, NASM, 86Box (+ROMs), DOSBox-X, FreeDOS 1.4,
      mTCP binaries + source.
- [x] **M1 - Bootable disk**: `scripts/build.py` makes a FreeDOS 1.4 HDD image
      (16 heads x 63 sectors, as PicoMEM expects) with our own MBR, the 8086
      FreeDOS kernel, DOS tools, mTCP and the PicoMEM drivers. Boots in DOSBox-X
      (`scripts/boottest.py`) and on an 8088 XT in 86Box.
- [x] **M2 - Networking (emulated)**: `NETUP.BAT` loads PM2000 on a PicoMEM, or
      Crynwr NE1000/NE2000 at 340h in emulators, then DHCP. Works in 86Box.
- [x] **M3 - Bridge server**: `bridge/xtbridge.py` (Claude API, streaming,
      refusal fallbacks, CP437) with an offline echo backend.
- [x] **M4 - CLAUDE.EXE**: streaming chat with word wrap, hardware scrolling,
      scrollback, stop/new-chat. Verified against the echo bridge in 86Box.
- [ ] **M5 - Real hardware**: boot CLAUDEXT.IMG from the PicoMEM, PM2000 over
      WiFi, chat with the live Claude API.
- [ ] **M6 - Foundation**: grow `lib/` (input line editor, windows/menus, EMS
      via PicoMEM, more bridge services) and a launcher for Claude-XT apps.

## Repo layout

See the README.
