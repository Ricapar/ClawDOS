# 8088 MPH: techniques from the authors' write-ups (notes for an RGB/5153 technique guide)

Target: IBM 5150/5160, 8088 @ 4.77 MHz, IBM CGA (MC6845), PC speaker. We drive an IBM 5153 **RGBI** monitor,
so anything that depends on NTSC artifact colour is marked **composite-only** and kept to a line.

Every number below comes from a page listed under "Sources read". Where a value is standard PC knowledge
not stated in those pages, it is marked *(not from the write-ups)*.

Abbreviations from reenigne's CGA wait-state post: **hdot** = one 14.318 MHz tick (~70 ns, a pixel in 640-wide modes);
**ccycle** = one CPU cycle = 3 hdots; **hchar** = 8 hdots (80-column character); **lchar** = 16 hdots (40-column
character / low-res character clock); one PIT tick = 12 hdots = 4 CPU cycles.

---

## 0. Composite-only items (one line each, not usable on RGB)

- **1024-colour mode** (80x100, 80-col text + colour burst, CRTC restarted every 2 scanlines so only the first glyph scanline of 0x55/0x13/0xB0/0xB1 shows): composite-only colours. (The CRTC stacking it uses is RGB-viable, see section 4.)
- **512-colour mode** (80-col text, chars 0x55 / 0x13, set-and-forget): composite-only.
- **160x200 16-colour artifact mode** (mode 6 / 640x200 with colour burst on; used by sprites, polygons, title screen): composite-only.
- **Mode 6 foreground-colour fades** (foreground 15, 7, 8, 0 dims all artifact colours): composite-only.
- **Hsync width = 0 (means 16 on MC6845) to restore the colour burst in +HRES mode**; also breaks on HD6845, final version adds a calibration screen to avoid it: composite-only.
- **Border colour brown (6) in 80-col mode to get a colour burst**: composite-only; RGB ("TTL") monitors don't care about the burst.
- **Old-style vs new-style CGA colour tuning, CGA2NTSC model, portrait conversion**: composite-only (RGBI direct colours do differ slightly between the two card revisions, but the art re-do was for composite).
- **"+BW" bit 2 of the mode register** (kills the colour carrier): composite-only; no effect on TTL monitors.

---

## 1. Platform timing facts (RGB-viable: yes, this is the foundation for everything)

**What it is.** One 14.318 MHz crystal feeds everything, so the CPU, PIT and CGA run at fixed ratios and can be
counted against each other.

**Numbers.**
- CPU = 14.318/3 = 4.77 MHz; PIT = /12 = 1.193 MHz ("105MHz/88"); CGA lchar = /16. (reenigne, CGA lockstep; MOD player)
- The 8088 needs 4 cycles per byte read, so its effective rate is about 4.77/4 = 1.19 MHz. (Trixter)
- One scanline = 912 hdots = 304 CPU cycles = 76 PIT ticks. One frame = 262 scanlines = 238,944 hdots = **19,912 PIT ticks**
  (262*76). (Scali sprites; MartyPC Lake; reenigne CGA lockstep)
- 1 CPU cycle = 3 hdots exactly, so cycle counts map straight onto beam position. (MartyPC Lake)
- MUL is microcoded, 69-154 cycles with register operands; an 8-bit MUL takes 69 + 1 per set bit in AL, which reenigne
  uses as a variable delay. (Trixter comments; reenigne CGA lockstep)
- Published 8088 instruction timings are best case (full queue, no bus delays). Real timing depends on the prefetch
  queue and BIU state, so you have to measure. (MartyPC CPU test)

**Source:** https://trixter.oldskool.org/2015/04/07/8088-mph/ , https://www.reenigne.org/blog/adventures-in-cga-lockstep/ ,
https://martypc.blogspot.com/2025/05/emulator-debugging-area-5150s-lake.html , https://scalibq.wordpress.com/2015/04/19/8088-mph-sprites-where-were-going-we-dont-need-sprites/

---

## 2. DRAM refresh (PIT channel 1 -> DMA channel 0) (RGB-viable: yes)

**What it is.** The PC refreshes DRAM by having PIT channel 1 trigger DMA channel 0 every 18 PIT ticks (72 CPU cycles).
Each refresh steals 4 CPU cycles from the bus, about 5.6%. Because the refresh lands at a random phase, it adds jitter
to any cycle-counted code.

**How they handled it.**
- **Reset the refresh phase.** The CPU speed test reprograms channel 1 with its default value first, just to restart it at a known phase:
  `mov al,54h / out 43h,al` (counter 1, LSB only, mode 2), then `mov al,12h / out 41h,al` (count 18), then `cli`.
  With refresh on, the test body runs about 5% slower (6676 cycles traced on a real XT, versus 6328 with no DMA). (MartyPC CPU test)
- **Pick a refresh period that divides your loop.** The Kefrens loop is 304 cycles per scanline. That is not a multiple of 72,
  so it would need up to nine different scanline routines. Instead they set PIT1 to **19** (every **76** cycles), which gives
  exactly 4 refreshes per scanline in the same places every time. This is out of spec but worked on every machine they tried.
  The credits went back to 18/72 because their inner loop is a multiple of 72 and not 76. (reenigne "More 8088 MPH"; Trixter)
