# Claude-XT architecture review (2026-10-28)

Scope: `lib/*`, `src/claude/*`, `tests/unit/cxtunit.c`, `scripts/*.py`, `bridge/*.py`, the docs, and `build/claude.map` as built on 2026-10-28. Every finding below was confirmed by three independent verifiers against the sources, the objects (`wdis`) and the map; their corrections are folded in. Severity is the verifiers' significance, not the original reviewer's. Finding ids (F*, C*) key into the appendix.

## 1. Executive summary

This is a well-engineered, well-measured codebase for an unusual target. The layering is real (lib never sees src, parsers are hardware-free behind callback tables, the tool contract is one small header), the CGA path is faithful to the real IBM card, the protocol is sized to the XT's buffers end to end, and the Python side is uniformly clean. The problems are concentrated: three real stack-memory bugs in C (`memory_list`, `basdraw`'s error path, `-watch`), a draw path that pays ~400 cycles per pixel, a console wrap rule that tears full-width lines, and a test gap exactly where the hardest logic lives (`screen.c`, `console.c`, `toolapi.c` have zero automated coverage). Memory is already trimmed where it matters; another ~25 KB is available with low-risk changes, but M6.2's ~560 KB goal is only reachable by the exit-and-relaunch design the roadmap already favours. Nothing here blocks M6; several items should land before Claude starts writing C on the XT in earnest.

| Area | Grade | Reason |
|---|---|---|
| Readability and organization | A- | Exemplary headers and naming; two static naming styles, a 1214-line `claude.cpp`, duplicated I/O loops |
| Correctness and idiom | B | ISR, CP437, handle and varargs discipline are right; three real stack overflows and a dangling pointer on model-driven paths |
| Memory | B+ | Measured and trimmed with rationale; ~25 KB of easy wins left; DGROUP has 14.6 KB of headroom and no budget check |
| Performance | B- | CGA write path is excellent; draw, PAINT, console painting and search are 3-10x slower than they need to be |
| Robustness | B- | Link loss, truncation and Esc are handled thoughtfully; tool-call lifetime, device names, disk-full writes and midnight are not |
| Architecture / design | B+ | Clean lib; session logic, screen parking and tool special-casing sit in the app and will be needed by M6.2/M7 |
| Build and Python tooling | B | Readable build; `-wx` silently off for the app's own C++, no size report, no test runner |
| Tests and docs | B- | Three-level test story and accurate protocol docs; the display and tool-wire layers are untested, and six expectations live in prose |

## Status (updated 2026-10-29)

Three commits since the review. The tables below describe the code as reviewed on 2026-10-28; this section records what changed, and the memory picture after it.

| Commit | What | Findings closed |
|---|---|---|
| `d7d09a4` | Stack-buffer fixes with unit tests; `#define NOTRACE` | F5, F21, F37, F46, F47 fixed; F12 done |
| `1bcfd23` | The `draw` tool removed outright (basdraw, raster, gfx, `toolDraw`, bridge schema, mock demo, 54 unit tests): with real compilers on the image it saw no use | F4, F20, F27, F29, F31, C5, F65, F69, F70 gone with the code; the raster share of C10/F23 (2,048 B) gone |
| (docs) | README layout table lists edit, search and ems | F54 |
| `916ea7a` | Console wraps when the next character arrives, not when the last column fills; console.c linked into CXTUNIT with a stub screen | C1, F99 fixed; F3 closed (wrap has unit tests) |
| `fa10de3` | `lib/ticks.h`: rollover-safe `ticks_elapsed()`, one set of tick macros for every timeout | C6 fixed; F28's tick half done |
| `1eb59cb` | `-watch` removed with screen.c's shadow copy of VRAM | F97 |
| `ea803a4` | Build prints memory size and DGROUP per program, fails past 56 KB; `-zc` for far-data models | F40, C9 |
| `209ba66` | Bridge gives up on a tool call after 20 minutes: error to the model, then hangs up so the conversation parks | F2 |

| Measure | Reviewed | Now | Change |
|---|---|---|---|
| Linker memory size | 165,216 | 139,584 | -25,632 |
| CLAUDE.EXE on disk | 129,618 | 104,700 | -24,918 |
| DGROUP | 50,896 | 43,312 | -7,584 |
| `_BSS` | 38,340 | 36,190 | -2,150 |

Action plan items 1, 2, 3, 7 and 11 are done; items 4 and 8 are moot (their code no longer exists); item 13 loses its F27 part and has its F3 part done. Links into `lib/basdraw.c`, `lib/raster.c` and `lib/gfx.c` below are now dead: the files were deleted in `1bcfd23`.

## 2. The architecture as it is

CLAUDE.EXE is a single DOS program (OpenWatcom, large model, `-0 -os -zp2`, `wlink eliminate, stack=4096`) made of three layers plus the vendored mTCP stack:

1. **Application (C++, `src/claude/`)**: `claude.cpp` holds the main loop, the input box, the transcript rendering, link liveness (PING/PONG, reconnect with RESUME), the `-watch` VRAM checker and the `ui_*` functions declared in `ui.h`. `tools.cpp` parses the bridge's TOOL/ARG/CALL lines, holds the 19-entry `ToolDef` table (name, title, class R/W/H/M, executor), asks the user in safe mode, runs the executor and streams OUT/DONE back. The two files talk only through `tools.h` (down) and `ui.h` (up).
2. **Library (C, `lib/`)**: `screen.c` (CGA cell writes, retrace sync, CRTC hardware scroll, footer, scrollback, suspend/resume, shadow watch) and `console.c` (word-wrapping stream on top of it) form the display path. `toolapi.c` defines the tool contract (`ToolCtx`, `targ_*` argument store, `tout_*` output, `tool_esc`, `tool_idle`). Executors each sit in their own file: `dosfs.c` (dir/read/write/delete/mkdir/rename), `edit.c`, `search.c`, `shell.c` (run: `spawnl` with an INT 10h teletype hook), `memory.c`, `sysinfo.c`, `sound.c` (+ pure parser `play.c`), `gfx.c` (+ pure `basdraw.c` interpreter and `raster.c` primitives), `ems.c`. `play.c`, `basdraw.c`, `raster.c`, and the `edit_*`/`search_match`/`mem_*` helpers are hardware-free and are linked into `cxtunit.exe` with `toolapi.c`.
3. **Network (C++)**: `xtbridge.cpp` wraps one mTCP `TcpSocket` as a line protocol (1024-byte rx buffer, `send()` with a 30 s stall timeout, `poll()` yielding verb/payload). `claude.cfg` trims mTCP to 1 socket, 12 packet buffers, 5 transmit buffers.

**Dependency graph** (arrows = includes/calls):

```
claude.cpp ──> xtbridge.h ──> mTCP (utils, tcp, tcpsockm, dns, arp, udp, packet, timer)
   │  │  └──> tools.h          ▲
   │  └─────> console.h ──> screen.h          ui.h <── tools.cpp
   └────────> toolapi.h (tool_idle)                      │
tools.cpp ──> toolapi, dosfs, edit, search, memory, ems, sysinfo, sound, gfx, shell, screen, console
edit.c, search.c ──> dosfs.h (FS_PATH_MAX) ──> toolapi.h
sound.c ──> play.h        gfx.c ──> basdraw.h, raster.h, toolapi.h
sysinfo.c ──> screen.h    shell.c, memory.c ──> toolapi.h
play.c, basdraw.c, raster.c, ems.c, screen.c: no project dependencies
```

**Keyboard to bridge to screen.** The main loop polls `biosIsKeyReady()`; `handleKey` edits `input[901]`, `layoutInput` re-wraps it into `lineStart[]` and redraws the footer rows with `scr_putn`. Enter calls `submit()`: history push, `echoUser` (con_write), `bridge.send("MSG", text)`, which builds `tx[]` and loops `TcpSocket::send` + `Utils::processPackets`. Replies arrive via `bridge.poll()` into `rx[]`, then `takeLine` and `handleLine`: `tools_line` first; `TXT` goes `con_write`, `con_putc`, `scr_putc` (word wrap in `console.c`; `scr_scroll` moves the CRTC start address and rewrites the footer during vertical blank; `rebase()` copies the window back before the 16 KB end); `NL` is `con_newline`; `END`/`ERR` is `finished()`.

**Bridge to tool to result.** `TOOL id name` resets `store[]`; each `ARG key chunk` is unescaped and appended by `targ_append`; `CALL` runs `runCall`: `tool_idle()` (ACKs), lookup, `ui_tool_header`, for class W with safe mode `ui_ask` (write/edit previews), then `d->fn(&t)` writes into `outBuf[4097]` through `tout_*`. `sendResult` re-escapes in 400-byte raw chunks as `OUT`, then `DONE id status`; `TS_STOPPED` also sends `STOP`. `draw` and `run` first park the text screen (`saveScreen`: EMS page or 16 KB malloc, `scr_suspend`), run `gfx_draw`/`shell_run`, then `scr_resume` + `ui_redraw`.

**Sizes from the map.** Load image 165,216 bytes; code 101,621 bytes (project modules 41,346; mTCP 25,435; C runtime 34,840); far constants 12,691; DGROUP 50,896 (of which `_BSS` 38,340 and stack 4,096). The EXE file is 129,618 bytes.

### Module table

