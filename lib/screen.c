/* ClawDOS screen: see screen.h. */

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"

#define VRAM_SEG     0xB800
#define VRAM_WORDS   0x2000          /* CGA: 16 KB, 8K cells */
#define WRAP(x)      ((x) & (VRAM_WORDS - 1))
#define VRAM_ROWS    (VRAM_WORDS / SCR_COLS)              /* 102 whole rows */
#define HIST_MAX     (VRAM_ROWS - SCR_ROWS)               /* 77 rows */
#define KEEP_ROWS    25              /* scrollback kept when the window moves back */
#define MOVE_ROWS    4               /* rows copied per vertical blank meanwhile */
#define CRTC_INDEX   0x3D4
#define CRTC_STATUS  0x3DA

static unsigned top;        /* VRAM cell offset of live row 0 */
static int history;         /* rows of valid scrollback above `top` */
static int view;            /* rows currently scrolled back */
static int cga;             /* a CGA, not an EGA/VGA: snow, mode register 3D8h */
static int hwscroll;        /* scrolling by CRTC start address (CGA, EGA and VGA) */
static int snowcheck;       /* wait for retrace before each VRAM write */
static unsigned cur_pos;    /* cursor, as a cell offset from `top` */
static int foot;            /* rows pinned at the bottom */

/* Copy of the bottom SCR_FOOT_MAX rows, so the footer can be redrawn without
   reading VRAM (slow, and snowy on CGA). */
#define MIRROR_TOP   (SCR_ROWS - SCR_FOOT_MAX)
static unsigned short mirror[SCR_FOOT_MAX][SCR_COLS];

/* Set while a screen call is changing VRAM, `top` or the mirror, and while the
   screen is handed to something else: the ticker's interrupt stays off them. */
static volatile int busy;
static volatile int suspended;
#define BUSY_BEGIN() (busy++)
#define BUSY_END()   (busy--)

/* Fill n cells starting at cell offset `off` (wrapping) with `cell`. */
static void vfill(unsigned off, unsigned cell, unsigned n)
{
    int sync = snowcheck;

    if (n == 0)
        return;
    _asm {
        push    es
        push    di
        mov     ax, VRAM_SEG
        mov     es, ax
        mov     di, off
        shl     di, 1
        mov     bx, cell
        mov     cx, n
        mov     dx, CRTC_STATUS
        cmp     sync, 0
        je      fast
    slow:
        and     di, 3FFFh
    w1: in      al, dx              ; wait until not in horizontal retrace...
        test    al, 1
        jnz     w1
        cli
    w2: in      al, dx              ; ...then for the start of the next one
        test    al, 1
        jz      w2
        mov     es:[di], bx
        sti
        add     di, 2
        loop    slow
        jmp     done
    fast:
        and     di, 3FFFh
        mov     es:[di], bx
        add     di, 2
        loop    fast
    done:
        pop     di
        pop     es
    }
}

/* Copy n cells from src to cell offset `off` (wrapping). */
static void vcopy(unsigned off, const unsigned short __far *src, unsigned n)
{
    int sync = snowcheck;

    if (n == 0)
        return;
    _asm {
        push    ds
        push    es
        push    si
        push    di
        mov     ax, VRAM_SEG
        mov     es, ax
        mov     di, off
        shl     di, 1
        mov     cx, n
        mov     dx, CRTC_STATUS
        mov     ax, sync
        lds     si, src
        or      ax, ax
        je      fast
    slow:
        and     di, 3FFFh
        mov     bx, [si]
        add     si, 2
    w1: in      al, dx
        test    al, 1
        jnz     w1
        cli
    w2: in      al, dx
        test    al, 1
        jz      w2
        mov     es:[di], bx
        sti
        add     di, 2
        loop    slow
        jmp     done
    fast:
        and     di, 3FFFh
        mov     bx, [si]
        add     si, 2
        mov     es:[di], bx
        add     di, 2
        loop    fast
    done:
        pop     di
        pop     si
        pop     es
        pop     ds
    }
}

/* Copy n cells to cell offset `off` (wrapping) at full speed: only for use
   during vertical blanking. */
static void vblast(unsigned off, const unsigned short *src, unsigned n)
{
    unsigned first = VRAM_WORDS - off;
    unsigned short __far *vram = (unsigned short __far *)MK_FP(VRAM_SEG, 0);

    if (first > n)
        first = n;
    _fmemcpy(vram + off, src, first * 2);
    if (n > first)
        _fmemcpy(vram, src + first, (n - first) * 2);
}