- **Or make the loop divide the default.** The MOD player's 288-cycle sample loop = 4 x 72, so the default 18 gives exactly
  four refreshes per sample in the same code positions and no jitter. (reenigne MOD player)
- **Turning refresh off entirely** (reenigne, 2012, used during lockstep experiments): it makes timing consistent, but your
  code must touch every DRAM row itself. 4116 parts need 128 consecutive bytes within 2 ms, 4164 need 256 within 4 ms,
  41256 need 512 within 8 ms. Touching 512 consecutive bytes every 8 ms covers all three. A long enough unrolled inner
  loop (over 127 bytes of code) does this by itself. An attempt to slow refresh to about 1/19-1/20 on a PC1512 was unstable beyond that.
- **Gotcha (relevant to a PicoMEM NE2000):** any other DMA or IRQ source breaks lockstep. The Kefrens bars went unstable on a real
  5150 because of a network driver; a clean boot fixed it. Disk DMA lands at unpredictable points; HLT after it and resync on a timer IRQ. (reenigne "More", CRTC lockstep; Scali "came about")

**Sources:** https://www.reenigne.org/blog/more-8088-mph-how-its-done/ , https://www.reenigne.org/blog/8088-pc-speaker-mod-player-how-its-done/ ,
https://martypc.blogspot.com/2023/08/the-8088mph-cpu-test.html , https://www.reenigne.org/blog/how-to-get-away-with-disabling-dram-refresh/

---

## 3. CGA memory: wait states, bandwidth, snow (RGB-viable: yes, this is the main constraint on RGB too)

**Wait states.** Every CPU access to CGA RAM waits "1 hchar, then the next lchar, then the next ccycle": 3 to 8 cycles
depending on the CPU/CGA clock phase. There are 16 phases: one gives 3, and three each give 4, 5, 6, 7 and 8. The average is
**5.8125 cycles**. The waits apply at every beam position, including retrace, because the CRTC keeps refreshing CGA RAM. (reenigne wait states)
MartyPC derives the same table from the schematic: 5,5,4,4,4,3,8,8,8,7,7,7,6,6,6,5 cycles by phase.
- REP STOSW gets close to the worst possible wait-state pattern. (reenigne)
- `STOSW NOP NOP` costs the same to CGA as `STOSW NOP`, so an extra NOP per word is free. Interleaving computation
  between VRAM accesses hides the waits. (reenigne comments)
- REP MOVSW to CGA RAM, refresh off: 256 KB/s (112 hdots/word), against 382 KB/s to system RAM. (reenigne comments)
- Scali's working figure for CGA writes: **about 170 KB/s**. So clearing 8 KB takes about 0.05 s, a ceiling of about 20 fps
  before you draw anything. This number drove the delta renderer, the compiled sprites, and "small updates per frame" in general. (Scali polygons)
- Address bit A14 isn't decoded: the 16 KB at B8000-BBFFF mirrors at BC000-BFFFF. reenigne uses this for wraparound. (MartyPC wait states; reenigne MOD player)

**Snow (80-column text mode only).** CGA RAM is single-ported and the CPU gets priority. The wait-state logic is timed
on the lchar clock, which suits 40-column and graphics modes. 80-column mode needs twice the bandwidth, and IBM never added the
extra wait. So a CPU access during active display makes the CRTC latch whatever is on the bus, and you get snow. It shows in
vertical columns where the one wait state happens to cover it and where it doesn't. 40-column text and all graphics modes are snow-free. (Scali final version and Area 5150 reflection; MartyPC wait states)

**How they dealt with snow:**
- **Avoid 80-col mode when writing a lot.** The moiré, voxel and similar chunky effects use 40-col text. (reenigne "More"; Scali A5150)
- **Write only while display enable is off.** The plasma (80-col) buffers writes in registers or RAM and flushes them when the
  status port reports blanking. That cost it from 60 fps to 20 fps. Writes were meant to land so the snow fell in the border,
  which assumed full lockstep with refresh at 19. (Trixter; Scali final)
- **Power-on PIT/CGA phase.** The PIT can start on any of 4 CPU cycles relative to the CGA clock, and software can't change it.
  For the final version they wrote a tool that reports the phase, and you power-cycle until it's right. Only the snow-timed plasma
  needs this; the other cycle-exact parts don't depend on it. (Scali final)
- **Effects that change only CRTC registers produce no snow.** Changing the start address or frame structure never touches VRAM.
  Area 5150 builds most of its 80-col effects on this. (Scali A5150)

**Sources:** https://www.reenigne.org/blog/the-cga-wait-states/ , https://martypc.blogspot.com/2023/05/exploring-cga-wait-states.html ,
https://scalibq.wordpress.com/2015/04/23/8088-mph-the-polygons/ , https://scalibq.wordpress.com/2015/08/02/8088-mph-the-final-version/ ,
https://scalibq.wordpress.com/2022/09/16/area-5150-a-reflection/

---

## 4. 6845 CRTC tricks (RGB-viable: yes, all of these are timing tricks independent of the output)

### 4a. Tweaked window sizes / text-mode "graphics"
- **160x100x16 (80-col text, 2 scanlines per row, 100 rows):** fill the screen with 0xDE so each cell is two "pixels", background
  and foreground, any of 16 colours. Turn blinking off. Writing to it means 80-col snow. "ANSI from Hell" uses the whole ROM
  charset in this layout to imitate about 640x200x16. This works on RGB (VileR calls it "purely a text mode / RGBI trick"). (int10h)
