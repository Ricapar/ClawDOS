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

static void set_start(void)
{
    unsigned s = view_start();
    unsigned c = s + WRAP(top + cur_pos - s);   /* unwrapped MA for the cursor */

    wait_vretrace();
    crtc_write(12, s >> 8);
    crtc_write(13, s & 0xFF);
    crtc_write(14, c >> 8);
    crtc_write(15, c & 0xFF);
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

void scr_putc(int row, int col, unsigned char ch, unsigned char attr)
{
    vfill(cell_off(row, col), ch | (attr << 8), 1);
}

void scr_putn(int row, int col, const unsigned short *cells, int n)
{
    vcopy(cell_off(row, col), cells, n);
}

void scr_puts(int row, int col, const char *s, unsigned char attr)
{
    unsigned short cells[SCR_COLS];
    int n = 0;

    while (*s && col + n < SCR_COLS)
        cells[n++] = (unsigned char)*s++ | (attr << 8);
    vcopy(cell_off(row, col), cells, n);
}

void scr_fill(int row, int col, int n, unsigned char ch, unsigned char attr)
{
    vfill(cell_off(row, col), ch | (attr << 8), n);
}

void scr_clear(unsigned char attr)
{
    vfill(top, ' ' | (attr << 8), SCR_ROWS * SCR_COLS);
}

void scr_scroll(unsigned char attr)
{
    if (hwscroll) {
        /* Clear the row that is about to appear, then move the window. */
        vfill(cell_off(SCR_ROWS, 0), ' ' | (attr << 8), SCR_COLS);
        top = WRAP(top + SCR_COLS);
        if (history < HIST_MAX)
            history++;
        if (view && view < history)
            view++;                 /* keep the scrolled-back text still */
        set_start();
    } else {
        static unsigned short row[SCR_COLS];
        unsigned short __far *vram = (unsigned short __far *)MK_FP(VRAM_SEG, 0);
        int r;

        for (r = 1; r < SCR_ROWS; r++) {
            _fmemcpy(row, vram + r * SCR_COLS, sizeof(row));
            vcopy((r - 1) * SCR_COLS, row, SCR_COLS);
        }
        vfill((SCR_ROWS - 1) * SCR_COLS, ' ' | (attr << 8), SCR_COLS);
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
