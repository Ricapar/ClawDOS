# Claude-XT

DOS software for a real IBM PC XT (8088 @ 4.77 MHz, 640K, CGA, PicoMEM NE2000 over WiFi).
See docs/ROADMAP.md for hardware, architecture, and milestones.

## Environment
- Setup once: `.\scripts\setup-tools.ps1`. Per shell: `. .\scripts\env.ps1` (puts OpenWatcom, NASM, DOSBox-X, 86Box on PATH).
- Toolchains live in `tools/` (gitignored); build output in `build/` (gitignored).

## Build / run
- C for DOS: `wcl -q -0 -ms -bt=dos '-fe=out.exe' file.c` (quote `-fe=` in PowerShell). Always use `-0` (8088 code).
- Headless smoke test in DOSBox-X:
  `dosbox-x -silent -nogui -set "cpu cputype=8086" -set "cpu cycles=fixed 240" -set "dosbox machine=cga" -c "mount c build" -c "c:" -c "prog > out.txt" -c "exit"` then read `build\OUT.TXT`.
- mTCP apps build with `wmake` from their APPS/<name> dir (see tools/mtcp-src).

## Constraints
- 8086 instruction set only: no 186+ instructions (`pusha`, `shl r,imm>1`, `push imm`, etc.).
- Every byte and cycle matters: prefer direct CGA memory writes (B800:0000) over BIOS/DOS output for UIs; avoid CGA snow on real hardware in 80-col mode.
- Code linking mTCP's library is GPLv3.