- **80x50 in 40-col text:** set scanlines-per-row to 4 (R9 = 3) with the 0xDE half-block. Only 47 rows got updated per 60 Hz frame. (reenigne "More")
- **Smaller windows to free VRAM.** 140x170 (sprite part, to leave room for a scrolling 60-pixel tile) and 112x140 in 8 KB (vectorballs:
  a real hidden page, flipped with the start address). Changing the CRTC's displayed size changes how much VRAM a frame uses. (Scali sprites)
- **80-col text holds only 102 scanlines of unique data:** 160 bytes/scanline into 16384. A full 200-line image has to reuse scanlines;
  the Area 5150 lake mirrors the bottom half with a sine offset. (Scali A5150)

### 4b. Start address register (R12/R13) behaviour
- **Latched once per CRTC frame.** You can write it at any time and it takes effect at the next frame, so a scroll or page flip needs no
  vblank wait. The sprite part sets the next scroll offset right before drawing. The vectorballs part *does* wait for vsync before drawing into
  the old page, so it isn't drawing into what's still on screen. (Scali sprites; Scali "Latch onto this")
- In 200-line graphics the CRTC works in 100 rows (two interleaved banks), so the start address only moves in **2-scanline** steps. (Scali sprites; Trixter)
- reenigne's MOD player uses a v-instruction to set the CRTC start byte for hardware text scrolling: `mov dx,03D4h / mov ax,990Dh / out dx,ax`,
  with the high byte patched. That is index 0Dh (start address low). At exit it sets the start to "8192 minus scrolled positions" so the DOS prompt follows the text. (reenigne MOD player)

### 4c. Several CRTC frames per CRT frame ("frame stacking"): the core RGB-viable trick
- The 6845 can't make more than 128 rows (plus up to 32 adjust scanlines), and the monitor needs 262 lines per field. So you run
  **several short CRTC frames per monitor frame and suppress vsync on all but one**. Each new CRTC frame reloads the start address, so
  each strip can show any part of VRAM. (reenigne 1K colours)
- **Smallest frame:** 2 scanlines (R4 vertical total = minimum, one row). The MC6845 handles one-scanline frames badly. (reenigne "More", CRTC lockstep; MartyPC Lake)
- **Vsync suppression:** set R7 (vsync position) above R4 so the stacked mini-frames never reach it. (MartyPC Lake)
- Examples: 1K images = 100 CRTC frames x 2 scanlines, with no vblank between them. The wide credits-intro picture = one 100-scanline frame at
  1 scanline per row, then a 162-scanline frame at 2 per row. (reenigne 1K colours; Scali A5150)
- **Effects from it:** the "drip" (start address changed every 2 lines), the Kefrens line buffer, and in Area 5150 most effects
  (checkerboard, twister, parallax, sine scroller, wobble, lake): repeated or offset scanlines in 80-col RGBI with no VRAM writes, so no snow. (Trixter; Scali A5150)
- **1-scanline resolution (Area 5150):** fit **two logical rows side by side on one scanline**. The dummy half is hidden in the horizontal blank, with
  the CRTC reprogrammed **8 times per scanline**, cycle-counted. That gives a new start address on every scanline, and 60 fps vertical scrolling in
  1-scanline steps. Side effect: the CRTC counts a line at each R0 match, so vsync came out 8 scanlines tall instead of 16. (MartyPC Lake; Scali A5150)
- **Mode switching per scanline:** Area 5150's glenz part shows 40-col text at the top and bottom and 320x200x4 in the middle. The object moves
  vertically by moving the switch point. Switching mid-scanline isn't reliable, and switching between 80-col text and the other modes isn't reliable either. (Scali A5150)
- **Hazards:** the 6845 compares with `==`. If you set R0 or R4 below a counter that has already passed it, the counter runs on until it wraps (or hits R7),
  which gives erratic hsync and vsync if your timing slips. Rewriting R7 mid-frame can produce a "phantom vsync": a 16-line black bar for one frame.
  int10h gives necessary conditions with row counter r, old R7 f, new R7 t: `r&7==f&7`, `r&~f&0x78==0`, `f&(t^r)&0x78==0`, `r&~t&0xf==0`.
  It hits more cases on HD6845 than on MC6845, and steady refresh makes it deterministic. Not all IBM CGAs have a Motorola MC6845; some have a Hitachi HD6845. (MartyPC Lake; int10h phantom vsync; Scali final)

### 4d. Raster colour changes
- The palette/colour register (3D9h *(port number not from the write-ups)*) takes effect immediately, so writing it in a timed loop paints raster bars.
  reenigne verified lockstep by running an unrolled `INC AX / OUT DX,AL` loop and looking at where the colour changes landed. The Kefrens loop does an `out dx,al` per scanline for raster bars. (reenigne lockstep posts; "More")

**Sources:** https://www.reenigne.org/blog/1k-colours-on-cga-how-its-done/ , https://www.reenigne.org/blog/adventures-in-crtc-lockstep/ ,
https://martypc.blogspot.com/2025/05/emulator-debugging-area-5150s-lake.html , https://int10h.org/blog/2023/03/cga-6845-crtc-phantom-vsync-glitch/ ,
https://int10h.org/blog/2015/04/cga-in-1024-colors-new-mode-illustrated/ , https://scalibq.wordpress.com/2015/09/13/latch-onto-this-its-all-relative/