| module | lines (c + h) | code bytes (map `*_TEXT`) | responsibility | depends on |
|---|---|---|---|---|
| src/claude/claude.cpp (+ ui.h) | 1214 + 15 | 8,671 (0x21DF) | main loop, input box, transcript, welcome panel, link liveness/reconnect/RESUME, clock from HELLO, `-watch` VRAM checker, `ui_*` for tools | xtbridge.h, screen.h, console.h, tools.h, toolapi.h, ui.h, mTCP inlines.h/types.h |
| src/claude/tools.cpp (+ tools.h) | 488 + 21 | 3,639 (0x0E37) | TOOL/ARG/CALL parsing, 19-entry tool table, safe-mode prompts and previews, OUT/DONE streaming, small tools (sysinfo wrapper, peek, poke, port, textart, draw, run), screen save to EMS/heap, `_harderr` | toolapi, dosfs, edit, search, memory, ems, sysinfo, sound, gfx, shell, screen, console, ui.h |
| lib/xtbridge.cpp (+ .h) | 234 + 80 | 1,486 (0x05CE) | mTCP stack start/stop, DNS + TCP connect, line framing (rx 1024, tx 1040), send with 30 s stall timeout, counters | mTCP: utils, timer, packet, arp, udp, dns, tcp, tcpsockm |
| lib/screen.c (+ .h) | 527 + 91 | 2,715 (0x0A9B) | CGA 80x25 cell writes with retrace sync, CRTC hardware scroll + footer rewrite in vblank, rebase before VRAM end, scrollback view, suspend/resume, shadow watch/repair | conio.h, i86.h only |
| lib/console.c (+ .h) | 180 + 45 | 889 (0x0379) | word-wrapping text stream with margins, mark/rewind, make_room for footer growth | screen.h |
| lib/toolapi.c (+ .h) | 221 + 55 | 1,454 (0x05AE) | tool contract: argument store (`targ_*`), result buffer (`tout_*`, hexdump), `tool_fail`, `tool_esc`/`tool_idle`, DOS error names | bios.h only |
| lib/dosfs.c (+ .h) | 310 + 26 | 2,761 (0x0AC9) | dir, read (text/binary hexdump), write, delete, mkdir, rename; `fs_size` | toolapi.h, dos.h/io.h |
| lib/edit.c (+ .h) | 326 + 39 | 2,660 (0x0A64) | exact-text replace in files <= 60,000 B loaded into DOS-allocated memory, temp-file swap, context display | toolapi.h, dosfs.h |
| lib/search.c (+ .h) | 181 + 21 | 1,467 (0x05BB) | text search across files/wildcards with line numbers, case option, binary skip | toolapi.h, dosfs.h |
| lib/shell.c (+ .h) | 408 + 32 | 3,308 (0x0CEC) | run: up to 12 command lines, direct spawn vs COMMAND.COM /C, INT 10h teletype hook capturing head 900 + tail ring 2800, screen snapshot fallback, hold prompt | toolapi.h, process.h/dos.h |
| lib/memory.c (+ .h) | 166 + 31 | 1,289 (0x0509) | memory_list/read/write as C:\MEMORY\NAME.TXT, name validation, first-line summary | toolapi.h |
| lib/ems.c (+ .h) | 69 + 23 | 296 (0x0128) | one 16 KB LIM EMS page via INT 67h, remapped on each call, freed at exit | dos.h/i86.h only |
| lib/sysinfo.c (+ .h) | 79 + 19 | 620 (0x026C) | DOS version, RAM, EMS KB, drive space, BIOS date, video type, date/time | toolapi.h, screen.h |
| lib/sound.c (+ .h) | 108 + 17 | 642 (0x0282) | play tool: PIT channel 2 tones, PIT channel 0 timing, Esc polling | play.h, toolapi.h |
| lib/play.c (+ .h) | 161 + 31 | 1,239 (0x04D7) | GW-BASIC PLAY macro parser into (hz, ms) callbacks; hardware-free | none |
| lib/basdraw.c (+ .h) | 594 + 44 | 5,210 (0x145A) | GW-BASIC drawing-script interpreter (fixed-point 8.8 expressions, DRAW turtle) against `DrawOps`; hardware-free | none |
| lib/raster.c (+ .h) | 175 + 31 | 1,891 (0x0763) | Bresenham line, box, midpoint ellipse, scanline flood fill (512-seed stack) via callbacks; hardware-free | none |
| lib/gfx.c (+ .h) | 207 + 20 | 1,109 (0x0455) | CGA SCREEN 1/2 pixel plotting, `DrawOps` backend, picture hold until key/timeout | basdraw.h, raster.h, toolapi.h |
| mTCP (12 vendored modules) | not counted | 25,435 total (TCP 9,543; UTILS 4,244; DNS 3,552; ARP 2,327; PACKET 1,540; IP 1,268; UDP 1,075; TCPSOCKM 985; TIMER 347; IPASM 315; TRACE 239; ETH 0) | packet driver, ARP, IP, TCP, UDP, DNS, timer tick | — |
| C/C++ runtime (`_TEXT`, `fsroot_TEXT`) | — | 34,840 (0x880D + 0x0B) | clibl.lib / plibl.lib | — |
| tests/unit/cxtunit.c | 408 | not in claude.map (separate cxtunit.exe, 41,838-byte file) | unit tests for play, basdraw, raster, memory helpers, edit, search; prints UNITS-OK | play, basdraw, raster, memory, edit, search (+ toolapi) |
| scripts/build.py | 360 | — | APPS table, Watcom flags, mTCP force-include, wlink .lnk, disk image | — |

### Memory picture (from `build/claude.map` and the declarations)

**Image.** Linker "Memory size" 165,216 bytes (0x28560) = code + far constants + DGROUP (incl. BSS and stack). EXE file on disk: 129,618 bytes. Entry `0f37:1892` (`_cstart_`).

| region | bytes | notes |
|---|---|---|
| code, project modules | 41,346 | claude 8,671; basdraw 5,210; tools 3,639; shell 3,308; dosfs 2,761; screen 2,715; edit 2,660; raster 1,891; xtbridge 1,486; search 1,467; toolapi 1,454; memory 1,289; play 1,239; gfx 1,109; console 889; sound 642; sysinfo 620; ems 296 |
| code, mTCP | 25,435 | TCP alone 9,543 |
| code, C/C++ runtime | 34,840 | `_TEXT` 0x880D + `fsroot_TEXT` 0x0B |
| FAR_CONST (`*_DATA1`, segment 18cf) | 12,691 | string literals/const data of the C++ modules: claude 3,443; tools 1,595; xtbridge 322; mTCP 7,331 |
| DGROUP | 50,896 | see below |

**DGROUP (0xC6D0 = 50,896 bytes) breakdown**

| segment | bytes | contents |
|---|---|---|
| `_NULL` + `_AFTERNULL` | 34 | Watcom null-pointer guard |
| `CONST` | 4,994 | C modules' string literals and `const` tables (e.g. `sin90[91]`, `octave6[12]`, `builtins[]`) |
| `CONST2` | 1,553 | runtime const data (`__IsTable`, `__Bits`, ...) |
| `_DATA` | 1,748 | initialised statics: `tools[]` table (19 x 14 B), `gfxOps`, `verbs[]`, `watchVecs[]`, mTCP parm names/config, `__iob` |
| `XI` / `YI` | 78 | C++ static init/fini tables |
| `_BSS` | 38,340 | zero-initialised statics (below) |
| `STACK` | 4,096 | `stack=4096` (map: "Stack size: 1000 (4096.)") |

Project-module BSS occupies roughly 1be9:2100–1be9:AC7E (about 35.2 KB); mTCP statics follow from `Buffer_first` at 1be9:AC7E to about 1be9:B3C8 (about 1.9 KB; largest: `Icmp::icmpEchoPacket` 302 B, `Dns::pendingQuery` 270 B, `Utils::lineBuffer` 160 B, `Dns::dnsTable` 136 B); the runtime's `__OpenStreams`, `_environ` etc. come last. File-scope `static` arrays are not named in the map, so the per-array figures below come from the declarations. (C10 parsed the OMF SEGDEF records of `build/obj/claude/*.obj` and confirms the per-module `_BSS`: claude 10,878; toolapi 10,247; shell 4,156; screen 3,540; raster 2,048; xtbridge 1,041; tools 988; dns 800; search 768; dosfs 552; memory 544; edit 512; console 334; ip 332; utils 292; total 37,514 of the 38,340.)

**Largest static buffers (declaration sizes)**

| array | file | bytes |
|---|---|---|
| `history[HIST_N=8][INPUT_MAX+1=901]` | claude.cpp | 7,208 |
| `store[TOOL_ARGS_MAX]` (tool arguments) | toolapi.c | 6,144 |
| `outBuf[TOOL_OUT_MAX+1]` (tool result) | toolapi.c | 4,097 |
| `tail[TAIL_MAX]` (run output ring) | shell.c | 2,800 |
| `sx[512]`, `sy[512]` (paint seed stack) | raster.c | 2,048 |
| `rows[(SCR_FOOT_MAX+1)*SCR_COLS]` (hw-scroll footer image) | screen.c | 1,760 |
| `mirror[SCR_FOOT_MAX][SCR_COLS]` | screen.c | 1,600 |
| `tx[XTBRIDGE_LINE_MAX+16]` | xtbridge.cpp | 1,040 |
| `XTBridge bridge` (contains `rx[1024]`) | claude.cpp / xtbridge.h | about 1,040 |
| `input[901]`, `lineStart[450]` (ints) | claude.cpp | 901 + 900 |
| `head[HEAD_MAX]` | shell.c | 900 |
| `line[OUT_CHUNK*2+8]` (OUT escaping) | tools.cpp | 808 |
| `buf[512]` x4 (dosfs, edit `wbuf`, search, memory) + `line[256]` (search) | lib | 2,304 |
| `lostReport[200]`, `offlineNote[100]`, `host/backend[80]`, `token[64]`, `clockReport[80]` | claude.cpp | 604 |
| `console` `line[80]`, `mark_line[80]` (cells), screen `row[80]` | console.c, screen.c | 480 |
| `first[136]`, `cmd[121]` x2, `why[120]`, `prog[80]` | tools.cpp, shell.c | about 578 |

Sum of the listed project arrays is about 35.2 KB, consistent with the project share of `_BSS`.

