/* Claude-XT screen: see screen.h. */

#include <conio.h>
#include <i86.h>
#include <string.h>
#include "screen.h"

#define VRAM_SEG     0xB800
#define VRAM_WORDS   0x2000          /* CGA: 16 KB, wraps at 8K cells */
#define WRAP(x)      ((x) & (VRAM_WORDS - 1))
#define HIST_MAX     (VRAM_WORDS / SCR_COLS - SCR_ROWS)   /* 77 rows */
#define CRTC_INDEX   0x3D4
#define CRTC_STATUS  0x3DA

static unsigned top;        /* VRAM cell offset of live row 0 */
static int history;         /* rows of valid scrollback above `top` */
static int view;            /* rows currently scrolled back */
static int hwscroll;        /* hardware scrolling available (real CGA) */
static int snowcheck;       /* wait for retrace before each VRAM write */
static unsigned cur_pos;    /* cursor, as a cell offset from `top` */
static int foot;            /* rows pinned at the bottom */

/* Copy of the bottom SCR_FOOT_MAX rows, so the footer can be redrawn without
   reading VRAM (slow, and snowy on CGA). */
#define MIRROR_TOP   (SCR_ROWS - SCR_FOOT_MAX)
static unsigned short mirror[SCR_FOOT_MAX][SCR_COLS];

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
    hwscroll = (r.h.bl == 0x10);    /* plain CGA: 16 KB wrapping VRAM */
    snowcheck = hwscroll;

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

    r.x.ax = 0x0003;
    int86(0x10, &r, &r);
}

void scr_set_snowcheck(int on) { snowcheck = on; }
int  scr_has_hwscroll(void)    { return hwscroll; }

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
    mirror_fill(row, col, cell, 1);
    vfill(cell_off(row, col), cell, 1);
}

void scr_putn(int row, int col, const unsigned short *cells, int n)
{
    if (row >= MIRROR_TOP)
        memcpy(&mirror[row - MIRROR_TOP][col], cells, n * sizeof(cells[0]));
    vcopy(cell_off(row, col), cells, n);
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
    mirror_fill(row, col, cell, n);
    vfill(cell_off(row, col), cell, n);
}

void scr_clear(unsigned char attr)
{
    int r;
    for (r = MIRROR_TOP; r < SCR_ROWS; r++)
        mirror_fill(r, 0, ' ' | (attr << 8), SCR_COLS);
    vfill(top, ' ' | (attr << 8), SCR_ROWS * SCR_COLS);
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

void scr_scroll(unsigned char attr)
{
    int bottom = SCR_ROWS - foot;   /* first footer row */
    unsigned blank = ' ' | (attr << 8);
    int r, c;

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
        unsigned off = cell_off(bottom, 0);     /* becomes row bottom-1 */
        unsigned n = (foot + 1) * SCR_COLS;

        for (c = 0; c < SCR_COLS; c++)
            rows[c] = blank;
        if (foot)
            memcpy(rows + SCR_COLS, mirror[bottom - MIRROR_TOP], foot * sizeof(mirror[0]));
        top = WRAP(top + SCR_COLS);
        if (history < HIST_MAX)
            history++;
        if (view && view < history)
            view++;                 /* keep the scrolled-back text still */
        _disable();
        wait_vblank();
        set_start_now();
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
        view = lines;
        set_start();
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