---

## 5. Finding the beam: polling, timer-as-raster-counter, light pen, lockstep (RGB-viable: yes)

- **Status port 3DAh:** bit 3 = vertical retrace, bit 0 = display enable inverted (1 = outside the active area: hblank, vblank or border).
  To sync to a frame: if in vsync, wait until it ends; wait for vsync; wait for it to end; then poll bit 0 going 1->0 for the first visible line.
  Then program PIT0. Polling 3DAh flat out leaves no CPU for anything else. (MartyPC Lake)
- **Software vsync interrupt:** PIT0 (IRQ0/INT 8) with count **19912** fires at the same beam position every frame, because the PIT and CGA share
  a clock. 8088 MPH's loader provides this as a service (hook/unhook/disable/reinit), firing just after the last displayed line. (Trixter; Scali sprites)
- **Timer as reverse scanline counter:** with PIT0 at 19912 restarting at the bottom of the screen, reading the counter gives beam position x76.
  The sprite part polls it to start erasing and redrawing right after the beam passes the sprite's last line. No vsync wait is needed; one sprite per frame self-synchronises. (Scali sprites)
- **Jitter-free multiple IRQs per frame (latch trick):** in mode 2 or 3, writing a new count to the PIT data port *without* a command byte
  latches it for the next reload, and the current count runs on. Chain several intervals that **sum to 19912** and you get stable raster splits with
  no drift, because no cycles go into reprogramming. You can only change the *next* interval. (Scali "Latch onto this")
- **Auto-EOI:** reinitialise the 8259 with the auto-EOI bit in ICW4 so handlers skip the EOI write. (Scali "Latch onto this")
- **HLT before the expected IRQ** so it isn't delayed by whatever instruction is running. Measured on a real 5150: about 7 cycles to wake from HLT,
  then about 90 cycles from INTR to the first ISR instruction (INTA cycles, IVT fetch, a refresh hit), which is 30 hdots. HLT with INTR already high
  doesn't halt. Area 5150's Lake chains **8 timer ISRs**: the first four do clock lockstep, the rest position the effect ISR to land at about column 723 of
  scanline 20. (MartyPC Lake; reenigne CGA lockstep)
- **Light-pen latch as a beam reader:** reading or writing **3DCh** sets the light-pen strobe, so the CRTC latches the current address into its light-pen
  registers; **3DBh** clears it. Resolution is one character horizontally and 2 scanlines in graphics modes. IBM CGA only. Scali rejected it for the
  sprite part because it isn't scanline-exact in 200-line modes. (reenigne light pen; Scali sprites)
- **CPU/CGA lockstep (reenigne):** start with 16 possible CPU/CGA phases. Do a CGA access (16 -> 3 phases), delay A, another access (3 -> 2), delay B,
  another (2 -> 1). Only A and B mod 16 matter, so search the 256 pairs. Verify by timing palette writes against a cycle-counted diagonal line;
  lockstep is good when the line ends agree mod 48 hdots. For **CRTC** lockstep (CRTC char clock versus CGA lchar is also random at start-up):
  set up one visible scanline plus one overscan scanline, read display enable, then take delay X or X+3 lchars. Lockstep survives until you wait on an
  external event (disk, serial, keyboard...); keyboard polling can keep it if every path has the same length. (reenigne CGA and CRTC lockstep)
- **Verifying timings:** the 8088 MPH CPU test times a long instruction mix with PIT0 (mode 2, count 65536) after resetting PIT1. A pass is
  1668-1688 PIT ticks (the window is 80 CPU cycles). MartyPC and reenigne's xtserver bus sniffer give cycle traces from a real XT. (MartyPC CPU test)

**Sources:** https://martypc.blogspot.com/2025/05/emulator-debugging-area-5150s-lake.html , https://scalibq.wordpress.com/2015/09/13/latch-onto-this-its-all-relative/ ,
https://www.reenigne.org/blog/cga-reading-the-current-beam-position-with-the-lightpen-latch/ , https://www.reenigne.org/blog/adventures-in-cga-lockstep/ ,
https://www.reenigne.org/blog/adventures-in-crtc-lockstep/ , https://martypc.blogspot.com/2023/08/the-8088mph-cpu-test.html

---

## 6. Code techniques (RGB-viable: yes)

- **Unrolled code, one copy per object:** starfield (948 copies), moiré (940 copies), Kefrens (200 copies of the 304-cycle scanline body),
  MOD mixer (4 channels unrolled). Unrolling also covers DRAM refresh when refresh is off. (reenigne)
- **Self-modifying immediates ("9999"/"99" placeholders):** frequencies, waveform numbers, loop counts and segment values are patched into the
  instruction stream rather than loaded from memory. (reenigne MOD player, "More")
- **Stack as a data pointer:** `pop` is the fastest sequential read. Moiré reads its bitmap with `pop ax` (SP = pointer, BP = second pointer).
  Kefrens does `mov sp,[bx]` then a run of `pop di / pop cx / pop ax` to fetch the masks and colours for the scanline. The MOD player applies patches
  with `pop bx / pop word [cs:bx]` (data address and value off the stack). Interrupts must be off. (reenigne)
