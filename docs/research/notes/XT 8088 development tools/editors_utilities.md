# Text editors and development utilities that run on an IBM PC XT (8088, 640 KB, CGA, FreeDOS 1.4)

Research note: ibiblio.org (the primary FreeDOS file host, including the per-package LSM pages and the FreeDOS 1.4 `report.html` that tags every package's CPU class) returned HTTP 503 throughout this session, and an archive.org copy could not be fetched. So the per-package CPU tags below come from search-engine snippets of that report, not from a full read of it. Items marked **[unverified]** come from background knowledge, not from a source fetched in this session. Verify them before relying on them.

## What FreeDOS 1.4 says about 8086 support, and which packages need a 386

### Takeaway
The FreeDOS 1.4 release announcement says nothing about CPU requirements. The machine-readable source is the distribution report (`distributions/1.4/report.html`), which tags each package with a minimum CPU class ("all", "286+", "386+"). EDIT and EDLIN are tagged "all". The memory managers and, importantly, **UNZIP** are tagged "386+". FreeDOS maintainers say the kernel and FreeCOM run on an 8088 but not every bundled program does.

### Cited Findings
- The FreeDOS 1.4 announcement (April 2025) covers dropping Seal/oZone, replacing BMP2PNG with DOSVIEW, and updating Xcopy 1.8b, Move 3.4 and Fdisk 1.4.3. It says nothing about CPU requirements or about EDIT/EDLIN/FC/FIND/SORT. — [FreeDOS 1.4 announcement](https://freedos.org/download/announce.html)
- In the FreeDOS 1.4 report, packages tagged "386+" include **fdxms, himemx, jemm, lbacache, shsucdx and unzip**. `edit` (130.8 KiB, BASE, FULL) and `edlin` (294.3 KiB, BASE, FULL) are tagged "all". This is from a search-engine snippet; the page itself could not be loaded. — [FreeDOS 1.4 report](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/report.html)
- The FreeDOS 1.3 report uses the same scheme and also has a "286+" class (e.g. `fdxms286`). — [FreeDOS 1.3 report](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official/report.html)
- FreeDOS 1.2 shipped EDLIN 2.10c and FreeDOS Edit 0.9a. — [FreeDOS 1.2 package comparison](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.2/official/compare.html)
- On a VCF forum thread, the FreeDOS kernel and FreeCOM 8086 builds booted on an 8088. A maintainer comments that not every program shipped with FreeDOS works on an 8088, but the base OS should. — [VCFed "FreeDOS on 8088"](https://forum.vcfed.org/index.php?threads/freedos-on-8088.42683/)
- An 86Box issue reports that on an emulated 8086/8088 machine, "FreeDOS Edit and MEM external command locks the system". This is an emulator report from an older version and not confirmed on real hardware. — [86Box issue #249](https://github.com/86Box/86Box/issues/249)
- A VOGONS thread on choosing a FreeDOS for an 8088 XT notes that an 8086 FAT32 kernel build exists and mentions Svarog86, a single-floppy FreeDOS distribution aimed at 8088 machines. — [VOGONS "Good version of FREE DOS for 8088 XT"](https://www.vogons.org/viewtopic.php?t=77546)

### Inferences
- "all" in the report seems to mean "no 386 instructions or DPMI known". It is not a tested-on-8088 guarantee: EDIT is tagged "all", yet there is the 86Box lock-up report. Treat "all" as "should work, test it".
- UNZIP in FreeDOS 1.4 is tagged 386+, most likely because it is a DJGPP (32-bit) build of Info-ZIP **[unverified that it is DJGPP]**. For the XT, ship a 16-bit Info-ZIP DOS build instead (see the archivers section).
- The memory-manager packages tagged 386+ (JEMM, HIMEMX, FDXMS) are irrelevant on an XT anyway. EMS on the XT comes from the PicoMEM's own LIM driver.

### Gaps
- I could not read the full 1.4 report, so I lack the CPU tag for fc, find, sort, more, comp, sed, grep, diff, gawk, less, vim, elvis, ted3, setedit, fed, pico, nano, zip, 7zip, arj, lha, debug, insight, nasm, tde, blocek, mtcp, wget, curl and so on. **Action:** when ibiblio is back up, grep `report.html` for "386+" and "286+". Each package's `appinfo/*.lsm` in its zip may also carry hints.
- I could not confirm whether FreeDOS 1.4's EDIT is still 0.9a or a newer build, or whether it has been tested on a real 8088.

## Editors (mainly for the human at the keyboard; EDLIN is the agent-scriptable one)

### Takeaway
For the agent, **EDLIN** is the only editor that works non-interactively (commands piped in by redirection, `EDLIN FILE.TXT < CMDS.TXT`). Even so, the agent's own file read/write tools make editors mostly unnecessary. For the human, the best-supported small 8088-friendly editors are **VDE** (assembly, about 80 KB, freeware, explicitly 8088- and palmtop-friendly), **TED/SuperTED** (tiny), FreeDOS EDIT (GPL, tagged "all" but with one lock-up report), and old 16-bit builds of MicroEMACS, Freemacs and JED from GNUish. Vim and TDE in modern DOS form are 32-bit builds.

### Cited Findings
- **FreeDOS EDLIN**: tagged "all" CPU class in the FreeDOS 1.4 report (BASE package). — [FreeDOS 1.4 report](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/report.html). Edlin is the classic line editor that was "the only text editor provided with early versions of IBM PC DOS, MS DOS and OS/2", and FreeDOS ships its own clone. — [Wikipedia: Edlin](https://en.wikipedia.org/wiki/Edlin)
- **FreeDOS EDIT**: tagged "all" in the 1.4 report. — [FreeDOS 1.4 report](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/report.html). Reported to lock up on emulated 8086/8088. — [86Box issue #249](https://github.com/86Box/86Box/issues/249)
- **VDE (Eric Meyer)**: since December 2002 it is "copyrighted freeware". It "may be freely used and distributed in complete original form, but not modified or sold for profit without written permission of the author". The registered-user spelling checker may not be distributed as freeware. The current manual requires DOS 3.x+. — [VDE 1.97 manual](https://sites.google.com/site/vdeeditor/Home/vde-files/vde-1-96-manual); [About VDE](https://sites.google.com/site/vdeeditor/Home/about-vde)
- VDE "Coded in assembly language, the executable file is remarkably small (80K)". The page mentions an "8088- & Palmtop-friendly 1.65C" version and "Compatibility with older or limited PCs including laptops, palmtops". v1.9x edits files of about 600 KB. Licence: "Formerly shareware – now freeware (but still copyrighted by the author)." — [BTTR Free Software for DOS: Text Editors](https://www.bttr-software.de/freesoft/txtedit1.htm)
- VDE 1.97 (2021) is on the Internet Archive. — [archive.org vde-197](https://archive.org/details/vde-197). DOS 2.x support was dropped in 1.92. — [VDE Google Group](https://groups.google.com/g/vde_editor)
- **SuperTED 1.01** (6 KB, "Based on PC Mag's TED", Tom Kihlken / Tony Whyman 1993) is listed. No CPU or licence notes are given. — [BTTR Text Editors](https://www.bttr-software.de/freesoft/txtedit1.htm)
- **SEDT** "is capable of loading very large files in real mode (default, up to 10MB using disk or EMS)". — [BTTR Text Editors](https://www.bttr-software.de/freesoft/txtedit1.htm)
- **TDE** and **Vim** are listed as "(32-bit ver.)" in BTTR's LFN list, so the commonly distributed DOS builds are 32-bit. — [BTTR Text Editors](https://www.bttr-software.de/freesoft/txtedit1.htm)
- The **GNUish** project ships 16/32-bit DOS editors: JED 0.97-13 (`jed9713x.zip`), Freemacs (`emacs16a.zoo`) and MicroEmacs (on the FSF diskette). The project "was first organized with small IBM PC's in mind, that is, 8088 and 80286 based systems". — [GNUish project page](https://ftp.lip6.fr/pub/freedos/gnuish/gnuish.htm); [GNU bulletin listing](https://web.cecs.pdx.edu/~trent/gnu/bull/16/gnu_bulletin_24.html)

### Inferences
- An EDLIN script approach (`EDLIN F.C < E.TXT` with `1,5R old^Znew`, `E`) technically works through redirection, but control characters (^Z, ^V) make it fragile. Writing whole files from the agent side is far more reliable. EDLIN is a last resort for in-place line edits of files too large to round-trip.
- Freemacs is written in 8086 assembly and is a strong 8088 candidate **[unverified]**.

### Gaps
- **TED** (PC Magazine, Tom Kihlken, about 3 KB, 1988): PC Magazine utilities were copyrighted and their redistribution status is unclear, so licence class (c) **[unverified]**.
- **QEdit**: SemWare's DOS QEdit (2.x/3.x) ran on 8088 PCs **[unverified]**. I found no source confirming that SemWare made old QEdit versions freeware. Treat it as (d) commercial unless the user owns a copy.
- **PE2** (IBM Personal Editor II): commercial IBM product, class (d). It ran on PC/XT-era hardware **[unverified]**.
- **Elvis**: 16-bit DOS builds of Elvis 1.x/2.x existed (Elvis 1.8 was built with Turbo C; licence is "Clarified Artistic" in 2.x) **[unverified, no source fetched]**.
- **MicroEMACS 3.x** (Daniel Lawrence): 16-bit DOS builds existed. The licence restricts commercial use (the 3.10+ terms were non-commercial) **[unverified]**.
- **Brief** (commercial, Underware/Borland): versions 2.x/3.x ran on 8088 PCs **[unverified]**.
- No 8088 timing or performance data was found for any editor.

## Command-line text processing utilities (sed/grep/diff/patch/awk/sort etc.)

### Takeaway
The **GNUish MS-DOS** project is the main source of GPL (class a) 16-bit real-mode builds of GNU sed, grep, diff, patch, gawk/mawk, sort (textutils), less, m4, make, RCS, uuencode, gzip and more. It explicitly targets 8088/80286. Most modern DOS Unix ports (DJGPP: GNU fileutils/textutils 2.0, findutils, shellutils, bash, sharutils) need a 386 plus DPMI and are excluded.

### Cited Findings
- GNUish: "GNUish MSDOS was first organized with small IBM PC's in mind, that is, 8088 and 80286 based systems." Programs are GPL "either version 1, or (at your option) any later version", and "The GNU General Public License requires that the complete source code be available" when binaries are distributed. — [GNUish project page (lip6 FreeDOS mirror)](https://ftp.lip6.fr/pub/freedos/gnuish/gnuish.htm)
- GNUish: "In general, this software will run on 8086 and 80286--based machines; a 80386 not required." — [GNUish (riken mirror)](https://ftp.riken.jp/GNU/MicrosPorts/MSDOS.gnuish)
- GNUish does contain some 32-bit builds. Gawk 3.0.x ships `gawk.exe` (16-bit DOS), `gawk-os2.exe` (16-bit OS/2), `gawk-djg.exe` (32-bit DOS) and `gawk-w32.exe`. — [GNUish project page](https://ftp.lip6.fr/pub/freedos/gnuish/gnuish.htm)
- GNUish file names: sed 1.06 (`sed106.zip`), grep 2.0 (`grep20ax.zip`), grep 1.5 (`grep15.zip`), fgrep 1.1, diff 2.3 (`diff23x.zip`) and 1.15, patch 2.0 port (`patch212.zip`), gawk 3.0.6/3.0.4/3.0.3, mawk 1.2.2 (`mawk122x.zip`), one-true-awk 970821, text utilities 1.11 (`tut111ax.zip`, includes sort/head/tail/wc/uniq/cut/od/md5sum-era tools), file utilities 3.12, find 3.2, less 2.91, m4, GNU make 3.71 and dmake 3.8, RCS 5.7, indent, flex, bison, uuencode (`uuencode.zip`), gzip 1.2.4, tar 1.09, zoo 2.1, bc 1.02, cpio. Bitness is not stated per file except for gawk. — [GNUish project page](https://ftp.lip6.fr/pub/freedos/gnuish/gnuish.htm)
- "There is no port of GNU bash available to 8088 and 80286 systems". — [GNUish project page](https://ftp.lip6.fr/pub/freedos/gnuish/gnuish.htm)
- The diff 1.15 port was built with Microsoft C 5.1/6.0. — [GNU bulletin](https://web.cecs.pdx.edu/~trent/gnu/bull/17/gnu_bulletin_9401_26.html)
- BTTR's Unix-tools page says 16-bit/DOS16 builds "will work on 80286 or older CPUs and almost any DOS", while 32-bit DJGPP builds "require an 80386+ CPU and a DOS Protected Mode Interface". It lists 16-bit builds of GNU Diffutils 2.3 and GNU Textutils 1.11 (sort, md5sum, etc.). GNU Fileutils has both 16- and 32-bit builds. bash, GNU Shellutils, Findutils and Sharutils are 32-bit (386+) only. — [BTTR Free Software for DOS: Unix-like](https://bttr-software.de/freesoft/unix.htm)
- BTTR describes the **Berkeley Utilities** (awk, grep, etc.) as "Formerly paid commercial software, now in the public domain and completely free". No CPU is stated. — [BTTR Unix-like](https://bttr-software.de/freesoft/unix.htm)

### Inferences
- A practical agent toolkit for the XT is GNUish sed 1.06, grep 2.0, diff 2.3, patch 2.x, mawk or 16-bit gawk, and textutils 1.11 (sort/wc/head/tail/od/cksum). All are GPL, so ship the source archives (`*s.zip`) alongside on the image or provide a written offer to satisfy the GPL.
- 16-bit GNU tools built with MSC have a 64 KB data limit per segment (or use far heap). Large files may fail with "memory exhausted". The agent should keep test files small.
- The FreeDOS base FIND/SORT/FC/MORE/COMP are GPL and small, and tagged "all" **[unverified per tool, report not readable]**. FC with `/B` gives a byte-level compare and `/N` line numbers, which is useful for the agent.

### Gaps
- I could not confirm 8088 (not just 286) operation for each GNUish binary. Some might be compiled for 286 (`-G2`). Test each with the project's DOSBox-X `cputype=8086` boottest or on the real XT.
- No hexdump/xxd tool was verified. Candidates: GNU `od` in textutils 1.11 (16-bit), FreeDOS DEBUG (`d` command, 8086, GPL **[unverified]**), or a tiny custom tool built with the project's own Watcom toolchain. The last is probably simplest.
- No CRC/checksum tool was verified apart from `md5sum`/`cksum` in textutils 1.11 (16-bit per BTTR). A custom Watcom-built CRC32 tool is trivial.
- **MKS Toolkit** (commercial, class d): early versions (1.x/2.x, mid-1980s) targeted 8086 DOS and included vi, awk, sed, grep, diff, sh (Korn shell) **[unverified, no source fetched]**. If the user owns an original copy, it would be the single most capable 8088 Unix toolkit, including a real shell.
- **Thompson Toolkit** (Thompson Automation, commercial): no information found.

## Archivers (ZIP/UNZIP/LHA/ARJ), uuencode, shells (4DOS), commercial notables

### Takeaway
Use **Info-ZIP** 16-bit DOS builds (class a, BSD-like Info-ZIP licence), not FreeDOS 1.4's UNZIP package (tagged 386+). 4DOS is source-available under a restricted modified-MIT licence, so it is not OSI-free but redistributable on FreeDOS-only terms. Its 8088 compatibility is unconfirmed.

### Cited Findings
- Info-ZIP License (version 2009-Jan-02): "Permission is granted to anyone to use this software for any purpose". Binary redistributions "must reproduce the above copyright notice". This is class (a). — [Info-ZIP License](https://infozip.sourceforge.net/license.html)
- GNUish lists Info-ZIP UnZip 5.32 (`unz532x3.exe`, DOS) and Zip 2.2 (`zip22x.zip`, DOS). — [GNUish project page](https://ftp.lip6.fr/pub/freedos/gnuish/gnuish.htm)
- FreeDOS 1.4's `unzip` package is tagged "386+". — [FreeDOS 1.4 report (search snippet)](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/report.html)
- 4DOS source is available under a modified MIT licence that does not meet OSI/FSF standards. The code "may not be used in any commercial product without written permission from Rex C. Conn" and "may not be compiled for use on any operating system other than FreeDOS". The last JP Software update was 7.50.130 (August 2004). Luchezar Georgiev and Jaelani Utomo continued it, up to 8.00 (2009-02-27). Wikipedia is inconsistent on whether 7.50.1 (2006) or 8.00 was the source release. — [Wikipedia: 4DOS](https://en.wikipedia.org/wiki/4DOS)
- GNUish also carries gzip 1.2.4, zoo 2.1, tar 1.09, cpio, shar 3.49 and uuencode. — [GNUish project page](https://ftp.lip6.fr/pub/freedos/gnuish/gnuish.htm)

### Inferences
- Licence class: 4DOS is (b)-like for this project: redistributable, but only for FreeDOS, and not in a commercial product. This disk image runs FreeDOS, so it fits, but check the exact licence text in the 4DOS distribution.
- 4DOS is valuable for the agent: `>&` redirection of stderr, `IFF`, `DO` loops, `%@functions` (e.g. `%@FILESIZE`, `%@CRC32` in later builds **[unverified]**), and the `ESET`/`SETDOS` commands make batch scripting far more capable than COMMAND.COM within the 127-character line limit.

### Gaps
- **Info-ZIP 16-bit DOS builds of UnZip 6.0 / Zip 3.0**: I believe Info-ZIP distributed 16-bit `unz600x3.exe`-style DOS builds (the "x3" suffix denotes 16-bit DOS) **[unverified]**. UnZip 5.52 16-bit is the safe fallback. Compressing with Zip on an 8088 is slow, and Zip 3.0's 16-bit build may have been dropped **[unverified]**.
- **PKZIP 2.04g**: shareware (PKWARE, 1993). It runs on an 8088 **[unverified, believed true]**. The shareware licence allowed non-commercial evaluation and unmodified distribution, but continued use required registration. The licence text was not fetched, so class (c)/(d).
- **LHA 2.55 (Haruyasu Yoshizaki)**: widely described as freeware and 8086-compatible **[unverified]**.
- **ARJ 2.x (Robert Jung)**: shareware. DOS versions ran on 8088 **[unverified]**. Open-source ARJ (arj.sourceforge.net) is a different, 32-bit build.
- **4DOS on 8088**: no source states a CPU requirement. JP Software's 4DOS historically supported 8088 PCs (4DOS 2.x-5.x era), but whether 7.50/8.00 still runs on an 8086 is **unconfirmed**; test it.
- **Norton Utilities** (commercial, class d): versions 4.x/4.5 ran on PC/XT 8088 machines **[unverified]**. They are useful for the human (NU, NCD, FileFind, DiskEdit-era tools) but mostly interactive. Batch-friendly pieces are FF (file find), TS (text search), FS (file size) and DS (directory sort) **[unverified]**.
- **Debugging**: FreeDOS DEBUG (by Paul Vojta, later enhanced; licence and 8086 support **[unverified]**) can be scripted via redirected input (`DEBUG PROG.EXE < CMDS.TXT > OUT.TXT`), which suits an agent's single-command `run` tool. The project's own Watcom toolchain includes WD (Watcom Debugger), but it runs on the dev PC, not the XT **[unverified for XT use]**. Microsoft SYMDEB/CodeView (commercial, class d) are older 8088 options.