/* Wait for the start of vertical blanking: the display-enable bit staying
   off for longer than a horizontal blank. That leaves ~3.9 ms of snow-free
   access. Gives up after a while on hardware that doesn't behave like a CGA. */
static void wait_vblank(void)
{
    _asm {
        mov     dx, CRTC_STATUS
        mov     bx, 0FFFFh          ; overall timeout
    disp:
        dec     bx
        jz      done
        in      al, dx              ; wait for active display...
        test    al, 1
        jnz     disp
    blank:
        dec     bx
        jz      done
        in      al, dx              ; ...then for blanking
        test    al, 1
        jz      blank
        mov     cx, 8               ; still blank 8 polls later? vertical
    hold:
        in      al, dx
        test    al, 1
        jz      disp                ; no, just a horizontal blank
        loop    hold
    done:
    }
}

static void crtc_write(unsigned char reg, unsigned char val)
{
    outp(CRTC_INDEX, reg);
    outp(CRTC_INDEX + 1, val);
}

static void wait_vretrace(void)
{
    while (inp(CRTC_STATUS) & 8)
        ;
    while (!(inp(CRTC_STATUS) & 8))
        ;
}

static unsigned view_start(void)
{
    return WRAP(top - (unsigned)view * SCR_COLS);
}

static void set_start_now(void)
{
    unsigned s = view_start();
    unsigned c = s + WRAP(top + cur_pos - s);   /* unwrapped MA for the cursor */

    crtc_write(12, s >> 8);
    crtc_write(13, s & 0xFF);
    crtc_write(14, c >> 8);
    crtc_write(15, c & 0xFF);
}

/* Point the CRTC at the new start address and return, with interrupts off,
   in the blanking before the first frame that shows it: the rows that moved
   can be rewritten then, unseen. A CGA's 6845 loads the start address as each
   frame begins, so writing it in vertical blanking is enough. EGA and VGA load
   it as vertical retrace begins: write it well before one, then wait for it. */
static void start_in_blank(void)
{
    if (cga) {
        _disable();
        wait_vblank();
        set_start_now();
    } else {
        wait_vretrace();
        while (inp(CRTC_STATUS) & 8)    /* past this retrace: the next is a frame away */
            ;
        _disable();
        set_start_now();
        while (!(inp(CRTC_STATUS) & 8))
            ;
    }
}

static void set_start(void)
{
    wait_vretrace();
    set_start_now();
}

static unsigned cell_off(int row, int col)
{
    return WRAP(top + row * SCR_COLS + col);
}

void scr_init(void)
{
    union REGS r;

    r.x.ax = 0x0003;                /* 80x25 color text; also resets the CRTC */
    int86(0x10, &r, &r);

    r.h.ah = 0x12;                  /* EGA/VGA present? (BL changes if so) */
    r.h.bl = 0x10;
    int86(0x10, &r, &r);
    cga = (r.h.bl == 0x10);
    snowcheck = cga;
    /* EGA and VGA have the same start address and cursor registers, in the
       same units, and at least 32 KB at B800h, so the 16 KB window works on
       them too. The copying path below stays for anything that can't. */
    hwscroll = 1;

    suspended = 0;
    top = 0;
    history = 0;
    view = 0;
    cur_pos = 0;
    foot = 0;
    scr_clear(SCR_ATTR(C_LIGHTGRAY, C_BLACK));
}

void scr_done(void)
{
    union REGS r;

    scr_ticker_off();
    r.x.ax = 0x0003;
    int86(0x10, &r, &r);
}

int  scr_is_cga(void)          { return cga; }

static void mirror_fill(int row, int col, unsigned cell, int n)
{
    if (row >= MIRROR_TOP) {
        unsigned short *p = &mirror[row - MIRROR_TOP][col];
        while (n-- > 0)
            *p++ = cell;
    }
}

void scr_putc(int row, int col, unsigned char ch, unsigned char attr)
{
    unsigned cell = ch | (attr << 8);
    BUSY_BEGIN();
    mirror_fill(row, col, cell, 1);
    vfill(cell_off(row, col), cell, 1);
    BUSY_END();
}

void scr_putn(int row, int col, const unsigned short *cells, int n)
{
    BUSY_BEGIN();
    if (row >= MIRROR_TOP)
        memcpy(&mirror[row - MIRROR_TOP][col], cells, n * sizeof(cells[0]));
    vcopy(cell_off(row, col), cells, n);
    BUSY_END();
}

void scr_puts(int row, int col, const char *s, unsigned char attr)
{
    unsigned short cells[SCR_COLS];
    int n = 0;

    while (*s && col + n < SCR_COLS)
        cells[n++] = (unsigned char)*s++ | (attr << 8);
    scr_putn(row, col, cells, n);
}