- **Prefetch-queue exploit:** in the MOD player, the `mov cl,99` right after the `pop word [cs:bx]` that patches it runs *unpatched*, because the 8088
  already has it in its queue. DOSBox runs the patched version, so there's a debug switch. (reenigne MOD player)
- **Equal-length paths:** the MOD player pads the non-patch path with `times 15 nop` so both paths take exactly 288 cycles. The NOP area later holds
  "v-instructions", which make up a tiny bytecode. (reenigne MOD player)
- **Sideways tables:** put the index you vary least in the high byte, so `mov bl,imm` picks the entry without a shift. (256x256 waveform table:
  low byte = waveform, high byte = position. The Kefrens big table is stored sideways too, with DS reloaded per scanline.) (reenigne)
- **Precalc versus compression trade-off:** pklite won because compressed load+unpack from floppy beat loading uncompressed. Kefrens compresses
  rasters and precalculates motion (the 327 KB table) at start-up. (Trixter; reenigne "More")
- **Compiled sprites** (section 7c) and **delta rendering** (section 7g).
- **Loader API (Trixter):** about 450 bytes of resident services. It plays background music, animates the "megademo" text with the start address, runs each part as a
  DOS child process (.EXE/.COM, so each author used their own toolchain), and handles the ready/start handshake (part decompresses and precalcs ->
  signals ready -> loader clears text -> signals go). It also provides the software vretrace interrupt and a countdown timer. It needs DOS 2+
  (the final version defaults the filename because DOS 2 can't give the program's own path). (Trixter; Scali final)

**Sources:** https://www.reenigne.org/blog/more-8088-mph-how-its-done/ , https://www.reenigne.org/blog/8088-pc-speaker-mod-player-how-its-done/ ,
https://trixter.oldskool.org/2015/04/07/8088-mph/

---

## 7. The effects

### 7a. Intro / fake DOS text (Trixter). RGB-viable: technique yes (art composite)
The text mode is emulated in a graphics mode: BIOS-like text output, with cursor and text blink timed like the 6845's. The title "unrolls" by brute-force
copying new scanlines into VRAM during retrace, because the start address can't be set per scanline in graphics. The fade-out ANDs a mask into
successive lines. Final version: palette adjusted on the fly as the title rolls, using PIT latch timing. (Trixter; Scali final; Scali latch)

### 7b. Loader "bobbing" text (Trixter). RGB-viable: yes
Software vretrace ISR right after the last displayed line changes the start address. A flag tells the ISR when to erase, using REP STOSW black lines.
It moves only in even-line steps (2 scanlines per CRTC row), as a CPU-saving compromise. (Trixter)

### 7c. Sprite part: DeLorean over the scrolling building (Scali). RGB-viable: technique yes (art is 160x200 composite)
- **Compiler:** takes a bitmap plus a separate transparency mask (so black can be opaque). It classifies each word or byte as fully opaque, fully transparent
  or mixed, and emits a hand-tuned template for each. It prefers `movsw`/`movsb` from a data stream over immediates. Transparent runs and line changes become
  merged pointer adds. It does all even lines and then all odd lines, so the bank-switch offsets are short encodings. It makes a pre-shifted variant per pixel
  phase and per even/odd start line.
- **Erase routines** are compiled too: `stosw`/`stosb` to black at byte granularity, or `movsw`/`movsb` restoring from a background image (software bobs).
- **Scroll:** the background is a 60-pixel repeating tile. A 140x170 tweak mode leaves VRAM to scroll a new tile in. Scroll = start address (latched, 2-line steps)
  mod 60, and the sprite target address is compensated.
- **Racing the beam:** a single buffer, so after PIT0 = 19912 the counter is polled. Erase and redraw start once the beam passes the sprite's last line,
  and the whole sprite must be drawn within one frame. Music runs in the timer ISR. Motion is keyframes with Hermite/cosine interpolation.
- **Fade to white:** a table maps each colour to the next brighter one in a gradient; it is applied repeatedly with a sideways sweep, because a full
  read-modify-write of the screen doesn't fit in a frame. This works on RGB with RGBI gradients. (Scali sprites)

### 7d. Vectorballs (Scali). RGB-viable: yes
30 balls at 30 Hz (32 ran acceptably). A 112x140 window in 8 KB gives a real back buffer. Balls are z-sorted, erased to black with the compiled erase,
drawn with compiled sprites, then the start address flips during vsync. (Scali sprites; Trixter)

### 7e. Moiré / circle interference (reenigne). RGB-viable: yes (40-col text, 16 RGBI colours)
40-col text, R9 = 3 (4 lines/row), 0xDE cells -> 80x50, 47 rows drawn. The two circle bitmaps (plus a half-cell-shifted copy made at start-up) are XORed.
Each 4 pixels: `pop ax / xor ax,[bp+99] / stosb / inc di / mov al,ah / stosb / inc di`, unrolled 940 times (20 per row x 47), with
`add sp,stride-40` / `add bp,stride` at row ends. Only attribute bytes are written (the `inc di` skips the char byte). The transition patches `xor ax,[bp+99]`
into `mov ax,0` in blocks. Any two images work, not just circles. (reenigne "More"; Trixter)

