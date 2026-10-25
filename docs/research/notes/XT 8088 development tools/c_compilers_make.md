# C compilers and MAKE utilities that run ON an IBM PC XT (8088 @ 4.77 MHz, 640 KB, FreeDOS 1.4)

Scope note: the question here is whether the compiler binary itself runs on an 8088 in real mode, not just whether it emits 8086 code. The agent drives tools through a single "run" command line (about 120 characters, no keyboard input, stdout captured), so command-line drivers matter most. About 30 tool calls were spent. Several primary pages could not be fetched: dunfield.classiccmp.org refused the connection, VOGONS returned 403, and the Embarcadero item page returned 404. Gaps are marked where that happened.

Licence classes:
- (a) free/open source, redistributable
- (b) freeware with redistribution explicitly allowed
- (c) free download, but licence unclear or redistribution not granted
- (d) commercial, needs an original licensed copy

## Pacific C (HI-TECH Software)

### Takeaway
Pacific C is the strongest free option and fits class (b). HI-TECH explicitly allowed redistribution as long as the complete package ships with its copyright notices intact. It is an ANSI C compiler for MS-DOS with a command-line driver, PACC.EXE. I could not find a primary statement that the compiler itself runs on an 8088, and none of the sources I found gives memory or disk needs.

### Cited Findings
- HI-TECH's Clyde Smith-Stubbs wrote to fd-dev@list.freedos.org on 17 June 1998: "Permission is hereby granted to use Pacific C for the FreeDOS project or any other private or commercial use" and "Pacific C may be redistributed alone or with other software providing the complete package is distributed and the copyright notices remain intact." The permission is stated to cover the current version until a new one is posted. — [FreeDOS mirror readme.txt](https://rsync.rediris.es/mirror/freedos/files/devel/c/pacific/readme.txt)
- The same message refers to the command-line driver pacc.exe. — [FreeDOS mirror readme.txt](https://rsync.rediris.es/mirror/freedos/files/devel/c/pacific/readme.txt)
- The freeware edition's readme calls it a "Freeware Version". The latest release note is V7.51. It is described as an ANSI C compiler for MS-DOS that also runs in a Windows DOS box. The IDE is PPD. The readme says VGA is needed for 50-line mode and EGA for 28/43-line modes, which implies the IDE also runs in 25 lines. It gives no CPU, RAM or disk requirement. — [FreeDOS mirror freeware/readme.txt](https://ftp.icm.edu.pl/packages/freedos/devel/c/pacific/freeware/readme.txt)
- The V7.51 notes mention fixing an fsub/fsubr assembler bug, so x87 floating point is supported in some form. — [freeware/readme.txt](https://ftp.icm.edu.pl/packages/freedos/devel/c/pacific/freeware/readme.txt)
- The C FAQ's compiler list describes Pacific C as shareware with optional registration for non-commercial use. That is an older characterisation and conflicts with the later freeware release. — [c-faq.com](https://c-faq.com/resources/compilers.html)

### Inferences
- The 1998 permission predates the V7.51 freeware readme. Redistributing the unmodified freeware archive seems covered in spirit, but the exact version the grant covers is ambiguous. Ship the complete archive untouched, with the readme.
- My unverified recollection is that Pacific C is commonly used on 8088 machines and supports small and large memory models. This needs confirming by running PACC.EXE on 86Box with an 8088 machine (`scripts/run86box.py`), since no primary source was found.

### Gaps
- Host CPU requirement, RAM needed, disk footprint, list of memory models, and float emulation details were not found in primary sources. The Pacific C manual (PACIFIC.DOC/PDF) was not located online.
- No compile-time figures for an 8088 were found.

## Turbo C 2.01 and Turbo C++ 1.01 (Borland "Antique Software")

### Takeaway
Both run on an 8088. WinWorld lists Turbo C 2.x's minimum CPU as an 8088, and the Turbo C++ 1.0 catalog record says it runs on XT/AT/PS/2 in 640K. Borland's "antique software" terms are personal use only, and a quoted notice forbids redistribution over the Internet or on media. That puts the free download in class (c): fine for the user's own machine, but it cannot go on a disk image we ship. The user's own original disks would fall under (d) and are equally usable.

### Cited Findings
- The Borland/Embarcadero antique-software notice says the files are free and may be used "as is" for personal use only. There is no developer support, and the copyright notices are "still in force". The listing is marked "All rights reserved". A free registration is required to download. — [Embarcadero CodeCentral item 26014 (Turbo C++ 1.01)](https://cc.embarcadero.com/Item/26014). The page now redirects to my.embarcadero.com and returned 404 when fetched, so this rests on search-indexed text.
- A 2003 forum repost of the Borland notice quotes: "These files may not be made available via the Internet or any hard copy media (e.g. diskette, CDROM)." — [Delphi-PRAXIS repost](https://www.delphipraxis.net/27397-post18.html). This is a secondary quote; I could not confirm it on the live Embarcadero page.
- Wikipedia says Borland made Turbo C and the MS-DOS Turbo C++ compilers "freeware" in 2000, and that downloads are on Embarcadero's site. Its infobox still lists the licence as "Proprietary". Turbo C 1.0 "ran in 384 kB of memory". Turbo C 1.5 shipped on five 360 KB diskettes. — [Wikipedia: Turbo C](https://en.wikipedia.org/wiki/Turbo_C)
- WinWorld lists Turbo C 2.x (1988; 2.01 is labelled 1989) with "Minimum CPU: 8088", in 360K and 720K disk sets. — [WinWorld Turbo C 2.x](https://winworldpc.com/product/borland-turbo-c/2x)
- Turbo C++ 1.0's 1990 catalog record says it runs on XT, AT and PS/2 machines and needs DOS 2.0+ and 640K. — [Computer History Museum catalog](https://www.computerhistory.org/collections/catalog/102777078). Caveat: this is the record ID returned by search for Turbo C++ 1.0 and was not opened directly.
- Speed anecdote: on a "Pocket 8088" at 4.77 MHz, a VOGONS poster timed a Hello World build at about 8 seconds with Turbo C 2.0 and 1 min 27 s with Turbo C++ 1.0. They attributed most of the time to standard library code being pulled in. — [VOGONS thread](https://www.vogons.org/viewtopic.php?p=1369312). The page returned 403 when fetched, so the figures come from the search summary and are unverified first-hand. Storage type (SD/CF versus floppy) is unknown.

### Inferences
- From my own knowledge, not fetched: Turbo C 2.0 ships TCC.EXE (command-line compiler), TLINK.EXE, MAKE.EXE, TLIB and the TC IDE. TCC is a good fit for a one-line "run" tool, for example `TCC -ms -O foo.c`. It supports tiny through huge models, ANSI C (C89 draft era) and floating-point emulation. The 8 s versus 87 s timing suggests Turbo C 2.0 is far more practical on a 4.77 MHz XT than Turbo C++ 1.0.
- Since the user owns original IBM PC software, they can install from their own disks under (d) and avoid the antique-licence question entirely.

### Gaps
- The exact Turbo C 2.01 antique-software page text (as opposed to TC++ 1.01) could not be fetched.
- Turbo C 2.0 RAM and disk requirements were not found in a primary source.

## Microsoft C, QuickC, Borland C++ 2.0/3.x (commercial, class d)

### Takeaway
Microsoft C 6.0's own install guide gives an 8088 as the minimum CPU and 512K free RAM under DOS, though it needs 8 MB or more of disk. That makes it the most capable commercial compiler with documented 8088 support, and NMAKE is a command-line tool. Borland's 3.x line (Turbo C++ 3.0 and BC++ 3.1) is reported to require an 80286, so treat it as excluded unless tested.

### Cited Findings
- Microsoft C 6.0 (1990, Professional Development System) install guide: "An 8088 processor (a minimum of an 8 megahertz, 80286 processor is recommended)". It needs "512K (kilobytes) of available memory (RAM) for operating under DOS (1 megabyte of RAM is recommended)" and a hard disk with at least 8 MB free (up to 20 MB). NMAKE is included (`NMAKE /F IO.MAK`; it defaults to MAKEFILE). CodeView under DOS wants HIMEM.SYS and 384K of extended memory, which an XT does not have. — [PCjs: MS C 6.0 Installing and Using](https://www.pcjs.org/documents/books/mspl13/c/cinstall/)
- QuickC 2.0 (January 1989) is described as MS C 5.1 compatible. Its "Up and Running" manual has a System Requirements section, but I did not read it. — [Wikipedia: QuickC](https://en.wikipedia.org/wiki/QuickC); [QuickC 2.0 Up and Running PDF (bitsavers mirror)](https://ftp.mirrorservice.org/sites/www.bitsavers.org/pdf/microsoft/msdos_c/quickc/410840040-200-R00-1088_Microsoft_QuickC_Up_and_Running_Version_2.0_198810.pdf); [WinWorld QuickC 2.x](https://winworldpc.com/product/quick-c/2x)
- A 1991 trade item says Turbo C++ 3.0 needs MS-DOS 3.3+ on an 80286. — [Tech Monitor](https://www.techmonitor.ai/?p=165824). This is a secondary source.
- DeSmet C88 output was strictly 8086/88 instructions, according to a 1988 Usenet review. — [comp.lang.c 1988](https://www.tuhs.org/Usenet/comp.lang.c/1988-November/043352.html)

### Inferences
- Unverified knowledge, to test on 86Box before relying on it:
  - MS C 5.1 and QuickC 1.0–2.5 also run on an 8088 (the QCL command line exists).
  - Turbo C++ 1.0/1.01 and Borland C++ 2.0 BCC.EXE are real-mode and probably run on an 8088 with 640K.
  - BC++ 3.x's IDE uses a DOS extender that needs a 286, while the 3.x command-line compilers may still have a real-mode variant.
  - Lattice C 2.x/3.x, Aztec C86, Mix Power C and Zortech C++ 1.x/2.x were sold for PC/XT-class machines.
- MS C 6.0 is very slow on a 4.77 MHz XT. It is a multi-pass compiler (C1/C2/C3), so expect minutes per file, though no benchmark was found. Turbo C 2.0 or QuickC is a better speed fit.

### Gaps
- Primary CPU and RAM specs for MS C 4.0/5.1, QuickC 1.0/2.5, Borland C++ 2.0/3.1, Lattice, Aztec, Mix Power C and Zortech were not retrieved.

## Dunfield Micro-C (Dave Dunfield)

### Takeaway
Micro-C for DOS (MCDOS.ZIP, 220.6 KB) is now freely downloadable, with source, under a short permissive notice ("released for anyone to use for any reasonable purpose", with attribution requested). It is effectively class (a) or (b), although the notice does not explicitly mention redistribution. It is a K&R-ish subset compiler that runs on IBM PCs and produces 8086 code. A tiny footprint makes it a strong XT candidate. A "DdsBuilDer" build helper and Borland TASM are offered on the same page.

### Cited Findings
- COPY.TXT: "The files contained is this archive are hereby released for anyone to use for any reasonable purpose." For published material it asks you to "Acknowlege the original author (Dave Dunfield)" and to say where to obtain the original. It also says "I do NOT require release of derived source code!" and disclaims liability. The file "may be updated from time to time". — [Dunfield COPY.TXT](https://dunfield.themindfactory.com/dnld/sc/COPY.TXT)
- The download page lists: Micro-C DOS/Win32, MCDOS.ZIP, 220.6k; DdsBuilDer, DBD.ZIP, 22.3k ("a tool to simplify building with Micro-C"); Borland TASM, TASM.ZIP, 90.8k (see its 00README.TXT); and an EDT editor. No separate MAKE is listed. — [Dunfield downloads](https://dunfield.themindfactory.com/dnld.htm)
- Source releases are at dnldsrc.htm. A forum reader judged the terms "liberal but not quite OSS". — [BTTR forum 2023](https://bttr-software.de/forum/mix_entry.php?id=20819)
- The Micro-C 3.15 PC readme says it produces code for the 8086 family, runs on IBM PCs, and needs a MASM-compatible assembler and linker. — [mc315pc.txt (FreeDOS mirror)](https://sunsite2.icm.edu.pl/packages/freedos/devel/c/micro-c/mc315pc.txt)
- Older 3.15 terms: free to use indefinitely, $25 registration suggested, copyright reserved, redistribution limited to unmodified archives. — [BTTR forum 2011](https://bttr-software.de/forum/forum_entry.php?id=9372)

### Inferences
- The TASM.ZIP that Dunfield offers is Borland code. Its redistribution status depends on its own 00README and is unverified, so do not assume it is shippable. Micro-C's own toolchain (MCDOS) has historically included its own assembler/linker (ASM/LINK, unverified). Check the contents of MCDOS.ZIP.
- Micro-C lacks parts of ANSI C (for example, typedef and structure features are limited in the classic versions; my recollection, unverified). It is good for small utilities but a poor target for porting ANSI code.

### Gaps
- The contents of MCDOS.ZIP (whether it bundles its own assembler, linker and MAKE) and the exact C subset were not verified.

## Dev86 / BCC, Smaller C, DeSmet C, and other open-source options

### Takeaway
- Dev86 BCC has a 16-bit OpenWatcom-hosted DOS build, which should be real-mode. Its 8086 compatibility is unconfirmed, it targets tiny/small models only, and the licence is GPL (a).
- Smaller C (BSD-2) emits 80386 code even for 16-bit output, so its output will not run on an 8088. It is excluded as an XT toolchain.
- DeSmet C88 was reportedly GPL'd in 2005 and generates strict 8086 code. It is a promising (a) candidate, but the release archive was not located.

### Cited Findings
- Dev86 BCC 0.16.21 and 1.0.1 have Watcom-hosted 16-bit and 32-bit DOS builds. The 32-bit build needs HXRT. The compiler is a "Simple C compiler that produces 8086 assembler for tiny/small models" and is "too large to compile itself". The builds are hosted on a gopher server (January 2026). — [BTTR forum](https://www.bttr-software.de/forum/mix_entry.php?id=23110)
- An older DJGPP-hosted Dev86DOS 0.16.18 (which needs a 386) supports usage like `bcc -v -O -ansi myfile.c -o myfile.com`. — [BTTR forum 2012](https://bttr-software.de/forum/forum_entry.php?id=11370). The older DOS-hosted 0.16.2 is minimal, producing small-model (64k+64k) .COM output only. — [BTTR forum](https://bttr-software.de/forum/forum_entry.php?id=11236)
- Smaller C "generates 16-bit and 32-bit 80386+ assembly code for NASM". It supports most of the C common to C89 and C99 and is licensed BSD-2-Clause. — [SmallerC GitHub](https://github.com/alexfru/SmallerC). The DOS huge model uses 32-bit ints and pointers. — [BTTR Smaller C announcement](https://bttr-software.de/forum/forum_entry.php?id=13772)
- DeSmet C was released under the GPL, per a 2005 Slashdot story ("Old C Compiler Lives Again Under GPL"). — [Slashdot](https://developers.slashdot.org/story/05/08/03/1810211/old-c-compiler-lives-again-under-gpl). Its output is strictly 8086/88. — [Usenet 1988](https://www.tuhs.org/Usenet/comp.lang.c/1988-November/043352.html)

### Inferences
- Because Smaller C's generated code uses 386 instructions and registers, even a DOS-hosted smlrc.exe would itself be built with 386 code, so it would not run on an 8088. This is inferred and not confirmed by its docs, but it is consistent with the "80386+" statement. Exclude it.
- The 16-bit BCC build was compiled with OpenWatcom. If it was built with `-0` it would run on an 8088, but that is untested. It is also the most actively maintained open-source option, so it is worth a quick 86Box test.
- Not researched for lack of budget: Small-C (Cain/Hendrix; public domain, classically 8086-hosted), Small-C/Plus, ACK, SubC and T3X. Of these, Small-C is very likely 8088-hosted and class (a), but that is unverified.
- Open Watcom, DJGPP, GCC-ia16 hosted on DOS, and 32-bit Smaller C need a 386 or DPMI host. Exclude them as XT-hosted tools; they remain fine as cross-compilers on the PC, which is how this repo already builds.

### Gaps
- Primary sources for Small-C, ACK, SubC and T3X (DOS 8086-hosted builds and licences) are missing.
- The location and licence text of the DeSmet C GPL source archive were not found. I believe it is the "PCC" / DeSmet C 3.1 package hosted at desmet-c.com (unverified).

## MAKE utilities on the 8088

### Takeaway
Borland MAKE (bundled with Turbo C, class d/c) and Microsoft NMAKE (bundled with MS C 6.0, class d) are command-line tools that ship with 8088-capable compilers. No freely redistributable 8088 MAKE was confirmed in this research. For an agent driving one command line at a time, a batch file (or Dunfield's DdsBuilDer) is an adequate substitute.

### Cited Findings
- NMAKE ships with MS C 6.0, which itself documents an 8088 minimum. NMAKE uses `NMAKE /F file`, or MAKEFILE by default. — [PCjs MS C 6.0 guide](https://www.pcjs.org/documents/books/mspl13/c/cinstall/)
- Dunfield offers DBD.ZIP, "a tool to simplify building with Micro-C". — [Dunfield downloads](https://dunfield.themindfactory.com/dnld.htm)

### Inferences
- A batch file such as `BUILD.BAT` that calls `TCC`/`PACC` with fixed arguments works well within the 127-character DOS command-line limit. It avoids MAKE's memory overhead, because MAKE stays resident while spawning the compiler, which costs RAM on a 640K machine. For small projects, recompiling everything is cheap compared with the risk of a confusing build.
- Unverified candidates I did not research: NDMAKE (shareware, Don Kneller), Opus Make (commercial), and DJGPP-built GNU make (needs a 386, so excluded).

### Gaps
- No licence or 8088 data was retrieved for NDMAKE, Opus Make, or a 16-bit GNU make port.

## Compile speed on a 4.77 MHz 8088

### Takeaway
The only hard data point found: Turbo C 2.0 builds Hello World in about 8 s, while Turbo C++ 1.0 takes about 87 s on a 4.77 MHz Pocket 8088. Single-pass and lightweight compilers (Turbo C 2.0, Pacific C, Micro-C) are the practical choices.

### Cited Findings
- Turbo C 2.0 took about 8 s and Turbo C++ 1.0 took 1:27 for Hello World on a 4.77 MHz Pocket 8088. — [VOGONS](https://www.vogons.org/viewtopic.php?p=1369312) (seen via search summary; 403 on direct fetch)
- A 10 MHz NEC V30 is about 3–4 times faster than a 4.77 MHz 8088. — [VOGONS](https://www.vogons.org/viewtopic.php?p=1369312) (same caveat; the exact post is uncertain)

### Inferences
- Recommended shortlist to test on 86Box with an XT config:
  1. Pacific C (shippable, ANSI, PACC command line).
  2. Turbo C 2.01 from the user's own disks, or the personal-use download kept off the shipped image (fast, TCC command line).
  3. Micro-C (tiny, permissive).
  4. Dev86 BCC 16-bit (GPL; 8088 compatibility untested).
- Putting the compiler and its temp files on a RAM disk in EMS would likely help a lot on an XT with a slow disk. That is a guess with no benchmark found.

### Gaps
- No benchmarks were found for Pacific C, Micro-C, MS C or QuickC on an 8088.