**Stack.** 4,096 bytes, inside DGROUP (SS = DS in the large model). Notable frame-local arrays: `tout_printf` `buf[300]`, `basdraw` `stDraw` `s[256]` + a `Bas` copy per DRAW, `scr_puts` `cells[80]` (160 B), `console wrap` `tail[80]` (160 B), `claude.cpp` `Row` (160 B) per footer draw, `watchCheck` `buf[160]`, `snapshot` `line[82]+lens[25]`. The high-water mark and ISR stack use (mTCP timer/packet handlers; `int10Hook` runs on the child's stack) are not determinable from the map.

**Heap and DOS memory (runtime, not in the map).**
- mTCP at `startStack`: `Buffer_init` mallocs `PACKET_BUFFERS x PACKET_BUFFER_LEN` = 12 x 1,514 = 18,168 bytes; `TcpBuffer::init` mallocs the transmit buffers (verifiers: `XTBridge::startStack` passes `TCP_SOCKET_RING_SIZE` = 4, so 4 x (`sizeof(TcpBuffer)` + MSS 1460), about 6.1 KB; `TCP_MAX_XMIT_BUFS` 5 only sizes the pointer array) and `TcpSocketMgr` 1 x `sizeof(TcpSocket)` (about 210 B per TCP.H comment); `setRecvBuffer(4096)` mallocs 4,096 bytes per connect (xtbridge.cpp). About 28.5 KB in all.
- `saveScreen` (draw/run): 16,384 bytes from `ems_page()` when EMS is present, else `malloc`, freed after the tool.
- `watchStart` (`-watch`): one 16,384-byte `malloc`, kept for the run.
- `edit.c load()`: up to 60,000 bytes via `_dos_allocmem`, freed with `_dos_freemem` before returning (bypasses the C heap so a later `run` gets it back).
- `shell_run`: `spawnl(P_WAIT)` gives the child whatever DOS memory remains; docs/ROADMAP.md measures about 355 KB resident (CLAUDE.EXE + mTCP buffers + heap) leaving about 285 KB for the child. A documented measurement, not derivable from the map.

## 3. Readability and organization

**What is good.** Every `lib/*.h` opens with a paragraph stating the module's purpose and contract; the `.c` says "see X.h"; include guards and `extern "C"` are everywhere (except `xtbridge.h`, C++ by design). Public names carry a module prefix (`scr_`, `con_`, `targ_`/`tout_`, `ras_`, `bas_`, ...), mirrored on the C++ side (`XTBridge::`, `tools_*`, `ui_*`). Comments say why: Nagle in `Session.__init__`, `rebase()`'s note that the real CGA does not wrap, `load()`'s reason for `_dos_allocmem`, `int10Hook`'s honest note about the unexplained helper-call failure. The `ToolDef` table and `TOOLS` list give a newcomer one place per side to see every tool.

**Findings.**

- The same primitives are copied across `dosfs.c`, `memory.c`, `search.c`, `edit.c` (F25, medium): the CRLF write loop twice with flush thresholds `sizeof(buf) - 1` vs `- 2` plus `edit.c fileOut` as a third variant; the 512-byte read/strip-CR/stop-at-0x1A loop three times (not four: `load()` reads the whole file at once and should stay separate); the "append `*.*`" code in `tool_dir` and `searchSpec`; the keyboard drain in four places. Verifiers: the shared primitives cannot go in `dosfs.c`, because `cxtunit.exe` links search/edit/memory/toolapi without it; `toolapi.c` or a new `lib/textio.c` in both APPS entries. Pairs with the F24 fix. [lib/dosfs.c:244](../../lib/dosfs.c#L244)
- Two static-naming conventions (F35, low): seven files `snake_case`, five (`shell`, `edit`, `search`, `gfx`, `basdraw`) `camelCase`; `play.c` is not mixed as claimed. One rule line in CLAUDE.md. [lib/shell.c:36](../../lib/shell.c#L36)
- `path_arg()` is static in `dosfs.c`, so `edit.c` and `search.c` re-implement it with the literal `64` for `FS_PATH_MAX`; same for `60000`, `4000`, `256` (F26, low). Stringize at compile time (`XSTR(FS_PATH_MAX)`), not `sprintf`; `EDIT_FILE_MAX` is `60000u` and needs a plain number. [lib/edit.c:254](../../lib/edit.c#L254)
- Tick arithmetic (`* 182 / 10` in seven places, `* 18` in three), `MK_FP(0x40, 0x6C)` three times, bare `0xB800` in gfx.c and shell.c, hard-coded 25/80 in `snapshot` (F28, low). Harmless today; a `lib/bios.h` is also the home for C6's rollover-safe helper. [lib/gfx.c:172](../../lib/gfx.c#L172)
- `claude.cpp` is 1214 lines holding six concerns (F45, low). The input editor (181-349, 694-784) is self-contained and is ROADMAP M7's "input line editor". Treat F13, F42, F45 as one "split claude.cpp" item timed with M7. [src/claude/claude.cpp:214](../../src/claude/claude.cpp#L214)
- In passing (all low, details in the appendix): `ras` defined twice in gfx.c with a shadow `maxX` (F69); "wait for a key or N seconds" written three times (F75); `turtle()`/`statement()` are the hardest functions in lib, and `dirs[d - "UDLREFGH"]` relies on Watcom pooling two identical literals (F65); EMS probed twice differently (F76); `tool_esc()` is really the tool tick (F81); comma sequencing and `note_` (F98); `getattr(args, ...)` nine times in `start()` (F60); `memory.c` means notes, not RAM (F71).

## 4. Would a seasoned C expert accept it?

Mostly yes. The things experts check first are right: the INT 10h hook is textbook Watcom ISR hygiene (`__interrupt __far`, only `volatile` statics, `_chain_intr`, the stack hazard documented); every `ctype` call and cell pack goes through `(unsigned char)`; every `_dos_open`/`_dos_creat` is closed on every error path; varargs are type-correct; the 512-byte chunked writers, `tout_hexdump`'s 90-byte line, `search.c`'s `path[FS_PATH_MAX+16]` and `targ_append`'s edge handling all hold. The following would be sent back.

### Real bugs (undefined behaviour or wrong output reachable today)

**F5 (high): `tool_memory_list` overflows `name[9]` on every call.** [lib/memory.c:74](../../lib/memory.c#L74)
```c
char summary[MEM_SUMMARY + 1], name[9], path[32];
...
strcpy(name, f.name); *strchr(name, '.') = 0;
```
`find_t::name` holds up to 12 chars + NUL; the shipped `DEVTOOLS.TXT` overruns by 4, the tour's `CXTTOUR.TXT` by 3. Verifiers disassembled `memory.obj`: the overrun lands on a pad byte, `h` and the low byte of `got`, both rewritten before use, so it is benign today by layout luck. The system prompt asks for `memory_list` at the start of every conversation. Fix: `char name[13]`; a cxtunit case (memory.c is already linked there).

**F21 (medium): `bas_draw`'s "unexpected char" path formats a dangling pointer.** [lib/basdraw.c:480](../../lib/basdraw.c#L480)
```c
char kw[12]; ... b->kw = kw;                                   /* statement(): local */
sprintf(msg, "unexpected '%c'", *b.p); return fail(&b, msg);  /* bas_draw(): statement() has returned */
sprintf(buf, "line %d: %s: %s", b->line, b->kw, msg);         /* fail(): %s over the dead frame */
```
Worse than stated: `fail()` runs at the depth `statement()` had, so its `buf[120]` occupies where `kw` lived and the `%s` source overlaps the destination. Trigger: `PSET (1,1) 5`. Fix: `char kw[12]` as a `Bas` member; add the `drawFails` case.

**F47 (medium): `watchCheck`'s `buf[160]` overflows on realistic values.** [src/claude/claude.cpp:1030](../../src/claude/claude.cpp#L1030) The first format has 138 fixed chars; with report #10+, `n` = 2048 and a 3-digit second count it reaches 160-162 with the NUL. The BIOS diff loop appends exactly 10 chars per changed byte with no `len <` guard (the vector loop beside it has one): 13+ changes overflow, max 338. `-watch` only, but a stack smash in the tool meant to debug screen corruption. `_snprintf` or a remaining-space check.

**F20 (medium): unbounded recursion in the expression parser on model input.** [lib/basdraw.c:102](../../lib/basdraw.c#L102) `factor()` recurses on every unary sign and parenthesis; `-s`, `stack=4096`, and STACK sits directly above `_BSS`'s tail (`_environ`, `__doserrno`). ~12-16 B per sign, ~70 B per paren level, 6 KB of script allowed. Fix: a depth counter in `Bas` failing past ~16; `stackavail()` is already linked, so `if (stackavail() < 512) return fail(...)` is a cheap second guard. Keep 4096.

**C5 (medium): `ras_ellipse` misdraws from radius ~181, not 46340.** [lib/raster.c:111](../../lib/raster.c#L111) The `a2 * b2` wrap is harmless (sums are exact mod 2^32 when the true `p` fits); the damage is `b2 * (2x+1) * (2x+1) / 4`, which wraps before the divide. Simulated: first wrong default-aspect `CIRCLE` at r = 181-190; even on-screen `rx=300, ry=100` is bent. Fix (derived by a verifier of the rejected F30): `p = b2*x*(x+1) + b2/4 + a2*(y-1)*(y-1) - a2*b2`, identical in integer arithmetic; clamp radii to ~1000 in `stCircle` (region 1's `2*a2*ry` overflows past that); a large-radius unit test.

**C1 and F99 (medium): the console wraps eagerly when the last column fills.** [lib/console.c:146](../../lib/console.c#L146) `if (++col == rm) wrap();` decides before knowing whether the next byte is text or the NL. With reply margins (2, 79) a 77-character TXT line plus NL either orphans its last word or, with no space, hard-wraps and `con_newline` adds a blank row. `toolTextart` (margins (2, 80), 78 columns) hits the same path, so the 78-wide art the tool description invites is torn. Fix: defer to the leading `if (col >= rm) wrap();`, moved ahead of the `c == ' ' && col == lm && soft_wrapped` check, and treat a space arriving at `col == rm` as the break; for textart `col < 77` with the current margins (the suggested `SCR_COLS - 1` plus `col < 77` still wraps) and fix tools.py's "78". [src/claude/tools.cpp:156](../../src/claude/tools.cpp#L156)

**C6 (medium): BIOS ticks treated as monotonic; a bridge outage across midnight stalls reconnect.** [src/claude/claude.cpp:1188](../../src/claude/claude.cpp#L1188) `retryAt` armed within 10 s of midnight lands at or above 0x1800B0, which the counter never reaches again after the BIOS reset, so `(long)(*biosTicks - retryAt) >= 0` stays false until restart ("retrying in 10s" forever). `setClock()` makes real midnights happen. The other sites (`hold()`, `opSleep`, `waitLine`, `drawStatus`, a PING in flight) only cut a wait short or print a bad count. Fix: a self-contained lib `ticksSince()` adding 0x1800B0 when `now < start` (lib must not link mTCP), or a hook CLAUDE points at mTCP's monotonic `Timer_CurrentTicks`.

**F49 (medium): `restoreScreen` frees the EMS frame pointer if the remap fails.** [src/claude/tools.cpp:187](../../src/claude/tools.cpp#L187) Origin is recovered by calling `ems_page()` again; a failed 0x44 remap after `run` returns NULL, so the frame pointer reaches `free()` after `scr_resume` copied whatever is mapped. Watcom's `_ffree` likely ignores a foreign segment, so "heap corruption" is overstated; the garbage restore is real. A `saveFromEms` flag; on failure `con_init()` plus a note.

**C3 (medium): `write` and `memory_write` truncate in place; a full disk destroys the old content.** [lib/dosfs.c:241](../../lib/dosfs.c#L241), [lib/memory.c:142](../../lib/memory.c#L142) `_dos_creat` truncates before the first write; on `wrote < n` a partial copy is left. `edit.c` already has `tempPath()` and `remove || rename`; hoist into dosfs for non-append writes.

**C4 (medium): no guard against DOS device names.** [lib/dosfs.c:18](../../lib/dosfs.c#L18) `read AUX` (or `C:\SRC\AUX.C`) blocks in `_dos_read` before any `tool_esc`, and mTCP's no-op INT 23h handler means Ctrl-C cannot break it; `write CON` prints through DOS over the transcript; `mem_name_ok` accepts `AUX`. Prefer IOCTL `AX=4400h` after open (refuse if DX bit 7) over a name list: it also catches `EMMXXXX0` and PicoMEM devices.

**F37, F46 (medium): `sprintf` targets sized to the typical case.** `why[120]` takes 123-124 bytes for a 79-char `prog` or long `COMSPEC` [lib/shell.c:290](../../lib/shell.c#L290); `offlineNote[100]` overflows for a 64+ char host, and `drawHint`'s `col = 76 - strlen(host)` goes negative above 76 and already overwrites the hint text above ~16 chars (visible with `192.168.1.100:8023`) [src/claude/claude.cpp:959](../../src/claude/claude.cpp#L959). Clamp `host` at entry, `%.Ns`, `snprintf` (declared in Watcom's stdio.h).

**Smaller, real (low):** `targ_num` parses octal (`start=08` is 0, not `def`) despite "decimal or 0x hex" (F78) [lib/toolapi.c:73](../../lib/toolapi.c#L73); tabs reach the screen as glyph 0x09 because `to_cp437()` encodes before consulting the fallback table, and the fix must stay on the TXT path since tool ARG payloads (a Makefile) go through it too (C11) [bridge/cp437.py:17](../../bridge/cp437.py#L17); `newChat` ignores a failed `NEW` (F2/Ctrl-N offline), and must not clear `session[]` since the bridge swaps the chat in place (F95) [src/claude/claude.cpp:500](../../src/claude/claude.cpp#L500); `-q` exits 0 after `ERR` while clock.bat already echoes `%ERRORLEVEL%` (F96) [src/claude/claude.cpp:585](../../src/claude/claude.cpp#L585).

### Idiom and contract

- `read` cuts lines over 160 chars silently (F24, medium). Worse than stated: a single-line `old` copied from a cut line is a prefix of the real line, so `edit` replaces 160 chars and glues the tail onto the new text; a multi-line `old` fails with a hint that points the wrong way. A `...[+N]` marker; `LINE_MAX` 255 fits `tout_printf`. `search.c:54` has the same cut at 100 columns. [lib/dosfs.c:190](../../lib/dosfs.c#L190)
- The INT 10h hook is restored blindly after `spawnl` (F9, medium). Two of three verifiers correct the crash claim: after the blind restore the TSR's stale pointer is never followed; the hang would come from the finding's own suggestion. Keep the restore, compare `_dos_getvect(0x10)` with `int10Hook`, and report in the result that a resident program's hook was removed. [lib/shell.c:287](../../lib/shell.c#L287)
- `tout_printf` does `vsprintf` into `buf[300]` unbounded (F79, low); every caller fits today by discipline only. `_vsnprintf` setting `truncated`, or format into `t->out + t->len` when 300 bytes remain. [lib/toolapi.c:141](../../lib/toolapi.c#L141)
- `targ_append` reopens only the newest key; PROTOCOL.md's "repeated lines append" lacks the adjacency rule (C14, low). The only producer groups chunks by key and `replay.py` merges into a dict, so one sentence in PROTOCOL.md. [lib/toolapi.c:26](../../lib/toolapi.c#L26)
- `run` of `CD` moves CLAUDE's own current directory for the session and the echoed prompt is a fixed `C:\>` (C13, low). Multi-command lists rely on it (`CD \TESTS\DEV` then `TCC X.C`), so save/restore drive and directory around the whole `shell_run`, or echo the real path and say so in the description. [lib/shell.c:269](../../lib/shell.c#L269)

## 5. Memory

**The picture.** 165,216 B image + about 28.5 KB mTCP heap + 16 KB screen save (without EMS) is the ~355 KB ROADMAP measures resident during `run`; DGROUP is 50,896 of 65,536 B (14,640 B headroom) with the 4 KB stack inside it. Resident size is M6.2's problem (F8, medium: nothing is handed back before `spawnl`; verifiers note `_fheapshrink()` would return nothing because every big far block is live then). They agree with the roadmap: approach 1 (exit, stub runs the program, relaunch with `-resume`) is the only route to ~560 KB; approach 2 (swap to EMS) carries these buffers along, so F92's buffer sizes do not discriminate. The trims below are cheap and the DGROUP ones buy headroom for M6.3/M6.4's static buffers.

**Memory findings ranked by KB saved** (R = resident bytes freed, D = DGROUP bytes freed; running totals in KB).

| # | finding | saving | how | R | D |
|---|---|---|---|---|---|
| 1 | F12 (medium) mTCP tracing compiled in | ~10 KB (~6 KB far const + code) | `#define NOTRACE` in [src/claude/claude.cfg:4](../../src/claude/claude.cfg#L4); upstream's NETDRV.CFG does the same. TCP_DATA1 (3,083) is all trace, UTILS_DATA1 (1,202) stays; stdio stays linked (Utils' config reader); drops the undocumented `DEBUGGING=` env trace. Re-run net.bat, tools.bat. | 10 | 0 |
| 2 | F43 (medium) `history[8][901]` | ~5 KB | A 2 KB NUL-separated ring, or just make the array `__far`/heap. Also removes a 6,307-byte memmove per submit. [src/claude/claude.cpp:125](../../src/claude/claude.cpp#L125) | 15 | 5 |
| 3 | C10, F23, F34 (low) tool-only scratch | ~5 KB | raster `sx/sy` 2,048, shell `head/tail` 3,700, five file-tool buffers ~3.5 KB live only during one tool (~9.3 KB); a 4 KB union in `toolapi.c` (in both APPS entries). Do not overlay shell's ring on the args store: `shell_run` re-reads the command list between commands. Moot under M6.2 approach 1. [lib/raster.c:129](../../lib/raster.c#L129) | 20 | 10 |
| 4 | C9 (medium) literals in DGROUP | ~4-5.3 KB DGROUP (moved to code, not freed) | `-zc`: verifiers rebuilt every lib/*.c with it, zero warnings, CONST/CONST2 move into `_TEXT`. Only wcc modules gain (wpp already uses FAR_CONST); silent no-op in the small model. [scripts/build.py:112](../../scripts/build.py#L112) | 20 | 15 |
| 5 | F32 (low) `rows[]` duplicates the footer mirror | ~1.9 KB | Fill the blank row with a helper (attribute is per call) and blast the footer straight from `mirror[]`; split `shadow_copy` likewise; derive `SCR_FOOT_MAX` from `BOX_LINES`. [lib/screen.c:379](../../lib/screen.c#L379) | 22 | 17 |
| 6 | F48 (low) C runtime 34.8 KB | ~1-2 KB realistic | "Most is avoidable" rejected: `spawnl`+mb*/env are the run tool's, time/rand are mTCP's, FILE/sscanf are Utils', `prtf` is shared with `sprintf`. Cheap: `strerror`, the three `printf` wrappers. [src/claude/claude.cpp:1115](../../src/claude/claude.cpp#L1115) | 23.5 | 17 |
| 7 | F97 (low) `-watch` in every build | ~1-1.5 KB | `#ifdef CXT_WATCH` (a bare `-D` is fine under the Watcom rule); watch.bat builds the debug variant. [src/claude/claude.cpp:1027](../../src/claude/claude.cpp#L1027) | 25 | 17 |
| 8 | F85 (low) line buffers sized 1024 | ~1.3 KB | `rx` can be 520; `tx` must stay >= ~910 (MSG carries INPUT_MAX 900), so the saving is rx plus dropping `line[808]` via a raw-line `ui_send_line`. [lib/xtbridge.h:16](../../lib/xtbridge.h#L16) | 26 | 18 |
| 9 | F93 (low) `lineStart[450]` | ~850 B | Two adjacent one-char lines are impossible; 26 is the bound, use 32 and clamp. [src/claude/claude.cpp:122](../../src/claude/claude.cpp#L122) | 27 | 19 |
| 10 | F10 (low) dead functions linked | ~0.4 KB + 160 B BSS | All three verifiers rebuilt with the suggested `-zm`: code *grows* (+61 B) because same-module calls become far. Drop that idea; delete `scr_puts`, `scr_cursor_visible`, `con_row`, `con_col`, `con_mark`, `con_rewind` (and `mark_line`). [scripts/build.py:112](../../scripts/build.py#L112) | 27.5 | 19.5 |

About 27 KB resident (7-8% of 355 KB) and about 19.5 KB of DGROUP (50.9 to ~31 KB) without redesign. None of it changes M6.2's arithmetic. F40 (medium) is the tooling that keeps it honest: parse the map after `wlink`, print DGROUP/_BSS/code per app, fail past a stated budget (e.g. 56 KB), and put large tool-scoped buffers in EMS or `_dos_allocmem` as `edit.c` does. [scripts/build.py:112](../../scripts/build.py#L112) F92 (low): leave the mTCP buffers; `TCP_MAX_XMIT_BUFS` 5 vs the 4 actually passed is harmless.

### Performance (8088 cost)

- **F4 (high): every pixel of `draw` is 3 calls, two clips and a MUL.** `ras_plot` clips, far-indirect-calls `rasPlot`, which calls `plot()` (clips again, re-reads `mode`), which calls `offset()` whose `(y >> 1) * 80` is a real `imul dx` (confirmed in `gfx.obj`); `hline()` does this per pixel, so a 320x200 `BF` box is 64,000 trips of ~400-500 cycles, 5-7 s. Fix: an `hspan(x1, x2, y, c)` callback filled bytewise in gfx.c (colour replicated 0x00/0x55/0xAA/0xFF) used by `hline()`; `(h << 6) + (h << 4)` or a row table. Keep `plot()`'s clip or route `opPset` through `ras_plot`, since it calls `plot()` directly. [lib/gfx.c:33](../../lib/gfx.c#L33)
- **F31 (medium): PAINT probes every pixel twice.** `FILLABLE` evaluates `r->point` twice per non-border pixel, ~6N far indirect calls per N-pixel fill. One helper reading once halves it (a static is still a far call in -ml, so one instead of two); a row-oriented probe removes the multiply. [lib/raster.c:132](../../lib/raster.c#L132)
- **F29 (medium): lines are stepped through off-screen space in 32-bit arithmetic.** `LINE (-30000,100)-(30000,100)` is 60,001 rejected far calls to draw 320 pixels; bounded, seconds not a hang. Cheaper than Liang-Barsky: trivial reject when both ends are past one edge, a 16-bit fast path when `|dx|+|dy| < 16000`. [lib/raster.c:18](../../lib/raster.c#L18)
- **F22 (medium): streamed text is painted one character per call chain.** `con_putc` to far `scr_putc` (imul, `vfill` with pushes and a retrace wait for one cell) to far `scr_cursor` (second imul, four CRTC OUTs): 800-1000 cycles against the 64 us the write is bound by; a 500-byte TXT takes ~120 ms instead of ~35 with nothing polled meanwhile. `line[]` already mirrors the row: flush one `scr_putn` per TXT line, before `wrap()`, `con_newline`, `con_line`, `con_make_room`, mark/rewind and any scroll; give the direct `con_putc` callers (bullets, `toolTextart`) a flush. Attribute changes need no flush. [lib/console.c:145](../../lib/console.c#L145)
- **F33 (medium): case-insensitive search calls `toupper()` (a far library call) twice per compared byte** and `strlen(text)` per line, on the default path of a tool that exists because `FIND` was slow. Pre-uppercase the needle once, fold the haystack inline (`c - ('a'-'A')` in a..z), skip to candidates by first byte. Do not uppercase `line[]` in place: `checkLine` prints it. [lib/search.c:28](../../lib/search.c#L28)
- Low, in the appendix: the spinner rewrites 80 synced cells nine times a second, ~45 ms/s (F44; "costs twice" was wrong); the INT 10h hook's `div si` by 2800 is a few percent, not 20-30% (F36); hex dumps call `sprintf` 18 times per 16 bytes (F80); hold loops recompute a long mul/div and never call `tool_idle` (F70; use `tool_idle()`, not `tool_esc()`, which swallows keys); `edit` makes 3 full + 2 partial passes (F66); `poll()` memmoves per line, mostly tiny (F82); two writes per hblank fit, validate on the real 5153 only (F72); `scr_suspend` reads 16 KB unsynchronized while `scr_resume` already blanks via 3D8 (C8, two-line fix); only `screen.c` compiles to IMUL at `-os`, measure before `-ot` (F87).

## 6. Other areas

### Robustness

- **F2 (medium): a tool call's lifetime is bound to the TCP connection.** `run_tool` awaits with no timeout; only `TCP_NODELAY` is set; the bridge sends nothing while waiting, so a hung or power-cycled XT is never detected, `expired()` stays false and the Session leaks. Corrections: the "CALL after `tools_reset`" sub-claim is wrong; the lenient HELLO check is by design; the 15-byte name/key limit is latent. Do not add `SO_KEEPALIVE` with a 60 s idle: during `run` mTCP is unserviced for up to 9 min, so it would drop every build. Fix: `asyncio.wait_for(fut, 15-20 min)` resolving as error and closing; document the 15-byte limits; specify `AWAY` with M6.2. [bridge/xtbridge.py:383](../../bridge/xtbridge.py#L383)
- **F1 (medium): the bridge never checks arguments against the XT's 6 KB budget.** The XT fails cleanly but only after every ARG arrives and without a number; logged writes reach 5.4 KB; `edit` counts `old` plus `new`. Check `sum(len(key)+1+len(to_cp437(value))+1) <= 6144` (unescaped, not the escaped size proposed) before sending; state the limit in the `write`/`edit`/`draw` descriptions; a test reading `TOOL_ARGS_MAX` from `lib/toolapi.h`. [bridge/tools.py:284](../../bridge/tools.py#L284)
- **F15 (medium): a link drop mid-tool tells the model "Interrupted by the user".** `_settle` fills the pending `tool_use` with `stopped` on the `CancelledError` from `Session.run`'s finally; after RESUME the model believes the user cancelled. Esc and link loss both arrive as `CancelledError`, so set a reason flag before cancel and add a `lost` status through `STATUSES`/`model_text`/`_result_status`. The XT half (`sendResult` ignoring `ui_send`) is cosmetic: the user sees "The reply was cut off" at the next poll. [bridge/providers.py:361](../../bridge/providers.py#L361)
- **F52 (medium): `-q` without `-unsafe` hangs both ends at the first W tool.** `runCall` ignores `unattended`; `ui_ask` blocks in `biosKeyRead` with no `tool_idle`. Return `TS_DENIED` with "nobody to ask (-q)"; poll `biosIsKeyReady()` + `tool_idle()` in `ui_ask`. [src/claude/tools.cpp:427](../../src/claude/tools.cpp#L427)
- **C2 (medium): `XTBridge::close()` uses mTCP's blocking `close()`**, up to `TCP_CLOSE_TIMEOUT` 15 s when the peer cannot ACK the FIN, exactly the `linkLost()` case. `2000ul` will not compile (`static_assert >= 5000ul`); `closeNonblocking()` plus polling `isCloseDone()`, and with one socket `reconnect()` must wait for it. [lib/xtbridge.cpp:114](../../lib/xtbridge.cpp#L114)
- Low: MDA/Hercules takes the CGA path and spins forever on 3DAh (C7); the bridge's overlong-line recovery leaves the tail to be parsed as commands, harmless for CLAUDE.EXE (F59); `send()` clamps at 1024 while PROTOCOL.md says 4096, document 1024 and do not return -1 since callers treat non-zero as a dead link (F83); `has_key()` always says yes for anthropic and `client.api_key`/`credentials` can be checked offline (F57); parked chats are purged only on connect (F18).

### Architecture and design (readiness for M6.2-M6.4 and M7)

- **F7 (medium): `shell.c` fuses the capture ISR, buffers, formatting and hold screen.** Verifiers (2/3) temper it: the stub is NASM and re-implements capture regardless, and CLAUDE can load the stub's dump into the same statics and call `putOutput` unchanged. Do the split (formatting over a byte source) as step one of M6.2, not before. [lib/shell.c:36](../../lib/shell.c#L36)
- **F13 (medium): HELLO/AUTH/RESUME/PING/reconnect live in `claude.cpp`**; `xtbridge.h` says "see claude.cpp's linkLost". M6.2 approach 1 relaunches CLAUDE.EXE itself, which already has the code; only an M7 second app needs the move. A struct-based `lib/xtlink` (GPL like `XTBridge`) when M7 starts; drop.bat/clock.bat cover it. [src/claude/claude.cpp:875](../../src/claude/claude.cpp#L875)
- Low: ~41 + 6 loose statics are M6.2's `ResumeState`, define it as M6.2 work (F42); the EMS-or-heap screen park belongs in a `lib/vsave.c`, and `ems_page()`'s `atexit(release)` means approach 1 needs a file variant anyway (F100, with F49); `runCall` string-compares `"run"`/`"write"`/`"edit"`, add `preview`/`details` pointers when M6.4's W tool arrives (F51); the Python/C tool tables agree and the tooltour catches name drift, so only a regex test of the prose limits is needed (F17); `ui.h`'s header cycle is fixed by a comment or a split header, not a vtable, since a stub `ui.cpp` tests the dispatcher (F53); `xtbridge.h`'s C++ costs 11 bytes and the GPL boundary is the EXE either way (F84); adapter type and the hwscroll flag are one variable, so `sysinfo` says EGA/VGA under `-noscroll` (F77).

### Build and Python tooling

- **F11 (medium): `-wx` is off for all of the app's own C++.** `strict = ours and not (mtcp and .cpp)` exempts claude.cpp, tools.cpp and xtbridge.cpp. Verifiers compiled all three with `-wx`: only xtbridge.cpp warns (W716/W389 from ip.h/tcp.h, W014). Exempt it alone, or `-wcd=716 -wcd=389 -wcd=14`. Note `-wx` is the maximum warning level, not warnings-as-errors; renaming tools.cpp to tools.c would break the link (no `extern "C"` in ui.h/tools.h). [scripts/build.py:143](../../scripts/build.py#L143)
- F40 (medium): the size report and DGROUP budget (Memory above).
- Low: `serial.log` removed after `Popen`, nil window, move it (F38); `sys.exit()` in build helpers makes build and test failures the same exit code (F86); `to_83('a.b.c')` writes `A.B` (F88); rundosbox repeats boottest's `-set` list and `MAX_TXT`/`MAX_PAYLOAD` duplicate 500, `MAX_TXT = tools.MAX_PAYLOAD` is the whole fix (F89); mTCP version pinned twice, `volid`/label use `now()` while files already get the fixed 1980 stamp, Python 3.11 floor unstated (F90); replay scrapes the log format and `trace()`'s 120-char cut means long lines replay truncated (F58).

### Tests and docs drift

- **F6 (high): `screen.c` has zero automated coverage.** The one real bug (13a7fc8) was visible only on the XT. Correction: DOSBox-X runs `machine=cga`, so the hwscroll/rebase path *is* executed by every CLAUDE boot test, just never asserted, and the emulated CGA wraps. The hook needs `les di, vram` in `vfill`/`vcopy` (they load ES from the `VRAM_SEG` immediate) and forced `hwscroll=1`. Invariant after a scroll: `scr_top() + 25*80 <= 102*80` (after #77 top = 6160, and +26 rows is legal); #78 rebases; `history` caps at 77; 25 rows survive. [lib/screen.c:337](../../lib/screen.c#L337)
- **F3 (medium): word wrap has no regression test**; `!wide`/`!long` exist but boottests only grep logs. A stub screen.c with `cells[25][80]`. Realistic width is `[lm, 79)`; `con_mark`/`con_rewind` have no callers (see F10); add C1's 77-char cases. [lib/console.c:107](../../lib/console.c#L107)
- **F50 (medium): `toolapi.c` is linked into cxtunit but never called.** `targ_append` (reopen at toolapi.c:26-27, lone trailing backslash, overflow paths), `targ_hex`/`targ_addr`/`targ_num`, `tout_puts` truncation, `tout_hexdump`'s exact line, and `sendResult`'s encoder moved into toolapi for a round trip against `test_tools.py`'s strings. [src/claude/tools.cpp:348](../../src/claude/tools.cpp#L348)
- **F27 (medium): CGA pixel packing is untested arithmetic that M6.3's PNG decoder must match.** Expected bytes check out (`PSET(0,0),3` gives 0xC0; y=1 lands at 0x2000; SCREEN 2 (639,199) at `0x2000+99*80+79` bit 0). `setMode()` does INT 10h, so the hook must also set `mode`/`maxX`. Document `point()`'s in-range precondition. [lib/gfx.c:33](../../lib/gfx.c#L33)
- **F14, F39 (medium): no runner; `--expect` lacks negatives and exit codes.** CLAUDE.md's drop.bat expectation (`connection #2`) is weaker than drop.bat's own header; both pass. `boottest.py` already has `--no-build` and repeatable `--expect`, so a `scripts/test.py` driven by `REM EXPECT:`/`REM BRIDGE` headers is thin; exclude the 86Box-only batches. Add `--expect "exit code 0"` (every batch echoes it) and explicit `--reject` per command, no default list ("FAIL" matches `-v` log lines). The mock's PASS is computed from real XT results, so only the display is unproven. [CLAUDE.md:13](../../CLAUDE.md#L13), [scripts/boottest.py:142](../../scripts/boottest.py#L142)
- **F19 (medium): `--prompt` help says "on top of the built-in one"; it is the whole prompt** since 5d7e491. [bridge/xtbridge.py:550](../../bridge/xtbridge.py#L550)
- Low: delete the dead `hasattr(s, 'client')` in `Resume.hang_up` and `TOUR_EXPECT` (F16, F56); shell's capture arithmetic untested beyond two tour steps (F74); `wrapInput` and the TIME parser are static in the app, and `setClock` does no range check so "2026-13-99 rejected" would fail today (F94); cxtunit's recorders append unbounded into `rec[600]`, nothing overflows today (C12); unit tests never run on the 86Box 8088, use `--done UNITS-DONE` not `UNITS-OK` (F64). Docs: README's lib row omits `edit`, `search`, `ems` (F54); ROADMAP says `wcl -0` (F62); 290 vs 285 KB, and DEVTOOLS.TXT goes stale under M6.2 (F63); convlog's type list misses two kinds (F55); PROTOCOL.md lists two of seven ERR texts and its "ERR ends the reply" is wrong for `ERR busy` (F61); `edit_replace`'s returns undocumented (F68); xttest.py says "(no networking)" while tccrun.bat uses the NE1000 through it (F91).

## 7. Strengths

- Headers are the documentation: every `lib/*.h` states purpose, contract, limits and grammar, matching the executors and `tools.py`; rationale lives where it is needed (CGA non-wrapping VRAM in screen.c, Nagle in xtbridge.py, `_dos_allocmem` in edit.c, the SS != DGROUP suspicion in shell.c's ISR).
- The layering is real and respected: lib never includes src; screen to console is a clean two-level stack; the only app-to-lib callback is `tool_idle`; the parsers (basdraw, play, raster, edit_replace) are hardware-free behind callback tables, which is why cxtunit runs 90+ assertions on DOS and why gfx.c and sound.c are thin.
- The CGA path is engineered for the real card: retrace-synced asm writes with a no-snow fast path, hardware scroll by CRTC start address with the footer re-blasted from a RAM mirror during vblank, `wait_vblank()` distinguishing vertical from horizontal blank, `rebase()` respecting the IBM CGA's non-wrapping display, VRAM never read on the hot path.
- Memory was measured before it was optimized: the link map is in the build, commits list KB won per change, mTCP is trimmed with reasons, the chat screen parks in EMS, edit.c's file buffer bypasses the C heap so `run` gets it back, and every argument/result size is bounded end to end to match the protocol.
- Low-level C discipline is right where it is dangerous: ISR hygiene, `(unsigned char)` on every ctype call and cell pack, no DOS handle leaks, type-correct varargs, 16-bit overflow probed in the raster tests, DOS critical errors turned into tool errors.
- The protocol is minimal and debuggable: text verbs, CP437 bytes straight to the screen, 500-byte server payloads for a 1 KB receive buffer, escape-aware chunking, a wire trace, `--replay`, and all history held on the bridge so the XT holds only what is on screen.
- Link loss, truncation and Esc are handled consistently on both sides: send marks the socket dead on a partial write, PING/PONG with distinct idle thresholds, RESUME with parked conversations and takeover, `tool_idle()` before long tools, `DONE stopped` then `STOP`, and truncation reported rather than hidden in every tool.
- The test story spans three levels (cxtunit, boottest `--expect` against the mock tooltour's 27 steps over all 19 tools, test_bridge.py over the state machine where bugs hide), the mock bridge sits on its own port so tests never reach a real key, and the Python is uniformly clean (pylint 10/10, dataclasses, justified disables).

## 8. Prioritized action plan

Ordered by payoff over effort. S = under an hour, M = an afternoon, L = a day or more.

1. **Done (`d7d09a4`).** Fix the three stack-memory bugs (S): `name[13]` in memory.c (F5); `kw[12]` into `Bas` (F21); `_snprintf`/length guard in `watchCheck` (F47); `snprintf` for `why` (F37); clamp `host` (F46). Add the memory_list and `PSET (1,1) 5` cases to cxtunit.
2. **Done (`d7d09a4`).** `#define NOTRACE` in claude.cfg (S): ~10 KB resident, one line; re-run net.bat and tools.bat (F12).
3. **Done (`916ea7a`).** Defer the console wrap and fix textart's 78 (S): C1 + F99, a showcase bug on every full-width line.
4. **Moot (`1bcfd23` removed the draw tool).** Depth-limit `factor()` and fix the ellipse seed (S): F20, C5; both are model-input paths into the resident client; both get a unit test.
5. **F2 half done (`TOOL_TIMEOUT`); F1 open.** Bound the tool-call wait on the bridge and check the 6 KB argument budget (S-M): F2 `wait_for`, F1 size check with the limit in the descriptions; no keepalive during tools.
6. **Make `-q` deny W tools and service mTCP in `ui_ask`** (S): F52; return a nonzero exit on ERR (F96).
7. **Done (`ea803a4`).** Build-size report with a DGROUP budget (S): parse the map after wlink, print per app, fail past 56 KB (F40); add `-zc` while there (C9, ~4-5 KB DGROUP).
8. **Moot (`1bcfd23` removed the draw tool).** Span fill and single-probe PAINT in the draw path (M): F4 `hspan` + row table, F31 `fillable()`, F29 trivial reject/16-bit fast path; 5-10x on filled drawings.
9. **Batch console painting into `scr_putn` runs** (M): F22, ~3x on reply painting and catch-up after tools; needs the flush points listed.
10. **Safe writes and device-name guard in dosfs** (M): C3 temp-and-rename for write/memory_write, C4 IOCTL 4400h check, F24 truncation marker; hoist the shared text loops into toolapi/textio while there (F25, F26).
11. **Done (`fa10de3`, as `lib/ticks.h`: Watcom owns the name `bios.h`).** Rollover-safe ticks (S): C6 `ticksSince()`, F28 constants; fixes the overnight reconnect stall.
12. **`scripts/test.py` and honest expectations** (M): F14 runner from `REM EXPECT:` headers, F39 `--expect "exit code 0"`/`--reject`, F19 help text, delete `TOUR_EXPECT` and the dead `hasattr` (F16, F56).
13. **Test hooks for screen.c, console.c and toolapi** (L): F6 `les di, vram` + forced hwscroll with the corrected invariants, F3 stub screen (done in `916ea7a`), F50 `targ_*`/`tout_*` round trips, F27 pixel packing bytes that M6.3's decoder will reuse (moot: gfx.c is gone, M6.3 starts fresh).
14. **Per-file `-wx` exemption** (S): only xtbridge.cpp (F11); `closeNonblocking()` for C2 while in that file.
15. **Honest tool status on link loss** (M): F15 `lost` status plumbed through `STATUSES`/`_settle`, and `sendResult` checking `ui_send`.

Deferred to M6.2's design session rather than done ahead of it: F7 (shell.c split), F13/F42/F45 (claude.cpp split, `ResumeState`), F100 (screen park into lib), F34/C10 (scratch union), F9 (INT 10h restore note).

## 9. Appendix

### A. Confirmed findings

Line numbers corrected by the verifiers are used where the original anchor was wrong (C2, C3, C5, C10, C14, F77, F89, C7, C8, F88).

| id | file:line | severity | title |
|---|---|---|---|
| F4 | [lib/gfx.c:33](../../lib/gfx.c#L33) | high | Every pixel of draw goes through 3 calls, duplicate clipping and a multiply by 80 — **gone with the draw tool, `1bcfd23`** |
| F5 | [lib/memory.c:74](../../lib/memory.c#L74) | high | tool_memory_list: strcpy of a 12-char 8.3 name into name[9] overflows the stack — **fixed in `d7d09a4`** |
| F6 | [lib/screen.c:337](../../lib/screen.c#L337) | high | screen.c (hardware scroll, rebase, footer mirror, scrollback) has zero automated coverage |
| F1 | [bridge/tools.py:284](../../bridge/tools.py#L284) | medium | Bridge never checks tool arguments against the XT's 6 KB budget |
| F2 | [bridge/xtbridge.py:383](../../bridge/xtbridge.py#L383) | medium | A tool call's lifetime is bound to the TCP connection; hung XT leaks the session — **fixed: `TOOL_TIMEOUT` in xtbridge.py** |
| F3 | [lib/console.c:107](../../lib/console.c#L107) | medium | Word wrap (console.c) has no regression test — **fixed in `916ea7a`** |
| F7 | [lib/shell.c:36](../../lib/shell.c#L36) | medium | shell.c fuses the INT 10h capture ISR, buffers, formatting and hold screen |
| F8 | [lib/shell.c:282](../../lib/shell.c#L282) | medium | Nothing is handed back to DOS before spawnl; the whole image stays resident |
| F9 | [lib/shell.c:287](../../lib/shell.c#L287) | medium | INT 10h hook restored blindly after spawn: a TSR's hook is silently removed (crash claim corrected) |
| F11 | [scripts/build.py:143](../../scripts/build.py#L143) | medium | -wx silently dropped for all of the app's own C++ |
| F12 | [src/claude/claude.cfg:4](../../src/claude/claude.cfg#L4) | medium | mTCP tracing compiled in: ~6 KB of trace strings plus ~150 call sites — **done in `d7d09a4`** |
| F13 | [src/claude/claude.cpp:875](../../src/claude/claude.cpp#L875) | medium | Bridge session logic (HELLO/AUTH/RESUME/PING/reconnect) lives in the app, not lib |
| F14 | [CLAUDE.md:13](../../CLAUDE.md#L13) | medium | No test runner; the matrix exists only as prose and drop.bat's expectation differs |
| F15 | [bridge/providers.py:361](../../bridge/providers.py#L361) | medium | A link drop mid-tool is reported to the model as 'Interrupted by the user' |
| F19 | [bridge/xtbridge.py:550](../../bridge/xtbridge.py#L550) | medium | --prompt help says "on top of the built-in one"; it is the whole prompt |
| F20 | [lib/basdraw.c:102](../../lib/basdraw.c#L102) | medium | Unbounded recursion in the expression parser on a 4 KB stack with stack checking off — **gone with the draw tool, `1bcfd23`** |
| F21 | [lib/basdraw.c:480](../../lib/basdraw.c#L480) | medium | bas_draw's 'unexpected char' error reads a keyword pointer into a dead stack frame — **fixed in `d7d09a4`, then gone, `1bcfd23`** |
| F22 | [lib/console.c:145](../../lib/console.c#L145) | medium | Streamed text is painted one character per call chain |
| F24 | [lib/dosfs.c:190](../../lib/dosfs.c#L190) | medium | read cuts lines over 160 characters silently; edit then misbehaves |
| F25 | [lib/dosfs.c:244](../../lib/dosfs.c#L244) | medium | The same read/write byte loops are copied into dosfs.c, memory.c, search.c and edit.c |
| F27 | [lib/gfx.c:33](../../lib/gfx.c#L33) | medium | CGA pixel packing is untested pure arithmetic that M6.3 screenshots will depend on — **gone with the draw tool, `1bcfd23`** |
| F29 | [lib/raster.c:18](../../lib/raster.c#L18) | medium | Lines are stepped pixel by pixel through off-screen space in 32-bit arithmetic — **gone with the draw tool, `1bcfd23`** |
| F31 | [lib/raster.c:132](../../lib/raster.c#L132) | medium | PAINT tests every pixel twice: FILLABLE calls point() two times per probe — **gone with the draw tool, `1bcfd23`** |
| F33 | [lib/search.c:28](../../lib/search.c#L28) | medium | Case-insensitive search calls toupper() twice per compared character |
| F37 | [lib/shell.c:290](../../lib/shell.c#L290) | medium | runOne's 'could not start' message can overrun why[120] — **fixed in `d7d09a4`** |
| F39 | [scripts/boottest.py:142](../../scripts/boottest.py#L142) | medium | boottest --expect lacks negative assertions and never checks CLAUDE.EXE's exit code |
| F40 | [scripts/build.py:112](../../scripts/build.py#L112) | medium | Large model is the only viable choice; DGROUP has ~14.6 KB headroom and no size report — **done in `ea803a4`** |
| F43 | [src/claude/claude.cpp:125](../../src/claude/claude.cpp#L125) | medium | history[8][901] = 7.2 KB of DGROUP for input recall |
| F46 | [src/claude/claude.cpp:959](../../src/claude/claude.cpp#L959) | medium | Several sprintf targets are sized to the typical case (offlineNote, drawHint col, lostReport) — **fixed in `d7d09a4`** |
| F47 | [src/claude/claude.cpp:1030](../../src/claude/claude.cpp#L1030) | medium | watchCheck's 160-byte buf overflows on realistic values and on the BIOS-area list — **fixed in `d7d09a4`** |
| F49 | [src/claude/tools.cpp:187](../../src/claude/tools.cpp#L187) | medium | restoreScreen frees an EMS frame pointer when the remap fails |
| F50 | [src/claude/tools.cpp:348](../../src/claude/tools.cpp#L348) | medium | XT side of the tool wire contract is untested although linked into cxtunit |
| F52 | [src/claude/tools.cpp:427](../../src/claude/tools.cpp#L427) | medium | Unattended (-q) runs block forever on the y/n prompt; ui_ask ignores the network |
| F99 | [src/claude/tools.cpp:156](../../src/claude/tools.cpp#L156) | medium | textart rows of 78 columns trigger the console's word wrap and split the picture — **fixed in `916ea7a`** |
| C1 | [lib/console.c:146](../../lib/console.c#L146) | medium | con_putc wraps eagerly when the last column fills: full-width lines get a blank line or an orphaned word — **fixed in `916ea7a`** |
| C2 | [lib/xtbridge.cpp:114](../../lib/xtbridge.cpp#L114) | medium | close() uses mTCP's blocking TcpSocket::close(): up to 15 s frozen UI when the link is dead |
| C3 | [lib/dosfs.c:241](../../lib/dosfs.c#L241) | medium | write and memory_write truncate the target in place; a full disk destroys the previous content |
| C4 | [lib/dosfs.c:18](../../lib/dosfs.c#L18) | medium | No guard against DOS device names: read of CON/AUX blocks with no Esc |
| C5 | [lib/raster.c:111](../../lib/raster.c#L111) | medium | ras_ellipse overflows 32-bit arithmetic (from radius ~181, b2*(2x+1)^2 term) — **gone with the draw tool, `1bcfd23`** |
| C6 | [src/claude/claude.cpp:1188](../../src/claude/claude.cpp#L1188) | medium | BIOS tick count used as monotonic; retryAt across midnight stalls reconnect — **fixed in `fa10de3`** |
| C9 | [scripts/build.py:112](../../scripts/build.py#L112) | medium | ~4-5.3 KB of literal strings sit in DGROUP; -zc moves them to code — **done in `ea803a4`** |
| F10 | [scripts/build.py:112](../../scripts/build.py#L112) | low | 67 unreferenced symbols linked; -zm does not help (grows code), delete six dead functions |
| F16 | [bridge/test_bridge.py:798](../../bridge/test_bridge.py#L798) | low | One 1050-line test file, no runner, dead hasattr check and TOUR_EXPECT |
| F17 | [bridge/tools.py:46](../../bridge/tools.py#L46) | low | Tool table hand-duplicated in Python and C with no cross-check of the prose limits |
| F18 | [bridge/xtbridge.py:51](../../bridge/xtbridge.py#L51) | low | Server state in module globals; parked conversations purged only on connect |
| F23 | [lib/dosfs.c:15](../../lib/dosfs.c#L15) | low | Five tool-only scratch buffers (~3.5 KB) could be one union |
| F26 | [lib/edit.c:254](../../lib/edit.c#L254) | low | Path validation duplicated because dosfs keeps it static; limits are literals in messages |
| F28 | [lib/gfx.c:172](../../lib/gfx.c#L172) | low | Timer-tick arithmetic and hardware addresses re-derived in every file — **ticks done in `fa10de3`; addresses remain** |
| F32 | [lib/screen.c:379](../../lib/screen.c#L379) | low | scr_scroll keeps a 1,760-byte copy of rows it already mirrors; SCR_FOOT_MAX is 10 for 8 |
| F34 | [lib/shell.c:23](../../lib/shell.c#L23) | low | 3.7 KB capture ring resident forever but only lives during a run call |
| F35 | [lib/shell.c:36](../../lib/shell.c#L36) | low | Two naming conventions coexist inside lib/*.c (play.c is not mixed) |
| F36 | [lib/shell.c:55](../../lib/shell.c#L55) | low | INT 10h hook divides by 2800 for every captured character (a few percent, not 20-30) |
| F38 | [scripts/boottest.py:106](../../scripts/boottest.py#L106) | low | serial.log is deleted after DOSBox-X has been started |
| F42 | [src/claude/claude.cpp:90](../../src/claude/claude.cpp#L90) | low | App state is ~40 loose file statics; M6.2 needs it as a struct |
| F44 | [src/claude/claude.cpp:211](../../src/claude/claude.cpp#L211) | low | Spinner redraws the whole 80-cell status row nine times a second |
| F45 | [src/claude/claude.cpp:214](../../src/claude/claude.cpp#L214) | low | claude.cpp is a 1214-line file holding six concerns |
| F48 | [src/claude/claude.cpp:1115](../../src/claude/claude.cpp#L1115) | low | C runtime is 34.8 KB of code; realistic saving 1-2 KB, not most of it |
| F51 | [src/claude/tools.cpp:406](../../src/claude/tools.cpp#L406) | low | runCall special-cases tools by string-comparing names |
| F53 | [src/claude/ui.h:12](../../src/claude/ui.h#L12) | low | ui.h mixes screen, network and dispatch; claude.cpp/tools.cpp header cycle |
| F54 | [README.md:150](../../README.md#L150) | low | README layout table omits edit, search and ems — **fixed, 2026-10-29** |
| F55 | [bridge/convlog.py:5](../../bridge/convlog.py#L5) | low | convlog docstring missing session_resume and system_prompt |
| F56 | [bridge/mockchat.py:96](../../bridge/mockchat.py#L96) | low | TOUR_EXPECT is dead scaffolding with a stale comment |
| F57 | [bridge/providers.py:94](../../bridge/providers.py#L94) | low | has_key() always says yes for anthropic |
| F58 | [bridge/replay.py:19](../../bridge/replay.py#L19) | low | Replay scrapes the debug log; trace truncation at 120 chars plays back cut text |
| F59 | [bridge/xtbridge.py:170](../../bridge/xtbridge.py#L170) | low | Overlong-line recovery discards 4096 bytes, not the rest of the line |
| F60 | [bridge/xtbridge.py:587](../../bridge/xtbridge.py#L587) | low | start() reads nine settings through getattr(args, ..., default) |
| F61 | [docs/PROTOCOL.md:19](../../docs/PROTOCOL.md#L19) | low | PROTOCOL.md does not list the bridge's ERR texts or verb case-insensitivity |
| F62 | [docs/ROADMAP.md:20](../../docs/ROADMAP.md#L20) | low | ROADMAP names wcl -0; the build uses wcc/wpp + wlink |
| F63 | [docs/ROADMAP.md:93](../../docs/ROADMAP.md#L93) | low | Free-memory figure differs: 290 KB vs 285 KB |
| F64 | [image/tests/units.bat:3](../../image/tests/units.bat#L3) | low | Unit tests only run under DOSBox-X's 80186, never on the 86Box 8088 |
| F65 | [lib/basdraw.c:368](../../lib/basdraw.c#L368) | low | turtle() and statement() are the hardest functions to follow; literal pooling dependence — **gone with the draw tool, `1bcfd23`** |
| F66 | [lib/edit.c:196](../../lib/edit.c#L196) | low | Edit makes 3 full + 2 partial passes over a 60 KB file |
| F68 | [lib/edit.h:27](../../lib/edit.h#L27) | low | edit_replace's return values are undocumented |
| F69 | [lib/gfx.c:22](../../lib/gfx.c#L22) | low | gfx.c declares ras twice and shadows its maxX — **gone with the draw tool, `1bcfd23`** |
| F70 | [lib/gfx.c:199](../../lib/gfx.c#L199) | low | Busy-wait loops recompute a long mul/div per iteration and do not service the network — **gone with the draw tool, `1bcfd23`** |
| F71 | [lib/memory.h:1](../../lib/memory.h#L1) | low | lib/ mixes platform modules with tool executors; 'memory' is ambiguous |
| F72 | [lib/screen.c:50](../../lib/screen.c#L50) | low | One cell per horizontal retrace is conservative; two fit |
| F74 | [lib/shell.c:136](../../lib/shell.c#L136) | low | shell.c's capture arithmetic and command-list parsing untested beyond two tooltour steps |
| F75 | [lib/shell.c:323](../../lib/shell.c#L323) | low | 'Wait for a key or N seconds' is written three times |
| F76 | [lib/sysinfo.c:10](../../lib/sysinfo.c#L10) | low | EMS presence check implemented twice, differently |
| F77 | [lib/sysinfo.c:71](../../lib/sysinfo.c#L71) | low | sysinfo depends on screen only for the adapter type, and conflates it with hwscroll |
| F78 | [lib/toolapi.c:73](../../lib/toolapi.c#L73) | low | targ_num's comment says 'decimal or 0x hex' but strtol base 0 parses octal |
| F79 | [lib/toolapi.c:141](../../lib/toolapi.c#L141) | low | tout_printf uses vsprintf into a 300-byte stack buffer with no bound |
| F80 | [lib/toolapi.c:160](../../lib/toolapi.c#L160) | low | Hex dumps call sprintf once per byte |
| F81 | [lib/toolapi.h:43](../../lib/toolapi.h#L43) | low | tool_esc() is really the tool tick |
| F82 | [lib/xtbridge.cpp:157](../../lib/xtbridge.cpp#L157) | low | poll() memmoves the rest of the receive buffer per line and scans twice when idle |
| F83 | [lib/xtbridge.cpp:193](../../lib/xtbridge.cpp#L193) | low | PROTOCOL.md says 4096, the client silently truncates at 1024 |
| F84 | [lib/xtbridge.h:13](../../lib/xtbridge.h#L13) | low | xtbridge.h exports mTCP's types.h and a C++ class |
| F85 | [lib/xtbridge.h:16](../../lib/xtbridge.h#L16) | low | Line buffers sized 1024 while server payloads cap at 500 (tx must stay >= 910) |
| F86 | [scripts/build.py:65](../../scripts/build.py#L65) | low | sys.exit() inside build library functions |
| F87 | [scripts/build.py:112](../../scripts/build.py#L112) | low | Everything is -os; only screen.c compiles to IMUL; measure before -ot |
| F88 | [scripts/fatimg.py:52](../../scripts/fatimg.py#L52) | low | to_83 accepts names with embedded dots |
| F89 | [scripts/rundosbox.py:39](../../scripts/rundosbox.py#L39) | low | DOSBox-X command line and the 500-byte constant exist in two copies |
| F90 | [scripts/setup-tools.ps1:12](../../scripts/setup-tools.ps1#L12) | low | mTCP version pinned twice; volid/label use now(); Python 3.11 floor unstated |
| F91 | [scripts/xttest.py:7](../../scripts/xttest.py#L7) | low | xttest.py docstring says "(no networking)" but tccrun.bat uses the network |
| F92 | [src/claude/claude.cfg:25](../../src/claude/claude.cfg#L25) | low | mTCP buffer sizing is already tuned (~28.5 KB; 4 xmit buffers actually allocated) |
| F93 | [src/claude/claude.cpp:122](../../src/claude/claude.cpp#L122) | low | lineStart[INPUT_MAX / 2] reserves 900 bytes for at most ~26 lines |
| F94 | [src/claude/claude.cpp:216](../../src/claude/claude.cpp#L216) | low | Input-box word wrap and HELLO parsing have no unit test |
| F95 | [src/claude/claude.cpp:500](../../src/claude/claude.cpp#L500) | low | newChat ignores a failed NEW (F2/Ctrl-N offline) |
| F96 | [src/claude/claude.cpp:585](../../src/claude/claude.cpp#L585) | low | -q runs exit 0 after an ERR reply |
| F97 | [src/claude/claude.cpp:1027](../../src/claude/claude.cpp#L1027) | low | -watch ships in every build — **removed in `1eb59cb`** |
| F98 | [src/claude/claude.cpp:1168](../../src/claude/claude.cpp#L1168) | low | Comma-operator sequencing and an underscore-suffixed local |
| F100 | [src/claude/tools.cpp:172](../../src/claude/tools.cpp#L172) | low | EMS-or-heap screen save/restore sits in the app |
| C7 | [lib/screen.c:242](../../lib/screen.c#L242) | low | Adapter detection is 'not EGA/VGA = CGA'; MDA/Hercules spins forever |
| C8 | [lib/screen.c:458](../../lib/screen.c#L458) | low | scr_suspend and the -noscroll scroll path read video memory unsynchronized |
| C10 | [lib/raster.c:129](../../lib/raster.c#L129) | low | Per-module static data measured; tool-scratch total ~9 KB — **raster share gone, `1bcfd23`** |
| C11 | [bridge/cp437.py:17](../../bridge/cp437.py#L17) | low | Tabs reach the screen as CP437 glyphs; the '\t' fallback is dead code |
| C12 | [tests/unit/cxtunit.c:44](../../tests/unit/cxtunit.c#L44) | low | Harness records with unbounded sprintf into rec[600] |
| C13 | [lib/shell.c:269](../../lib/shell.c#L269) | low | A run of CD silently moves CLAUDE's own current directory |
| C14 | [lib/toolapi.c:26](../../lib/toolapi.c#L26) | low | targ_append only reopens the newest key; adjacency rule undocumented |

Totals: 3 high, 38 medium, 69 low (110 confirmed).

### B. Rejected by the verifiers

- **F73** "Software scroll copies 24 rows through a stack buffer" (1/3): `row` is static, not stack; DOSBox-X runs `machine=cga` so every emulator test takes the hardware path; with CLAUDE's footer it is 16-20 rows; the 20x figure was off (6-7x). Kernel kept in C8: use `_fmemmove` for an overlapping copy if the EGA/VGA path is ever tuned.
- **F41** "Every build recompiles everything and every test builds the image twice" (true but 3/3 "none"): measured at ~1 s compile and 0.1-0.26 s per image fill against a DOSBox boot of tens of seconds; zipfile reads only named members.
- **F67** "Long explanatory prose for the model lives in far const" (0/3): wrong for the cited file. `edit.c` is compiled by wcc and its literals land in DGROUP `CONST`, the opposite of the claim (that fact became C9); the model-facing messages are the diagnostics it needs and should stay.
- **F30** "Huge-radius CIRCLE loops for hours with no Esc check" (1/3): region 1 runs ~rx iterations, region 2 is bounded by ry+1; simulated worst case ~32,769 iterations, a few seconds; `.001` aspect is rejected as zero. The overflow half was real and is reported as C5 with the exact fix one verifier derived.