### 7f. Starfield / particle field (reenigne). RGB-viable: yes (mode-agnostic)
948 particles, each an unrolled block: `mov di,9999` (patched current position) / `stosb` (AL = 0 erases) / `shl di,1` / `mov di,[di]`
(next position from a 2-byte table covering DS:0-32767, one entry per VRAM byte 0-16383, i.e. a vector field) / `es: mov [di],ah` (plot; the colour register
varies per copy: AH, BL, BH, CL, CH, DL, DH = 7 colours) / `mov [patchAddress+1],di`. Particles are processed in random order, so no tearing and no
need for frame sync. One of 21 vector fields was shipped, because the rest compressed poorly. (reenigne "More"; Trixter)

### 7g. Polygons: pyramid, cube, donut (Scali). RGB-viable: technique yes (palette/gradients composite)
**Delta span renderer:** per scanline, a linked list of spans (x0, x1, colour). Inserting a span back to front splits, merges or removes existing spans, so
there is no overdraw. "Clear" = insert one background span per line. The span list is compared with the previous frame's and only the differences are
written, top to bottom. So cost depends on how much changes, not on polygon size; no back buffer is needed; and there is no even/odd interlace look.
Only add, subtract and compare, which suits the 8088. 25-polygon donut, per-polygon sort, flat shading on VileR's 7-blue / 6-red gradients. Dithering =
one extra bit of light: two nearest colours packed in the byte for even lines, swapped for odd lines (X pattern). Colour changes cost a full refill, so
shading and dithering reduce speed. On RGB in 320x200x4 Scali notes a palette that reads as translucent red/grey (Area 5150 note). (Scali polygons; Scali A5150)

### 7h. Rotozoomer (Trixter). RGB-viable: yes
Trixter's 1996 routine, heavily optimised, drawing every other line (an interlaced-look mode; unused lines set to a background colour). There is no further
detail in the write-ups. (Trixter; Scali final)

### 7i. Plasma (Trixter). Composite-only colours; the snow handling is the transferable part
80-col 1K-variant text mode, writing attribute bytes only (256 colours). Writes go only in horizontal/vertical blanking (polled): 60 fps without
snow avoidance, 20 fps with it. Throttled writes timed to put the snow in the border depend on the power-on PIT/CGA phase (section 3). (Trixter; Scali final)

### 7j. Drip (Trixter). RGB-viable: yes as a technique
Uses the 1K mode's per-2-line start-address reload to slide image rows down. The same idea works on any frame-stacked RGB mode. (Trixter)

### 7k. Kefrens bars + raster bars (reenigne). RGB-viable: yes (320x200x4; colours chosen for composite)
- **Line buffer, not frame buffer** (Atari 2600 style): the same VRAM line (2 scanlines high, because 1-line CRTC frames misbehave) is redisplayed every
  row, so what you write on one scanline persists below. It is cleared during vertical overscan/blank/sync.
- Exactly **304 cycles per scanline**, no polling. Per scanline: 2 VRAM reads, 4 writes, 7 nybble-wide pixels, one `out dx,al` raster colour, and
  `lodsb / out soundPort,al` for a per-scanline PWM sample (planned; it ended up playing MONOTONE once per frame with NOPs). Body shape:
  `mov ax,9999 / mov ds,ax / mov sp,[bx] / pop di / mov al,[es:di] / pop cx / and ax,cx / pop cx / or ax,cx / stosw / pop ax / and ah,[es:di+1] / pop cx / or ax,cx / stosw / pop ax / out dx,al / mov ds,bp / lodsb / out soundPort,al`,
  unrolled 200 times.
- Refresh = 19 (76 cycles) so refresh hits the same 4 places each scanline. Tables: a big one of 200 x 838 x 2 bytes (about 327 KB, positions per scanline per frame,
  stored sideways) and a small one of 12-byte entries, 154 positions x 16 raster colours, doubled for odd/even = 59,136 bytes. Needs a genuine 4.77 MHz machine
  plus an IBM CGA with MC6845, and no NIC IRQs. (reenigne "More"; Trixter; Scali A5150; MartyPC wait states)

### 7l. Portrait / "mugshots" (reenigne + VileR). Composite-only
80x100 1K mode per scanline. A letterboxed 80x200 tweaked text mode (only 100 unique lines fit). (Trixter; Scali A5150)

### 7m. End credits + 4-channel MOD player (reenigne). RGB-viable: yes (audio is display-independent; the scroller is ANSI text)
See section 8. The scroller is ANSI-style text, scrolled by hardware start-address changes issued from inside the mixer loop (v/h-instructions), with
characters printed by h-instructions. (reenigne MOD player; Trixter; Scali final)

---

## 8. Sound

### 8a. Background music during effects (Trixter/MONOTONE). RGB-viable: yes
One PC-speaker square-wave tone (PIT channel 2), changed or silenced **once per frame (60 Hz)** from the timer/vretrace ISR. Arpeggios fake chords and
duty-cycle changes give limited volume. The player is 18 lines of asm but costs about 2 scanlines of CPU, which is why nothing richer ran during
full-frame 60 Hz effects. Area 5150 moved to a 120 Hz engine, which means placing two speaker updates evenly inside cycle-counted frame code.
(Trixter; reenigne MOD player; Scali A5150)