void scr_fill(int row, int col, int n, unsigned char ch, unsigned char attr)
{
    unsigned cell = ch | (attr << 8);
    BUSY_BEGIN();
    mirror_fill(row, col, cell, n);
    vfill(cell_off(row, col), cell, n);
    BUSY_END();
}

void scr_getn(int row, int col, unsigned short *cells, int n)
{
    const unsigned short __far *v = (const unsigned short __far *)MK_FP(VRAM_SEG, 0);
    unsigned off = cell_off(row, col);

    while (n-- > 0) {
        *cells++ = v[off];
        off = WRAP(off + 1);
    }
}

void scr_clear(unsigned char attr)
{
    int r;
    BUSY_BEGIN();
    for (r = MIRROR_TOP; r < SCR_ROWS; r++)
        mirror_fill(r, 0, ' ' | (attr << 8), SCR_COLS);
    vfill(top, ' ' | (attr << 8), SCR_ROWS * SCR_COLS);
    BUSY_END();
}

void scr_footer(int rows, unsigned char attr)
{
    int r;

    if (rows < 0)
        rows = 0;
    if (rows > SCR_FOOT_MAX)
        rows = SCR_FOOT_MAX;
    for (r = SCR_ROWS - foot; r < SCR_ROWS - rows; r++)
        scr_fill(r, 0, SCR_COLS, ' ', attr);
    foot = rows;
}

int scr_footer_rows(void) { return foot; }

/* The IBM CGA doesn't wrap its display at the end of video memory: the CRTC
   goes on counting into addresses with no RAM behind them, which show as
   garbage (86Box's CGA does wrap, so it can't show this). So the window must
   never run past the last whole row. Before it would, move the live screen
   and some scrollback back to the start of VRAM, a few rows per vertical
   blank (no snow, and it isn't on screen yet), then point the CRTC at it. */
static void rebase(void)
{
    unsigned short __far *vram = (unsigned short __far *)MK_FP(VRAM_SEG, 0);
    int keep = history < KEEP_ROWS ? history : KEEP_ROWS;
    unsigned from = top - keep * SCR_COLS;              /* past the copy's end */
    unsigned n = (keep + SCR_ROWS) * SCR_COLS, done, k;

    for (done = 0; done < n; done += k) {
        k = n - done < MOVE_ROWS * SCR_COLS ? n - done : MOVE_ROWS * SCR_COLS;
        _disable();
        if (cga)                    /* no snow to avoid on EGA/VGA */
            wait_vblank();
        _fmemcpy(vram + done, vram + from + done, k * 2);
        _enable();
    }
    top = keep * SCR_COLS;
    history = keep;
    if (view > history)
        view = history;
    start_in_blank();
    _enable();
}

void scr_scroll(unsigned char attr)
{
    int bottom = SCR_ROWS - foot;   /* first footer row */
    unsigned blank = ' ' | (attr << 8);
    int r, c;

    BUSY_BEGIN();
    /* The mirror follows the scrolling rows; the footer's part stays put. */
    for (r = 0; r < bottom - 1 - MIRROR_TOP; r++)
        memcpy(mirror[r], mirror[r + 1], sizeof(mirror[0]));
    if (bottom - 1 >= MIRROR_TOP)
        mirror_fill(bottom - 1, 0, blank, SCR_COLS);

    if (hwscroll) {
        /* Moving the window down a row moves everything up, footer included.
           So the new blank row and the footer are rewritten one row further
           on, during vertical blanking along with the new start address. */
        static unsigned short rows[(SCR_FOOT_MAX + 1) * SCR_COLS];
        unsigned off, n = (foot + 1) * SCR_COLS;

        if (top + (SCR_ROWS + 1) * SCR_COLS > VRAM_ROWS * SCR_COLS)
            rebase();
        off = cell_off(bottom, 0);              /* becomes row bottom-1 */

        for (c = 0; c < SCR_COLS; c++)
            rows[c] = blank;
        if (foot)
            memcpy(rows + SCR_COLS, mirror[bottom - MIRROR_TOP], foot * sizeof(mirror[0]));
        top += SCR_COLS;
        if (history < HIST_MAX)
            history++;
        if (view && view < history)
            view++;                 /* keep the scrolled-back text still */
        start_in_blank();
        vblast(off, rows, n);
        _enable();
    } else {
        static unsigned short row[SCR_COLS];
        unsigned short __far *vram = (unsigned short __far *)MK_FP(VRAM_SEG, 0);

        for (r = 1; r < bottom; r++) {
            _fmemcpy(row, vram + r * SCR_COLS, sizeof(row));
            vcopy((r - 1) * SCR_COLS, row, SCR_COLS);
        }
        vfill((bottom - 1) * SCR_COLS, blank, SCR_COLS);
    }
    BUSY_END();
}

