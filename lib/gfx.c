/* Claude-XT gfx: see gfx.h.
 *
 * CGA graphics memory is interleaved: even rows at B800:0000, odd rows at
 * B800:2000, 80 bytes per row. SCREEN 1 packs 4 pixels per byte (2 bits,
 * leftmost pixel in the top bits), SCREEN 2 packs 8 (1 bit).
 */

#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include <bios.h>
#include "toolapi.h"
#include "basdraw.h"
#include "raster.h"
#include "gfx.h"

static unsigned char __far *vram = (unsigned char __far *)MK_FP(0xB800, 0);
static volatile unsigned long __far *ticks = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
static int mode = 1, maxX = 319;
static int stopped;
static struct Raster ras;

static void setMode(int m)
{
    union REGS r;
    mode = m;
    maxX = ras.maxX = m == 1 ? 319 : 639;
    r.x.ax = m == 1 ? 0x0004 : 0x0006;
    int86(0x10, &r, &r);
}

static unsigned offset(int x, int y)
{
    return (unsigned)((y & 1) * 0x2000 + (y >> 1) * 80 + (mode == 1 ? x >> 2 : x >> 3));
}

static void plot(int x, int y, int c)
{
    unsigned char __far *p;
    int shift;
    unsigned char mask;

    if (x < 0 || y < 0 || x > maxX || y > 199)
        return;
    p = vram + offset(x, y);
    if (mode == 1) {
        shift = (3 - (x & 3)) * 2;
        mask = (unsigned char)(3 << shift);
        *p = (unsigned char)((*p & ~mask) | ((c & 3) << shift));
    } else {
        mask = (unsigned char)(0x80 >> (x & 7));
        *p = (unsigned char)(c & 1 ? *p | mask : *p & ~mask);
    }
}

static int point(int x, int y)
{
    unsigned char b = vram[offset(x, y)];
    if (mode == 1)
        return (b >> ((3 - (x & 3)) * 2)) & 3;
    return (b >> (7 - (x & 7))) & 1;
}

/* --- DrawOps ----------------------------------------------------------------- */

static void opScreen(void *ctx, int m)
{
    (void)ctx;
    setMode(m);
}

static void opColor(void *ctx, int bg, int palette)
{
    union REGS r;
    (void)ctx;
    r.h.ah = 0x0B;
    r.h.bh = 0;                       /* background / border colour */
    r.h.bl = (unsigned char)(bg & 15);
    int86(0x10, &r, &r);
    if (mode == 1) {
        r.h.ah = 0x0B;
        r.h.bh = 1;                   /* palette */
        r.h.bl = (unsigned char)(palette & 1);
        int86(0x10, &r, &r);
    }
}

static void opCls(void *ctx)
{
    (void)ctx;
    _fmemset(vram, 0, 0x4000);
}

static void opPset(void *ctx, int x, int y, int c)
{
    (void)ctx;
    plot(x, y, c);
}

static int stopCheck(void *ctx)
{
    (void)ctx;
    if (tool_esc())
        stopped = 1;
    return stopped;
}

static void rasPlot(void *ctx, int x, int y, int c)
{
    (void)ctx;
    plot(x, y, c);
}

static int rasPoint(void *ctx, int x, int y)
{
    (void)ctx;
    return point(x, y);
}

static Raster ras = { 0, rasPlot, rasPoint, stopCheck, 319, 199 };

static void opLine(void *ctx, int x1, int y1, int x2, int y2, int c)
{
    (void)ctx;
    ras_line(&ras, x1, y1, x2, y2, c);
}

static void opBox(void *ctx, int x1, int y1, int x2, int y2, int c, int fill)
{
    (void)ctx;
    ras_box(&ras, x1, y1, x2, y2, c, fill);
}

static void opEllipse(void *ctx, int cx, int cy, int rx, int ry, int c)
{
    (void)ctx;
    ras_ellipse(&ras, cx, cy, rx, ry, c);
}

static int opPaint(void *ctx, int x, int y, int c, int border)
{
    (void)ctx;
    return ras_paint(&ras, x, y, c, border) < 0 ? -1 : 0;
}

static void opText(void *ctx, int row, int col, const char *s, int c)
{
    union REGS r;
    (void)ctx;
    if (row < 1 || row > 25 || col < 1)
        return;
    r.h.ah = 0x02;                    /* cursor position */
    r.h.bh = 0;
    r.h.dh = (unsigned char)(row - 1);
    r.h.dl = (unsigned char)(col - 1);
    int86(0x10, &r, &r);
    for (; *s && col <= (mode == 1 ? 40 : 80); s++, col++) {
        r.h.ah = 0x0E;                /* teletype; BL = colour in graphics modes */
        r.h.al = (unsigned char)*s;
        r.h.bl = (unsigned char)c;
        int86(0x10, &r, &r);
    }
}

static void opSleep(void *ctx, int secs)
{
    unsigned long start = *ticks;
    (void)ctx;
    if (secs > 60)
        secs = 60;
    while (!stopped && *ticks - start < (unsigned long)secs * 182 / 10)
        if (tool_esc())
            stopped = 1;
}

static const DrawOps gfxOps = {
    0, opScreen, opColor, opCls, opPset, opLine, opBox, opEllipse, opPaint, opText,
    opSleep, stopCheck
};

int gfx_draw(const char *script, int holdSecs, char *err, int errlen, unsigned *viewed)
{
    unsigned long start;
    int rc;

    stopped = tool_esc();
    *viewed = 0;
    setMode(1);
    rc = bas_draw(script, &gfxOps, err, errlen);
    if (rc == 0 && stopped)
        rc = 1;
    if (rc == 1)
        return rc;

    start = *ticks;                   /* show the picture */
    while (_bios_keybrd(_KEYBRD_READY))
        _bios_keybrd(_KEYBRD_READ);
    while (!_bios_keybrd(_KEYBRD_READY)) {
        if (holdSecs && *ticks - start >= (unsigned long)holdSecs * 182 / 10)
            break;
    }
    if (_bios_keybrd(_KEYBRD_READY))
        _bios_keybrd(_KEYBRD_READ);
    *viewed = (unsigned)((*ticks - start) * 10 / 182);
    return rc;
}