### 8b. 4-channel MOD player, 16.5 kHz PWM (reenigne). RGB-viable: yes
- **PWM:** PIT channel 2 drives the speaker through an AND gate, in one-shot mode. Each `out 42h,al` sets the output high and starts a count, and the output
  drops when the count expires, so the pulse width = sample. Cycle-counted with interrupts off: no PIT0 reload IRQ, no jitter.
- **Rate:** 288 CPU cycles per sample = 72 PIT ticks -> 4.77 MHz / 288 = **16,572 Hz**, so the carrier whine is above hearing. Per-channel counts are 1-18,
  summed 4-72. **Never write 0** (it means 65536, about 55 ms of silence), and avoid overflow or negatives. The final version peak-normalises the song offline for loudness.
- **Mixer per channel:** `add bp,9999 / mov bx,bp / mov bl,99 / mov al,[bx]` (first), then `add si,9999 / mov bx,si / mov bl,99 / add al,[bx]`, ... then `out 42h,al`.
  BP, SI, DI, DX are 8.8 phase accumulators (the 9999 frequency is patched). BL (the 99 waveform number, patched) indexes a sideways 256x256 table. 256-byte
  single-cycle looped samples give 0-8.3 kHz at 0.25 Hz steps (SID-like; codename "SID"). Volume is baked into sample copies, with no volume tables. Ring mod is
  possible by patching `mov bl,99` to `mov bl,al`.
- **Song update:** a `loop` counter (CL patched). On expiry, patches are popped off the stack (`pop bx / pop word [cs:bx]`) or v-instructions run in the 15-NOP
  slot. 50 Hz updates, worst case 10 patches per frame (40 bytes/frame, 2000 B/s). The song stream is 800 B/s: 16-byte paragraphs = 12 bytes of music + 4 bytes of
  h-instruction (print char, set cursor, set CRTC start, finish). 331 samples per 20 ms. The longest v-instruction uses 119 of the 331 samples. About 87% CPU in total.
- **Offline converter** (mod_convert, based on PT2PLAY) resamples loops to 256 bytes and turns notes and effects into frequencies and waveform numbers. Everything except
  9xx, E0x, E9x and CIA tempo is supported. It also renders a WAV by simulating the 1-bit 1.193 MHz speaker output. Source: github.com/reenigne/reenigne
  (8088/demo/credits/mod.asm), github.com/crtc-demos/mod-convert.
- Area 5150's lake plays **one PWM sample per scanline = 15.7 kHz** alongside per-scanline CRTC work, which is the design the Kefrens loop was meant to have. (Scali A5150)

(Speaker gate/enable via port 61h bits 0-1 is standard PC knowledge *(not from the write-ups)*.)

**Sources:** https://www.reenigne.org/blog/8088-pc-speaker-mod-player-how-its-done/ , https://trixter.oldskool.org/2015/04/07/8088-mph/ ,
https://scalibq.wordpress.com/2015/08/02/8088-mph-the-final-version/ , https://scalibq.wordpress.com/2022/09/16/area-5150-a-reflection/

---

## 9. Area 5150 (2022) RGB-specific lessons (from Scali's reflection + MartyPC)

Area 5150 targets RGBI. Scali says its 80-col "ANSI from Hell" 640x200x16 looks "VGA/Amiga-like" because of RGBI's bright colours. The recipe:
(1) mostly 80-col text, with only a few small VRAM updates per frame placed in blanking (snow); (2) everything else by choosing which stored scanline shows
on each screen line (frame stacking / per-scanline start address), which costs no VRAM writes. The effects built this way: overscan gradients from reused lines,
twister, wavy transitions, checkerboard (60 fps full width), sine scroller (small active area to free CPU), 8-way scroller with 1-line steps, parallax, lake reflection;
40-col for chunky full-screen effects (voxel landscape); real text/graphics split screens. Glenz polygons use an EOR filler: draw outlines into a RAM buffer, then
XOR-fill each line with the line above it, writing the filled result straight to VRAM (no read-modify-write on slow CGA).

**Sources:** https://scalibq.wordpress.com/2022/09/16/area-5150-a-reflection/ , https://martypc.blogspot.com/2025/05/emulator-debugging-area-5150s-lake.html

---

## Sources read

