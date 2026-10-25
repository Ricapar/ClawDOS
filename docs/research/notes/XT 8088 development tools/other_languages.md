# Non-C/asm languages that run ON an IBM PC XT (8088, 640 KB, FreeDOS 1.4)

Scope note: about 18 tool calls. ibiblio.org (FreeDOS package repo) returned HTTP 503 on every fetch, so the FreeDOS 1.4 package list could not be checked. Coverage is uneven: GW-BASIC, Turbo Pascal, QuickBASIC, PowerBASIC, F-PC, XLISP, FST Modula-2, PD Prolog and Bywater BASIC were researched. Logo, Little Smalltalk, Icon, AWK, REXX, Turbo Prolog, muLISP/PC-LISP, Logitech Modula-2 and the other Forths were NOT researched (see Gaps). Anything marked "UNVERIFIED" is from the researcher's background knowledge, not a fetched source.

## GW-BASIC: an MIT-licensed, buildable binary; runs a .BAS from the command line; output redirection

### Takeaway
Yes. TK Chia's project assembles Microsoft's 2020 MIT GW-BASIC source with JWasm/MASM and publishes binaries, the latest being 20261005. Those binaries are licence class (a), MIT, so we can ship them on our image. GW-BASIC's own documentation describes `GWBASIC PROG >OUT` redirection, under which PRINT output goes to the redirected file. It looks like a strong fit for the `run` tool, but 8088 and FreeDOS behaviour of the rebuild, and whether capture works, still need testing on the XT.

