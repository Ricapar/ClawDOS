# Assemblers, linkers/librarians and debuggers that run ON an 8088 IBM PC 5160

Scope: the tool itself must execute on an 8088 @ 4.77 MHz, 640 KB, DOS 3.3+ (FreeDOS 1.4), driven by one
non-interactive command line (~120 chars, stdout captured, no keyboard). Licence classes:
(a) free/OSS redistributable; (b) freeware/shareware with redistribution allowed; (c) downloadable but licence unclear;
(d) commercial, needs an original copy.

Research budget note: ibiblio.org returned HTTP 503 for every fetch during this session, so several FreeDOS package
pages and the NASM 8086host directory listing could not be read directly. Claims marked "(unverified / from memory)"
have no fetched source and belong in the gaps.

## 1. NASM: which builds run on an 8086/8088?

### Takeaway
No official NASM release has a true 8086-host build: the official 16-bit DOS build of 0.98.39 (the "nasm16" line) is
186+ (uses ENTER). Community-rebuilt 8086-host binaries of 0.98.39 exist in the FreeDOS archive, but are memory-starved
on 640 KB and very slow (~7 minutes for a 2000-line source on an estimated 4.77 MHz 8086). NASM 2.x is 386+ only.

### Cited Findings
- "the official build of 0.98.39 for 16-bit DOS was 186 only" because it uses the ENTER instruction; some emulators
  (Fake86, 8086tinyplus) fail on it — [bttr-software forum, Rugxulo 2020](https://www.bttr-software.de/forum/forum_entry.php?id=16359)
- The prebuilt 16-bit EXE appears to have been compiled with MS C 7 (Mkfiles/Makefile.ms7) with `/G2` (286 target) —
  [bttr forum](https://bttr-software.de/forum/forum_entry.php?id=16399); the official 16-bit DOS binaries are on
  SourceForge under "DOS 16-bit binaries (OBSOLETE)/0.98.39" — [bttr forum](https://www.bttr-software.de/forum/forum_entry.php?id=16359)
- Two 8086-host rebuilds of 0.98.39 (Turbo C++ 1.01, Huge model; OpenWatcom 1.9) with makefiles are hosted at
  ibiblio.org/pub/micro/pc-stuff/freedos/files/devel/asm/nasm/0.98.39/8086host/; only `bin` and `obj` output enabled;
  conventional memory only; author says overlays, optional EMS or more trimming "may be needed" —
  [bttr forum](https://www.bttr-software.de/forum/forum_entry.php?id=16359)
- Memory: a build "stripped to 686 instructions" ran out of memory on 8086; "NASM.EXE is a 460 KiB file which means
  that it barely runs at all, while NDISASM.EXE worked"; the FreeDOS kernel build ran out of memory because make
  ate conventional RAM — [bttr forum search summary](https://www.bttr-software.de/forum/forum_entry.php?id=16384),
  [bttr forum](https://www.bttr-software.de/forum/forum_entry.php?id=16359)
- NASM 2.xx (since 2007) needs a C99 compiler and won't build for less than 386 protected mode —
  [bttr forum](https://www.bttr-software.de/forum/forum_entry.php?id=16359)
- Speed (estimated from NTVDM 8086 cycle counts at 4.77 MHz, PSR Invaders ~2000-line source): NASM16 `-O3 -a`
  ~423 s (~7 min); MiniNASM `-O3` ~357 s (~6 min) —
  [bttr forum timing thread, 2026](https://bttr-software.de/forum/mix_entry.php?id=23405)
- Current FreeDOS NASM package licence: Simplified (2-Clause) BSD —
  [FreeDOS devel group listing](https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/pkg-html/group-devel.html)

### Inferences
- Licence at 0.98.39: NASM was relicensed from LGPL to 2-clause BSD in the 2.x era; 0.98.39 was LGPL
  (unverified / from memory — not fetched). Either way class (a).
- Practical verdict: NASM is a poor fit on the DOS PC. TinyASM (section 3) gives NASM syntax at a fraction of the size.

### Gaps
- Exact filenames/sizes in the 8086host directory (ibiblio 503).
- Exact licence text in the 0.98.39 archive.

## 2. A86 / A386 / D86 (Eric Isaacson)

### Takeaway
A86 is the fastest 8088-hosted assembler by a wide margin (~5 s for a 2000-line source on a 4.77 MHz 8086 estimate),
writes .COM directly without a linker, and needs only ~40 KB beyond itself. It is still shareware (class b): free copying
of the complete unmodified ZIP is allowed, but registration ($50/$80) is required for continued use, and only
registered users may distribute programs made with it. D86 is in the same shareware ZIP; A386/D386 are registered-only.

### Cited Findings
- Still sold: "the basic A86-only package is $52"; extended package with D86, A386, D386 is $82 —
  [eji.com/a86](http://eji.com/a86/)
- Manual licence terms: "You may copy the A86Vxxx.ZIP and D86Vxxx.ZIP files, and give them to anyone who accepts these
  terms" (complete and unmodified); "This package may not be sold to anyone"; diskette fee "may not exceed $10";
  if you find it useful "you must become a registered user"; companies/agencies and educational institutions "MUST be
  registered"; only registered users may sell or distribute programs written with it —
  [A86 manual](https://pic.hallikainen.org/techref/language/a86/A86MANU.htm)
- Requirements: "MS-DOS V2.00 or later"; "You can assemble with only 40K bytes of memory beyond the program itself";
  uses up to 400K if available — [A86 manual](https://pic.hallikainen.org/techref/language/a86/A86MANU.htm)
- No linker needed: "the A86 program is all you need to produce programs with the COM extension"; output "can be a
  .COM file, ready to execute immediately"; switches can go anywhere on the command line, wildcards allowed —
  [A86 manual](https://pic.hallikainen.org/techref/language/a86/A86MANU.htm)
- Target CPU switch +P0 = 8086/8088 — [A86 manual](https://pic.hallikainen.org/techref/language/a86/A86MANU.htm)
- Speed: A86 V4.05 ~5.4 s (25.8 M cycles) on the 2000-line test at 4.77 MHz estimate —
  [bttr timing thread](https://bttr-software.de/forum/mix_entry.php?id=23405); author claims "4 times as fast as MASM
  V5.1" — [A86 manual](https://pic.hallikainen.org/techref/language/a86/A86MANU.htm)
- Ran on the Atari Portfolio (80C88), "annoyingly slow" for 10–20 kB sources there —
  [bttr timing thread](https://bttr-software.de/forum/mix_entry.php?id=23405)
- Wikipedia/aggregators conflict ("Freeware" vs "Proprietary") — [Wikipedia A86](https://en.wikipedia.org/wiki/A86_(software)),
  [Comparison of assemblers](https://en.wikipedia.org/wiki/Comparison_of_assemblers). The author's own page (still
  selling registrations) is authoritative: not free.
- A386/D386 "were not distributed as shareware, but were provided to users who registered" —
  [Wikipedia A86](https://en.wikipedia.org/wiki/A86_(software))

### Inferences
- Shipping the unmodified A86/D86 ZIP on the disk image appears permitted by the copy clause; the user (or agent) using
  it beyond evaluation should register. Its syntax is idiosyncratic (MASM-like but not identical).
- D86 is an interactive full-screen debugger: little use to a no-keyboard agent.

### Gaps
- Whether the newest A86 (v4.05) still runs on an 8088: no source states a host CPU minimum, but the Portfolio and NTVDM-
  8086 reports strongly suggest yes.

## 3. Other assemblers: Arrowsoft, CHASM, Wolfware, TinyASM, JWasm/WASM, FASM, Pass32, TASM, MASM

### Takeaway
8088-hosted and free: Wolfware Asm (now MIT, ~42 s on the test), TinyASM (BSD-2, NASM syntax, explicitly runs on a real
8088 PC, slow), Arrowsoft ASM 2.00c (public domain/freeware, MASM 3.0 syntax, needs a linker such as VAL), JWASMR (Open
Watcom licence, "runs on any x86", ~4–5 min, needs DOS 5-compatible). Excluded (386+): FASM, JWasm's DOS-extended
builds, Pass32 (unverified). Commercial: MASM 1.0–5.1 run on 8088 (5.1 ~55 s on test); MASM 6.x is DOS-extended;
TASM.EXE (real mode) in TASM 1.0–3.x/4.0 is believed 8088-capable (unverified).

### Cited Findings
- **Wolfware Asm**: FreeDOS devel package "Wolfware Asm — Intel x86-compatible assembler", licence MIT —
  [FreeDOS devel listing](https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/pkg-html/group-devel.html);
  ~42 s estimated on 4.77 MHz 8086 for the 2000-line test (~50 s without stripping comments) —
  [bttr timing thread](https://bttr-software.de/forum/mix_entry.php?id=23405)
- **TinyASM** (Oscar Toledo / nanochess): "Tiny assembler for 8086/8088, able to run over DOS for 8086/8088 machines";
  "can work over a real PC XT machine"; needs "at least 128K of memory"; NASM-compatible syntax subset (db, dw, equ,
  times, %include, incbin); `tinyasm -f bin x.asm -o x.com -l x.lst`; non-zero exit on errors; no automatic long
  conditional jumps; linear instruction search so slow; BSD-2-Clause — [github.com/nanochess/tinyasm](https://github.com/nanochess/tinyasm);
  packaged in FreeDOS devel as "Small assembler for 8086/8088 programs", Simplified BSD —
  [FreeDOS devel listing](https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/pkg-html/group-devel.html)
- **Arrowsoft Assembler**: accepts 8086/8088/80186/80286 source, outputs relocatable object modules; syntax "a superset
  of that accepted by the Microsoft Macro Assembler (versions 3.0 and later)" —
  [Vector.co.jp listing](https://vector.co.jp/soft/dos/prog/se014771.html); v2.00c listed as public domain, MASM 3.0
  compatible — [fysnet links](https://fysnet.net/links.htm); ARROWASM.ARC listed as Public Domain on a Simtel mirror —
  [Simtel Jun-1992 ASMUTL](https://web.synchro.net/files/Simtel.Jun-1992/ASMUTL); described as freeware whose
  associated linker is VAL — [idallen teaching notes](https://teaching.idallen.com/dat2343/09f/notes/28AssemblerTools.htm)
- **JWasm / JWASMR**: "JWASMR.EXE is a DOS real-mode program which runs on any x86 cpu. ... it needs a MS-DOS v5
  compatible DOS"; has restrictions to limit memory and "might be unable to assemble large sources" —
  [JWasm docs mirror](https://gitee.com/ThirdPartProject/JWasm); FreeDOS package licence: Sybase Open Watcom Public
  License 1.0 — [FreeDOS devel listing](https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/pkg-html/group-devel.html);
  WASMR/JWASMR ~4–4.5 min on the 2000-line 8086 test (WASMR /zq 275 s) —
  [bttr timing thread](https://bttr-software.de/forum/mix_entry.php?id=23405)
- **FASM**: DOS build checks CPU and prints "required 80386 or better" —
  [fasm source modes.inc](https://fossies.org/linux/fasm/source/dos/modes.inc); "DOS 2.0+ w/386+" —
  [fysnet](https://fysnet.net/links.htm). EXCLUDED.
- **a72/RA 1.05C** ~108 s; **as86 (dev86)** ~50 s (assembly only) on the 8086 test —
  [bttr timing thread](https://bttr-software.de/forum/mix_entry.php?id=23405)
- **AP.EXE (asmpp)** uses PUSH imm and near Jcc: does not run on 8086 — EXCLUDED —
  [bttr timing thread](https://bttr-software.de/forum/mix_entry.php?id=23405)
- **MASM**: 1.00–1.27 "generic 8088, 8087 and 8086 assemblers"; 3.00 added 286 protected-mode instructions; 5.1 (1988)
  first with OS/2 host — [EDM2 MASM](https://www.edm2.com/index.php/MASM); WinWorld lists MASM 5.x minimum CPU 8088 —
  [WinWorld MASM 5.x](https://winworldpc.com/product/macro-assembler/5x); MASM 5.10 ~55 s on the 8086 test —
  [bttr timing thread](https://bttr-software.de/forum/mix_entry.php?id=23405); MASM 6.0 needs DOS 3.0+ or OS/2 1.1+,
  640K RAM, 4 MB disk — [MASM 6.0 install guide (bitsavers)](https://ftp.mirrorservice.org/sites/www.bitsavers.org/pdf/microsoft/masm/Microsoft_MASM_6.0_1991/Installing_and_Using_the_Professional_Development_System_Ver_6.0_Feb91.pdf);
  MASM 6.1 needs MS-DOS 3.3+ — [MASM 6.1 Getting Started](https://ftp.mirrorservice.org/sites/www.bitsavers.org/pdf/microsoft/masm/Microsoft_MASM_6.1_1992/MASM_6.1_Getting_Started_1992.pdf)
- **TASM**: "TASM.EXE is the real-mode assembler. It uses 640K DOS memory"; "TASMX.EXE ... can use extended memory above
  640K" — [Swan, Mastering Turbo Assembler 2ed](https://ftp.mirrorservice.org/sites/www.bitsavers.org/pdf/borland/turbo_assembler/Swan_Mastering_Turbo_Assembler_2ed_1995.pdf);
  TASM32.EXE (Win32 PE) shipped with 5.0+ — [Wikipedia Turbo Assembler](https://en.wikipedia.org/wiki/Turbo_Assembler)

### Inferences
- Best free MASM-syntax choice on the DOS PC: JWASMR (modern, maintained) if DOS 5-compat (FreeDOS is) and source sizes stay
  small; Wolfware Asm is ~6x faster though.
- Best free NASM-syntax choice: TinyASM (single command line, .COM via `-f bin`, error exit code — good for an agent).
- MASM 6.x: ML.EXE is DOS-extended (6.0 Phar Lap 286 / 6.1 386 extender) — (unverified / from memory; the install guides
  above should be read to confirm). Treat MASM 6.x as excluded; MASM 4.0/5.1 are the commercial 8088 sweet spot.
- TASM 1.0–3.x TASM.EXE is widely believed to run on 8088 (unverified / from memory); TASMX (3.x) needs 286+ DPMI/
  extended memory; TASM 5's TASM32 needs Win32. If the user owns TASM 2.x, it's likely the most capable 8088 assembler
  with MASM compatibility (and IDEAL mode) — confirm by running it on the DOS PC.

### Gaps
- CHASM (Whitman/David Whitman's Cheap Assembler) — no source fetched; licence/8088 status unconfirmed (it was a 1980s
  IBM-PC shareware assembler, so 8088 host is very likely).
- Pass32 — no source found; assumed 386+ (it was a 32-bit DOS-extender oriented assembler) — unverified.
- Wolfware Asm FreeDOS package page (version, docs) could not be fetched; its 8088 host status rests on the NTVDM-8086
  timing estimate.
- `msa` (FreeDOS devel, MIT) — description page not retrievable; CPU requirement unknown.
- Exact TASM/MASM 6.x host-CPU minimums from original manuals.

## 4. Linkers, librarians, EXE2BIN, linker-free .COM output

### Takeaway
WarpLink (public domain, with source, overlays, actively maintained by ecm in 2025) is the strongest free 8088-era linker.
VAL is a small free 16-bit linker. ALINK's host requirements are unclear. Linker-free .COM output is available from A86,
TinyASM (`-f bin`), and NASM (`-f bin`). Commercial MS LINK (3.x–5.x) and TLINK from 8088-era packages are real-mode.

### Cited Findings
- WarpLink: "released to public domain and all copyright restrictions are removed by the original copyright holder,
  Michael Devore"; DOS linker with dynamic overlays — [WarpLink manual WLMAN.DOC](https://pushbx.org/ecm/doc/WLMAN.DOC);
  v2.7 source wl27src.zip (~978 KB) released PD in 1999 — [phatcode downloads](https://www.phatcode.net/downloads.php?id=476);
  current "release 4 by ecm (2025 September)" — [pushbx blog 2026](https://pushbx.org/ecm/dokuwiki/blog/pushbx/2026/0204_chasing_a_warplink_bug)
- VAL: free, produces DOS EXE, "can ONLY handle 16bit code" — [OSDev wiki VAL](https://wiki.osdev.org/VAL); paired with
  Arrowsoft ASM — [idallen notes](https://teaching.idallen.com/dat2343/09f/notes/28AssemblerTools.htm)
- ALINK (Anthony Williams, alink.sourceforge.net) handles OMF and COFF — [masmforum](https://masmforum.com/board/index.php?msg=121552)
- A86 makes .COM directly — [A86 manual](https://pic.hallikainen.org/techref/language/a86/A86MANU.htm);
  TinyASM `-f bin` — [tinyasm](https://github.com/nanochess/tinyasm)

### Inferences
- WarpLink is written in assembly from 1989–93, so it almost certainly runs on 8088 (inferred, not confirmed).
- EXE2BIN: FreeDOS ships a free EXE2BIN in its base/util set (unverified / from memory).
- Librarians: MS LIB from MASM 1–5.1 (commercial, real mode); free options not researched.

### Gaps
- ALINK host CPU (built with a 32-bit compiler? unknown). VAL licence text and download source. Microsoft LINK / TLINK
  version-by-version CPU requirements (LINK 5.x+ from C 6/7 era may be DOS-extended — unverified). A free 8088 librarian.

## 5. Debuggers and scripted (redirected) input

### Takeaway
FreeDOS DEBUG (Paul Vojta / Debug/X by Japheth) defaults to 8088, its 386-only features are optional, and it explicitly
handles `DEBUG < SCRIPT` reaching EOF without `q`. Licence: "feel free to distribute and use it as you wish" (class a/b).
lDebug (Fair License, "intended to run on any processor type down to the 8088/8086") adds script files (Y command) and
robust EOF handling; it is the most capable scriptable debugger for the agent. DEBUGX is the DPMI build (no use on an 8088).
D86, Insight, AFD are interactive full-screen tools — low value for a no-keyboard agent.

### Cited Findings
- FreeDOS DEBUG: default processor 8088; 32-bit registers, `rx`, `id`/`od` DWORD, `dx` need 386 —
  [Vojta DEBUG 1.25 README](https://sunsite.icm.edu.pl/packages/freedos/dos/debug/vojta/1.25/README.TXT)
- Licence: "This program is copyrighted, but feel free to distribute and use it as you wish" (see debug.asm) —
  [DEBUG README](https://sunsite.icm.edu.pl/packages/freedos/dos/debug/vojta/1.25/README.TXT),
  [Debug/X GitHub](https://github.com/Baron-von-Riedesel/DOS-debug)
- Redirect: "When doing `debug < file', debug will not hang if it reaches an end of file before encountering a `q'
  command" — [Debug/X GitHub](https://github.com/Baron-von-Riedesel/DOS-debug)
- DEBUGX is the DPMI-aware version; protected-mode debugging requires a DPMI host —
  [DEBUG README](https://sunsite.icm.edu.pl/packages/freedos/dos/debug/vojta/1.25/README.TXT)
- MS-DOS DEBUG (and MSDebug) reading a redirected file: "it is crucial to include a Q command in the file"; may hang at
  EOF — [MSDebug manual](https://pushbx.org/ecm/doc/msdebug.htm)
- lDebug: "Script files can be run with the Y command"; "Do not crash or loop infinitely upon encountering the end of a
  stdin-redirected file"; 686-level assembler/disassembler; licence changed to Fair License ("Usage of the works is
  permitted provided that this instrument is retained") — [lDebug manual](https://pushbx.org/ecm/doc/ldebug.htm);
  release announcement: programs "intended to run on any processor type down to the 8088/8086 level" —
  [search summary of bttr announce](https://bttr-software.de/forum/mix_entry.php?id=17816); FreeDOS devel lists lDebug,
  Fair License — [FreeDOS devel listing](https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/pkg-html/group-devel.html)
- Insight Debugger: "Real-mode 16-bit DOS debugger", GPL v2 —
  [FreeDOS devel listing](https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/pkg-html/group-devel.html)

### Inferences
- The agent's existing `DEBUG < SCRIPT.TXT` workflow is sound with FreeDOS DEBUG; switching to lDebug would add
  expression evaluation in the assembler and SHORT/NEAR/FAR jump control.
- SYMDEB (MASM 3.x–4.0 era, commercial) is 8088 real mode and line-oriented, so likely redirectable (unverified).

### Gaps
- Insight's minimum CPU (it may need 186+) — page not retrievable. AFD (Advanced Fullscreen Debugger) licence/CPU — not
  researched. D86 host CPU not stated by the manual.

## 6. FreeDOS 1.4 devel packages that run on an 8088

### Takeaway
Of the 54 FreeDOS devel packages, the 8088-capable ones for this toolchain are: TinyASM, Wolfware Asm, JWasm (JWASMR
binary), NASM only via the 8086host rebuild, lDebug, (FreeDOS DEBUG is in base), bcc (Bruce's C, outputs 8086 asm —
host CPU unconfirmed), Insight (real mode, CPU unconfirmed). Everything DJGPP-, FreeBASIC-, FreePascal-, FASM-,
IA-16-GCC- (DJGPP-hosted) and DOjS-based is 386+ and excluded. Open Watcom's DOS host tools are 386 DOS-extended
(unverified).

### Cited Findings
- Full devel list with licences: bcc (GPL v2), DJGPP family (GPL), flat assembler (BSD), FreeBASIC, FreePascal,
  ia-16 gcc "on a DJGPP host" (GPL v3), Insight (GPL v2), JWasm (Sybase OWPL 1.0), lDebug (Fair), msa (MIT), Nasm
  (BSD-2), Open Watcom C/Fortran (OWPL), TinyASM (BSD-2), Wolfware Asm (MIT), upx (GPL v2), runtime (PD), Regina REXX
  (LGPL), Lua (MIT), Bywater BASIC (GPL v2), Euphoria, Perl, SQLite —
  [FreeDOS devel group listing](https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/pkg-html/group-devel.html)

### Inferences
- "ia-16 gcc on a DJGPP host" means the compiler itself needs a 386 even though it targets 8086.
- Bywater BASIC, Lua, Regina may have 16-bit builds, but are out of this note's scope.

### Gaps
- Per-package CPU requirements for bcc, Insight, msa, Wolfware (ibiblio 503 blocked the individual package pages).

## Summary table (for the report writer)

| Tool | Runs on 8088? | Licence class | Non-interactive | Speed (2000-line test, est. 4.77 MHz) |
|---|---|---|---|---|
| A86 4.x | Yes (Portfolio/NTVDM evidence) | (b) shareware, register to keep using | Yes, .COM direct | ~5 s |
| Wolfware Asm | Yes (NTVDM-8086 estimate) | (a) MIT | Yes | ~42 s |
| TinyASM | Yes (author: real 8088 PC, 128K) | (a) BSD-2 | Yes, exit code | slow (no figure) |
| JWASMR | Yes ("any x86", DOS 5) | (a) OWPL | Yes | ~4–5 min |
| NASM 0.98.39 8086host | Barely (memory) | (a) | Yes | ~7 min |
| Arrowsoft 2.00c + VAL | Yes (targets 8088, MASM 3 syntax) | (a)/(c) PD/freeware | Yes | n/a |
| MASM 1.0–5.1 | Yes | (d) | Yes | 5.10 ~55 s |
| MASM 6.x | No (DOS extender, unverified) | (d) | — | — |
| TASM 1.x–3.x TASM.EXE | Likely (unverified) | (d) | Yes | n/a |
| FASM, AP.EXE, NASM 2.x | No (386+/186+) | — | — | — |
| WarpLink | Likely (asm, 1989–93) | (a) PD | Yes | n/a |
| FreeDOS DEBUG | Yes (386 features optional) | (a/b) "use as you wish" | Yes, `< script` safe at EOF | — |
| lDebug | Yes (stated 8088/8086) | (a) Fair License | Yes, Y scripts, EOF-safe | — |
| D86, Insight, AFD | D86 likely; others unknown | (b)/(a)/? | No (full-screen) | — |