1. Trixter, "8088 MPH: We Break All Your Emulators" (incl. comments): https://trixter.oldskool.org/2015/04/07/8088-mph/
2. reenigne, "8088 PC Speaker MOD player: How it's done": https://www.reenigne.org/blog/8088-pc-speaker-mod-player-how-its-done/ (via WebFetch)
3. reenigne, "More 8088 MPH how it's done": https://www.reenigne.org/blog/more-8088-mph-how-its-done/ (via WebFetch, two passes)
4. reenigne, "1K colours on CGA: How it's done": https://www.reenigne.org/blog/1k-colours-on-cga-how-its-done/ (via WebFetch)
5. reenigne, "The CGA wait states": https://www.reenigne.org/blog/the-cga-wait-states/
6. reenigne, "CGA: Why the 80-column text mode requires the border color to be set": https://www.reenigne.org/blog/cga-why-the-80-column-text-mode-requires-the-border-color-to-be-set/
7. reenigne, "CGA: Reading the current beam position with the lightpen latch": https://www.reenigne.org/blog/cga-reading-the-current-beam-position-with-the-lightpen-latch/
8. reenigne, "Adventures in CGA lockstep": https://www.reenigne.org/blog/adventures-in-cga-lockstep/
9. reenigne, "Adventures in CRTC lockstep": https://www.reenigne.org/blog/adventures-in-crtc-lockstep/
10. reenigne, "How to get away with disabling DRAM refresh": https://www.reenigne.org/blog/how-to-get-away-with-disabling-dram-refresh/
11. reenigne, "8088 MPH final version announcement": https://www.reenigne.org/blog/8088-mph-final-version-announcement/ (announcement only, no technical detail)
12. Scali, "8088 MPH: How it came about": https://scalibq.wordpress.com/2015/04/12/8088-mph-how-it-came-about/
13. Scali, "8088 MPH: Sprites? Where we're going, we don't need... sprites!": https://scalibq.wordpress.com/2015/04/19/8088-mph-sprites-where-were-going-we-dont-need-sprites/
14. Scali, "8088 MPH: The polygons": https://scalibq.wordpress.com/2015/04/23/8088-mph-the-polygons/
15. Scali, "8088 MPH: The final version": https://scalibq.wordpress.com/2015/08/02/8088-mph-the-final-version/
16. Scali, "Latch onto this, it's all relative": https://scalibq.wordpress.com/2015/09/13/latch-onto-this-its-all-relative/
17. Scali, "Area 5150, a reflection": https://scalibq.wordpress.com/2022/09/16/area-5150-a-reflection/
18. VileR, "CGA in 1024 Colors - a New Mode: the Illustrated Guide": https://int10h.org/blog/2015/04/cga-in-1024-colors-new-mode-illustrated/
19. VileR, "Old Chips, New Glitches: the CGA/CRTC 'Phantom' VSync": https://int10h.org/blog/2023/03/cga-6845-crtc-phantom-vsync-glitch/ (via WebFetch)
20. MartyPC, "The 8088MPH CPU Test": https://martypc.blogspot.com/2023/08/the-8088mph-cpu-test.html
21. MartyPC, "Exploring CGA Wait States": https://martypc.blogspot.com/2023/05/exploring-cga-wait-states.html
22. MartyPC, "Emulator Debugging: Area 5150's Lake Effect": https://martypc.blogspot.com/2025/05/emulator-debugging-area-5150s-lake.html

Note: reenigne.org pages were read through WebFetch's summariser (direct download was refused by the site's mod_security),
so their figures and snippets are as that tool reported them. The code lines quoted from those pages came from requests for verbatim excerpts.

## Pages not reached / not found

- Direct (raw HTML) download of reenigne.org: HTTP 406 from mod_security. Read via WebFetch instead.
- Trixter's loader design doc and compression spreadsheet (Google Docs links in Trixter's post): not fetched (Google Docs, likely needs a browser).
- No separate write-up exists from Trixter on the rotozoomer, the loader internals or the plasma beyond his overview, and none from reenigne on the 1K "drip"/credits scroller beyond the MOD-player post.
- No reenigne "Area 5150: how it's done" post was found (a site search for "area 5150" on reenigne.org returned only the DRAM-refresh post).
  Scali's reflection, MartyPC's Lake post and int10h's phantom-vsync post were used instead.
- VileR's final-version graphics post (int10h.org) was not read: it covers composite recolouring for new-style CGA.

---

## Our own analysis of the release (external/8088MPH)

- **86Box plays the whole demo correctly** (86Box 6.0, `ibmxt86`, 8088 at 4.77 MHz, CGA). The calibration screen needs Enter: we
  put it in the BIOS keyboard buffer first with a 24-byte .COM (`mov ax,40h / mov ds,ax / mov word [1Eh],1C0Dh /
  mov word [1Ah],1Eh / mov word [1Ch],20h / ret`). For the demo's real colours, the VM config needs `[IBM CGA] display_type = 1`
  (composite); leave it out for RGB, which is our monitor.
- **Unpacking:** the loader and every module except `.006` (Microsoft C, uncompressed) are PKLITE 1.12, "small" mode, some with
  "extra" mode. depklite only handles "large" mode. deark handles every variant and rebuilds the MZ header:
  `brew install deark; deark -m pklite -od OUT -o NAME 8088MPH.00X`. Disassemble with `ndisasm -b16` (strip the MZ header first; `-o 0x100` for .COM files).
- **Module map** (the loader runs `.001` to `.00D` in order; confirmed = the module's own strings say so, the rest are guesses from the order and from the ports and
  instructions in each module). `N088MPH.*` are the new-style-CGA versions of the composite-coloured parts.

| Module | Part | Basis |
|---|---|---|
| `8088MPH.EXE` | loader + API + background music; `NOCHECK`, `DEBUGSHELL` switches; cycle-count check | confirmed |
| `.001` | CGA-card picture + calibration | text-mode attribute data |
| `.002` | "Ho, what's this?" intro | confirmed |
| `.003` | moiré? | small .COM |
| `.004` | rotozoomer? | 64 XLATs, very many OUTs |
| `.005`/`.009`/`.00C` | composite-coloured parts with N- versions | guess |
| `.007` | picture or starfield (2176 MOVSWs) | guess |
| `.008` | starfield? (sine tables, 84 XLATs) | guess |
| `.00A` | sprite part? (415 KB unpacked) | guess |
| `.00B` | vectorballs/polygons? (about 2,900 POPs) | guess |
| `.00D` | end credits + 4-channel MOD player | confirmed |