int scr_view(int lines)
{
    if (!hwscroll)
        return 0;
    if (lines < 0)
        lines = 0;
    if (lines > history)
        lines = history;
    if (lines != view) {
        BUSY_BEGIN();
        view = lines;
        set_start();
        BUSY_END();
    }
    return view;
}

int scr_viewing(void) { return view; }

void scr_cursor(int row, int col)
{
    cur_pos = row * SCR_COLS + col;
    if (hwscroll) {
        unsigned s = view_start();
        unsigned c = s + WRAP(top + cur_pos - s);
        crtc_write(14, c >> 8);
        crtc_write(15, c & 0xFF);
    } else {
        crtc_write(14, cur_pos >> 8);
        crtc_write(15, cur_pos & 0xFF);
    }
}

void scr_cursor_visible(int on)
{
    crtc_write(10, on ? 6 : 0x20);  /* start line 6, or bit 5 = cursor off */
    crtc_write(11, 7);
}

#define CGA_MODE_REG  0x3D8
#define CGA_TEXT80    0x29          /* 80x25 colour text, video on, blink */

void scr_suspend(unsigned short __far *save)
{
    union REGS r;

    suspended = 1;                  /* the ticker keeps off until scr_resume */
    _fmemcpy(save, MK_FP(VRAM_SEG, 0), SCR_SAVE_BYTES);
    r.x.ax = 0x0003;
    int86(0x10, &r, &r);
}

void scr_state_get(ScrState *s)
{
    s->top = top;
    s->history = history;
    s->cur_pos = cur_pos;
}

void scr_state_set(const ScrState *s)
{
    BUSY_BEGIN();
    top = s->top;
    history = s->history;
    cur_pos = s->cur_pos;
    view = 0;
    BUSY_END();
}

void scr_resume(const unsigned short __far *save)
{
    union REGS r;

    r.x.ax = 0x0003;
    int86(0x10, &r, &r);
    if (cga)                        /* blank the display: no snow while copying */
        outp(CGA_MODE_REG, CGA_TEXT80 & ~8);
    else if (hwscroll)              /* EGA/VGA: show the window, not row 0, as it fills */
        set_start_now();
    _fmemcpy(MK_FP(VRAM_SEG, 0), save, SCR_SAVE_BYTES);
    if (cga) {
        set_start_now();
        outp(CGA_MODE_REG, CGA_TEXT80);
    } else if (!hwscroll) {
        scr_cursor(cur_pos / SCR_COLS, cur_pos % SCR_COLS);
    }
    suspended = 0;
}

/* --- the ticker (see screen.h) --------------------------------------------- */

#define TK_TAIL_MAX  64

static volatile int tk_on;          /* drawing; the hook can stay in while off */
static volatile int tk_in;          /* the interrupt is running (no re-entry) */
static int tk_row, tk_glyph_col, tk_secs_col;
static unsigned char tk_glyph_attr, tk_secs_attr;
static unsigned char tk_glyphs[8];
static unsigned short tk_tail[TK_TAIL_MAX];
static int tk_tail_len;
static volatile unsigned tk_last;   /* low word of the BIOS tick count last seen */
static volatile unsigned tk_secs;   /* seconds shown */
static volatile unsigned tk_frac;   /* tenths of ticks towards the next second: 182 make one */
static volatile int tk_digits;      /* digits of tk_secs on screen */
static unsigned short tk_buf[6 + TK_TAIL_MAX];
static void (__interrupt __far *tk_old)();
static int tk_hooked, tk_atexit;

/* Runs on whatever stack the timer interrupted. Like lib/shell.c's hook it
   calls none of our functions (code built for SS == DGROUP on a foreign stack):
   the VRAM writes are inlined, with the same retrace wait as vcopy. */