### Cited Findings
- Microsoft published the 8088 assembly source on GitHub under MIT on 2020-05-21. The repo is read-only, with "no build scripts, makefiles, or tools required to generate executable binaries, nor any pre-built binaries". — [Wikipedia: GW-BASIC](https://en.wikipedia.org/wiki/GW-BASIC); [InfoQ](https://www.infoq.com/news/2020/06/microsoft-gw-basic-open-source); [SD Times](https://sdtimes.com/msft/microsoft-open-sources-gw-basic-code/)
- TK Chia's GW-BASIC project is "a project for assembling Microsoft GW-BASIC from 1983, using MASM or JWasm", a fork of dspinellis/GW-BASIC, under the MIT License. It is mirrored from Codeberg. — [gitlab.com/tkchia/GW-BASIC](https://gitlab.com/tkchia/GW-BASIC)
- Releases: 20261005 ("Stable"; GWB26A05.ZIP, 76 KiB), 20221216 (GWB22C16.ZIP), 20221212, 20201025 and 2020-06-26/27/28 (each about 37 KiB). The notes call it a "pre-release" binary. "Serial port I/O support is missing", and light pen, joystick and printer output "need more testing". If Ctrl-Break doesn't work, try Ctrl-Shift-B. — [Codeberg releases](https://codeberg.org/tkchia/GW-BASIC/releases)
- FreeDOS does not ship GW-BASIC, but FreeDOS documentation points users to TK Chia's build, which runs as GWBASIC.EXE from the DOS prompt. — [FreeDOS book: GW-BASIC](https://freedos.org/books/get-started/21-gw-basic/); [opensource.com](https://opensource.com/article/21/6/freedos-gw-basic)
- GW-BASIC documents command-line redirection: `GWBASIC MYPROG >file` sends PRINT output to the file, and `GWBASIC MYPROG <file` reads input from one. One secondary transcription says INPUT/LINE INPUT still read the keyboard when only output is redirected. The original user guide has a chapter 5 section called "Re-direction Of Standard Input And Standard Output". — [Scribd transcription](https://www.scribd.com/document/378780455/basic4) (secondary); [Kaypro GW-BASIC User Guide 3.2 PDF](https://kayprojournal.com/images/7/7d/GW-Basic_UG_3.2.pdf) (primary, body not read)
- Primary reference manual for the language: [Microsoft GW-BASIC Interpreter User's Reference 1986 (bitsavers mirror)](https://www.mirrorservice.org/sites/www.bitsavers.org/pdf/microsoft/gw-basic/410130013-320-R01-0686_Microsoft_GW-BASIC_Interpreter_Users_Reference_1986.pdf)

### Inferences
- The 1983 source targets the 8088 (Microsoft describes it as 8088 assembly), so the TK Chia rebuild should run on the XT. This was not explicitly confirmed; see Gaps.
- Planned agent pattern: `GWBASIC PROG.BAS >OUT.TXT` with `SYSTEM` as the program's last line, then `TYPE OUT.TXT`. With output redirected, the screen-only statements (LOCATE, COLOR, SCREEN graphics) presumably don't reach the file.
- GW-BASIC binaries pulled from MS-DOS OEM disks are class (c)/(d): Microsoft's MIT grant covers its released source, not the OEM binaries. The TK Chia build avoids that question.
- BASICA: the user's XT has IBM Cassette BASIC in ROM, so their own IBM PC-DOS disk's BASICA.COM would work (class (d), their licensed copy). It offers nothing over GW-BASIC for this use. UNVERIFIED: BASICA needs the IBM ROM, and redirection behaviour is likely similar.

### Gaps
- 8088 operation and FreeDOS compatibility of the TK Chia build were not stated on the pages fetched (the GitHub README fetch returned 404).
- I did not confirm from primary text whether redirected PRINT output actually goes through DOS stdout, or whether a redirected stdin would also be read by INPUT. This needs testing in the boottest harness.
- I did not check whether TK Chia's release ZIPs include LICENSE text.

## Other BASICs: QuickBASIC 4.5, PowerBASIC, Turbo Basic, Bywater BASIC, FreeBASIC

### Takeaway
QuickBASIC 4.5 runs on an 8088 with 384K and is class (d). The user may own it, and its BC/LINK command-line compiler builds real EXEs that print to stdout. PowerBASIC 3.5 can be requested free from the vendor but may not be redistributed, so it is class (c), or (d) in practice. A 16-bit DOS Bywater BASIC build is unconfirmed and may hit stack problems.

### Cited Findings
- QuickBASIC 4.5: minimum CPU 8088. — [WinWorld: QuickBasic 4.5](https://winworldpc.com/product/quickbasic/45)
- QuickBASIC 4.5: "Requires 384K available user memory, DOS 2.1 or higher, two 360K drives (or hard drive)". — [EMSPS Microsoft BASIC version info](http://www.emsps.com/oldtools/msbasv.htm)
- QuickBASIC 4.50 runs in the browser emulator PCjs's PC x86 machines. — [PCjs QuickBASIC 4.50](https://www.pcjs.org/software/pcx86/lang/microsoft/basic/quickbasic/4.50/)
- PowerBASIC: the last DOS release was 3.5, in 1997. — [Wikipedia: PowerBASIC](https://en.wikipedia.org/wiki/PowerBASIC)
- PowerBASIC DOS terms, as reported in 2019: the product is unsupported, "not freeware", and redistribution is prohibited. It is "provided by request at no cost": support emails a temporary download link and a serial number. — [BTTR forum: Getting PowerBASIC 3.5 for DOS](https://bttr-software.de/forum/forum_entry.php?id=22536) (secondary, 2019)
- WinWorld lists PowerBASIC 2.x and 3.x, including a 3.5 build dated 12-16-1997. — [WinWorld PowerBASIC 3.x](https://winworldpc.com/product/powerbasic/3x); [2.x](https://winworldpc.com/product/powerbasic/2x)
- Bywater BASIC: written in C, open source, runs on DOS. The current release is bwbasic-3.40. The FreeDOS book covers it. — [SourceForge bwbasic](https://sourceforge.net/projects/bwbasic/files/); [FreeDOS book: Bywater BASIC](https://freedos.org/books/get-started/23-bywater-basic/); [opensource.com](https://opensource.com/article/21/6/freedos-bywater-basic)
- One hobbyist reports that bwbasic 2.5 "needs a BIG stack… so 16bit stuff is out". A GitHub mirror contains a `bccdos16.bat` build script, which hints at a 16-bit DOS target that I did not verify. — [VirtuallyFun: Bywater basic](https://virtuallyfun.com/2010/06/09/bywater-basic/); [GitHub John-Titor/bwbasic](https://github.com/John-Titor/bwbasic)

### Inferences
- QuickBASIC 4.5 is the most capable BASIC for the agent if the user owns it. Plausible pattern: `BC PROG.BAS;` then `LINK PROG;` then `PROG >OUT.TXT`. UNVERIFIED: whether BC needs the BRUN45 or BCOM45 library flags (`/O` for standalone).
- UNVERIFIED (background knowledge, no source fetched): FreeBASIC needs a 386 with DPMI (excluded). Turbo Basic 1.x ran on an 8088. QBasic 1.1 (shipped with MS-DOS 5+) runs on an 8088 with `QBASIC /RUN PROG.BAS`, but it is class (c)/(d).
- Bywater BASIC is class (a), but someone would have to build it with Watcom `-0` large or huge model to find out whether it runs on an 8088. The repo already has the Watcom toolchain to try this.

### Gaps
- No primary source on Turbo Basic CPU or memory requirements, or on PowerBASIC 2.x/3.x CPU minimums.
- Whether a 16-bit bwbasic build exists or works was not confirmed.

## Pascal: Turbo Pascal versions, Borland "antique" terms, Free Pascal i8086

### Takeaway
Borland's free antique downloads (TP 1.0, 3.02, 5.5) are "personal use only", so they are class (c) and cannot go on a shipped image. The user's own boxed copies are class (d). Every TP version generates 8086 code. TP 7.0's real-mode TURBO.EXE IDE runs on pre-286 CPUs. TPX, the protected-mode IDE, uses memory above 640 KB, which implies a 286 or later. For agent use, TPC (the command-line compiler, from TP 4.0 on) is the tool.

### Cited Findings
- Notice on the antique compilers (TP 1.0, 3.02, 5.5): "These historical Borland compilers are provided to the community free of charge." "They may be downloaded and used 'as is' for personal use only." — [Turbo51: Turbo Pascal download](https://turbo51.com/turbo-pascal-download) (mirror of the Borland notice)
- The same notice is reported to say that each product "contains copyright notices that are still in force". A 2002 report says download required (free) registration on Borland's developer site. — [Sharp PC-3K list, 2002](https://kogs-www.informatik.uni-hamburg.de/~Utcke/lists/sharp-pc3k/msg00321.html)
- A 2024 VOGONS post says the Borland museum "has disappeared". Turbo51 says Embarcadero still hosts some antique software. — [VOGONS](https://www.vogons.org/viewtopic.php?p=1256021); [Turbo51](https://turbo51.com/turbo-pascal-download)
- TP 7.0: "TURBO runs in real mode, using the 640 kB of a traditional PC; TPX runs in protected mode, and so makes use (only for itself) of memory above 640 kB." "Both TURBO and TPX generate only real-mode programs." — [Merlyn Pascal FAQ (mirror)](https://people.cs.nycu.edu.tw/~tsaiwn/sisc/runtime_error_200_div_by_0/www.merlyn.demon.co.uk/pas-bptp.htm)
- Dr. Dobb's (1993): the TP 7 IDE gains extended-memory access on 286/386/486 machines and "will still operate in real mode on pre-286 CPUs". — [Dr. Dobb's Structured Programming 1993](https://jacobfilipp.com/DrDobbs/articles/DDJ/1993/9301/9301l/9301l.htm)
- BYTE (1993): TP 7.0 is for "programmers who want to write DOS applications in real mode (i.e., 8086/8088 compatible)". — [Halfhill, BYTE 1993](https://halfhill.com/byte/1993-1_borland.html)
- TP 7.0 User's Guide PDF (primary source, not read in detail). — [turbopascal.org](https://turbopascal.org/wp-content/uploads/Turbo_Pascal_Version_7.0_Users_Guide_1992.pdf)

### Inferences
- Recommended order for the XT, if the user owns the disks: TP 5.5 or 6.0, or TP 7.0 using TURBO.EXE and TPC.EXE (not TPX or BP). A plausible pattern is `TPC PROG.PAS` then `PROG >OUT.TXT`. WriteLn goes through DOS stdout unless the program uses the CRT unit, which writes directly to video. UNVERIFIED, from background knowledge: CRT bypasses redirection unless you `Assign(Output,'')`.
- TP 3.02 is a COM-file IDE with no separate command-line compiler (UNVERIFIED), so it is hard to drive non-interactively.
- UNVERIFIED: Free Pascal's i8086 target is a cross-compiler hosted on a 32/64-bit OS, so it can't run on the XT, though it could build XT programs on the PC.

### Gaps
- No primary source on the minimum CPU for TP 5.5 or 6.0, or on TPC memory needs (both believed to work on an 8088).
- The original Borland or Embarcadero licence page was not retrieved; the notice text comes from mirrors.

## Forth, Lisp, Prolog, Modula-2

### Takeaway
F-PC 3.6 is public domain and built for PC/XT/AT machines under DOS, so it is class (a). XLISP 2.1 and XLISP-PLUS ship DOS PC executables but are "non-commercial" only (class b/c). Betz's later XLISP 3.0 is MIT, but its 8088 suitability is unknown. FST Modula-2 4.0 is closed-source freeware (class b or c; redistribution terms not seen). A.D.A. PD Prolog 1.9x is a "public domain", "rather slow" DOS Prolog (class a/c).

### Cited Findings
- F-PC 3.6 (1994-12-18), whose README says it "is a public domain package". — [Internet Archive: FPC 3.6](https://archive.org/details/fpc-36)
- F-PC is "a Forth system optimized for IBM-PC, XT & AT machines running DOS". — [GitHub uho/F-PC](https://github.com/uho/F-PC)
- Zimmer says the F-PC kernel started in the public domain and stayed there. — [Interview with Tom Zimmer](https://jimlawless.net/blog/posts/zimmer/)
- A ForthHub list of DOS Forths puts eForth and Pygmy under "Written in Assembly", with no licences given. — [ForthHub DOSForth](https://github.com/ForthHub/ForthFreak/blob/master/DOSForth)
- XLISP-PLUS: because some contributors reserve commercial rights, it "is only distributed for non-commercial (or educational) use". Executables are available for several 80x86 operating systems. — [XLISP-PLUS home page (Tom Almy)](https://almy.us/xlisp.html); [GitHub blakemcbride/XLISP-PLUS](https://github.com/blakemcbride/XLISP-PLUS)
- XLISP 2.1e's zip "comes with IBM/PC executables". — [CMU AI repository: XLISP](https://www.cs.cmu.edu/afs/cs/project/ai-repository/ai/lang/lisp/impl/xlisp/0.html)
- Betz's XLISP 3.0 repo is MIT-licensed. — [GitHub dbetz/xlisp](https://github.com/dbetz/xlisp)
- FST Modula-2: development stopped in 1996, and 4.0 was released as freeware. "originally shareware, now closed source freeware". Library sources were not included. — [EDM2: Fitted Software Tools Modula-2](https://www.edm2.com/index.php/Fitted_Software_Tools_Modula-2); [Free Modula-2 Pages](https://freepages.modula2.org/compi.html)
- PD Prolog (Automata Design Associates, defunct): "a rather slow implementation of Prolog for MS-DOS systems". Version 1.90 (5-May-86) is archived, and a later 1.95D exists. — [CMU AI repository: pdprolog](http://www.cs.cmu.edu/afs/cs/project/ai-repository/ai/lang/prolog/impl/prolog/pdprolog/0.html)

### Inferences
- Since PD Prolog dates from 1986 and F-PC targets the XT, both almost certainly run on an 8088. That is inference: PD Prolog's 8088 support was not explicitly confirmed.
- XLISP 2.x is portable C and was common on 8088 PCs in 1988, but the non-commercial clause means checking its bundled licence file before shipping it.
- UNVERIFIED: F-PC can probably run a script and exit with something like `F-PC FLOAD PROG.SEQ BYE`, but this needs confirming in F-PC docs.

### Gaps
- Not researched: Pygmy Forth licence, eForth 8086 licence, F83 and MVP-Forth; PC-LISP, muLISP, Golden Common Lisp; Turbo Prolog CPU needs; Logitech Modula-2.
- Unconfirmed for every tool in this section: non-interactive use (load a file, print to stdout, exit) and speed impressions on a 4.77 MHz machine.

## Logo, AWK, REXX, Little Smalltalk, Icon, and the FreeDOS 1.4 package collection

### Takeaway
I found no sourced information on these; ibiblio was returning 503 and the call budget ran out. They all remain open questions.

### Cited Findings
- None. The FreeDOS repository pages tried, [1.4 devel index](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/repos/devel/index.html) and [repositories/1.4/devel](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/devel/), both returned HTTP 503. freedos.org/software/ returned 404.

### Inferences
- UNVERIFIED leads: Thompson AWK and Brian Kernighan's "one true awk" can be built 16-bit. Icon for MS-DOS existed in 8086 builds from the University of Arizona (public domain). Little Smalltalk v3 is C and might build 16-bit. IBM PC Logo (LCSI, 1983) ran on the original PC. Regina REXX for DOS is believed to be 32-bit (DJGPP), which would exclude it. Several FreeDOS devel packages (Lua, Regina, Perl, FreeBASIC, DJGPP tools) are believed to need a 386.

### Gaps
- No package-by-package CPU check of FreeDOS 1.4 devel. Retry ibiblio, or read the FreeDOS LSM files.
- No sources on Logo, AWK, REXX, Little Smalltalk or Icon for DOS.