static void __interrupt __far tk_isr(void)
{
    if (tk_on && !busy && !suspended && !tk_in) {
        unsigned now = *(volatile unsigned __far *)MK_FP(0x40, 0x6C);
        unsigned d = now - tk_last;
        tk_in = 1;
        if (d != 0) {
            unsigned secs = tk_secs, frac = tk_frac, off, n = 1, col = tk_glyph_col;
            unsigned short __far *src = (unsigned short __far *)tk_buf;
            int sync = snowcheck, pass;

            if (d > 1000)           /* the midnight reset: count it as one tick */
                d = 1;
            tk_last = now;
            frac += d * 10;
            while (frac >= 182) {
                frac -= 182;
                secs++;
            }
            tk_frac = frac;
            tk_buf[0] = tk_glyphs[(now >> 1) & 7] | (tk_glyph_attr << 8);
            for (pass = 0; pass < 2; pass++) {
                if (pass == 1) {    /* then the seconds, if they moved on */
                    unsigned v = secs;
                    int k, nd = 0;
                    char dig[5];
                    if (secs == tk_secs)
                        break;
                    tk_secs = secs;
                    do {
                        dig[nd++] = (char)('0' + v % 10);
                        v /= 10;
                    } while (v && nd < 5);
                    for (k = 0; k < nd; k++)
                        tk_buf[k] = (unsigned char)dig[nd - 1 - k] | (tk_secs_attr << 8);
                    n = nd;
                    if (nd != tk_digits) {   /* one digit longer: move the rest along */
                        for (k = 0; k < tk_tail_len && n + k < 6 + TK_TAIL_MAX && tk_secs_col + n + k < SCR_COLS; k++)
                            tk_buf[n + k] = tk_tail[k];
                        n += k;
                        tk_digits = nd;
                    }
                    col = tk_secs_col;
                }
                if (tk_row >= MIRROR_TOP) {
                    unsigned k;
                    for (k = 0; k < n; k++)
                        mirror[tk_row - MIRROR_TOP][col + k] = tk_buf[k];
                }
                off = WRAP(top + tk_row * SCR_COLS + col);
                _asm {
                    push    ds
                    push    es
                    push    si
                    push    di
                    mov     ax, VRAM_SEG
                    mov     es, ax
                    mov     di, off
                    shl     di, 1
                    mov     cx, n
                    mov     dx, CRTC_STATUS
                    mov     ax, sync
                    lds     si, src
                    or      ax, ax
                    je      fast
                slow:
                    and     di, 3FFFh
                    mov     bx, [si]
                    add     si, 2
                w1: in      al, dx
                    test    al, 1
                    jnz     w1
                w2: in      al, dx
                    test    al, 1
                    jz      w2
                    mov     es:[di], bx
                    add     di, 2
                    loop    slow
                    jmp     done
                fast:
                    and     di, 3FFFh
                    mov     bx, [si]
                    add     si, 2
                    mov     es:[di], bx
                    add     di, 2
                    loop    fast
                done:
                    pop     di
                    pop     si
                    pop     es
                    pop     ds
                }
            }
        }
        tk_in = 0;
    }
    _chain_intr(tk_old);
}

/* mTCP hooks INT 1Ch too, before us, and puts back what it found when the
   stack stops: so unhook first (the harness does), and never put tk_old back
   once the vector is no longer ours, or it would point into a freed program. */
static void tk_unhook(void)
{
    if (tk_hooked) {
        tk_on = 0;
        if (_dos_getvect(0x1C) == tk_isr)
            _dos_setvect(0x1C, tk_old);
        tk_hooked = 0;
    }
}

void scr_ticker(int row, int glyphCol, const unsigned char *glyphs, unsigned char glyphAttr,
                int secsCol, unsigned char secsAttr, unsigned long elapsed, unsigned long at,
                const unsigned short *tail, int tailLen)
{
    unsigned long tenths = elapsed * 10;
    unsigned secs = (unsigned)(tenths / 182);
    int nd = 1, i;

    tk_on = 0;                      /* the interrupt leaves it alone while we set it up */
    tk_row = row;
    tk_glyph_col = glyphCol;
    tk_glyph_attr = glyphAttr;
    for (i = 0; i < 8; i++)
        tk_glyphs[i] = glyphs[i];
    tk_secs_col = secsCol;
    tk_secs_attr = secsAttr;
    if (tailLen > TK_TAIL_MAX)
        tailLen = TK_TAIL_MAX;
    for (i = 0; i < tailLen; i++)
        tk_tail[i] = tail[i];
    tk_tail_len = tailLen;
    tk_secs = secs;
    tk_frac = (unsigned)(tenths % 182);
    for (i = secs; i >= 10; i /= 10)
        nd++;
    tk_digits = nd;
    tk_last = (unsigned)at;
    if (!tk_hooked) {
        if (!tk_atexit) {           /* a hook left behind would point into freed memory */
            atexit(tk_unhook);
            tk_atexit = 1;
        }
        tk_old = _dos_getvect(0x1C);
        _dos_setvect(0x1C, tk_isr);
        tk_hooked = 1;
    }
    tk_on = 1;
}

void scr_ticker_off(void)
{
    tk_on = 0;
    tk_unhook();
}

int scr_ticker_on(void) { return tk_on; }
