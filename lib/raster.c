/* Claude-XT raster: see raster.h. */

#include <stdlib.h>
#include "raster.h"

#define PAINT_STACK 512

void ras_plot(const Raster *r, int x, int y, int c)
{
    if (x >= 0 && y >= 0 && x <= r->maxX && y <= r->maxY)
        r->plot(r->ctx, x, y, c);
}

/* Bresenham. The error terms are long: script coordinates can be far off
   screen, and 16-bit arithmetic would overflow and never reach the end. */
void ras_line(const Raster *r, int x1, int y1, int x2, int y2, int c)
{
    long dx = labs((long)x2 - x1), dy = -labs((long)y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    long e = dx + dy;

    for (;;) {
        long e2 = 2 * e;              /* decide both steps from the same error */
        ras_plot(r, x1, y1, c);
        if (x1 == x2 && y1 == y2)
            break;
        if (e2 >= dy) {
            e += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y1 += sy;
        }
    }
}

/* Clipped to the surface first, so huge spans cost nothing and x++ can't wrap. */
static void hline(const Raster *r, int x1, int x2, int y, int c)
{
    int x;
    if (x1 > x2) {
        x = x1;
        x1 = x2;
        x2 = x;
    }
    if (y < 0 || y > r->maxY || x2 < 0 || x1 > r->maxX)
        return;
    if (x1 < 0)
        x1 = 0;
    if (x2 > r->maxX)
        x2 = r->maxX;
    for (x = x1; x <= x2; x++)
        ras_plot(r, x, y, c);
}

void ras_box(const Raster *r, int x1, int y1, int x2, int y2, int c, int fill)
{
    int y, t;
    if (y1 > y2) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (fill) {
        if (y1 < 0)
            y1 = 0;
        if (y2 > r->maxY)
            y2 = r->maxY;
        for (y = y1; y <= y2; y++)
            hline(r, x1, x2, y, c);
    } else {
        hline(r, x1, x2, y1, c);
        hline(r, x1, x2, y2, c);
        ras_line(r, x1, y1 + 1, x1, y2 - 1, c);
        ras_line(r, x2, y1 + 1, x2, y2 - 1, c);
    }
}

static void plot4(const Raster *r, int cx, int cy, int x, int y, int c)
{
    ras_plot(r, cx + x, cy + y, c);
    ras_plot(r, cx - x, cy + y, c);
    ras_plot(r, cx + x, cy - y, c);
    ras_plot(r, cx - x, cy - y, c);
}

/* Midpoint ellipse. */
void ras_ellipse(const Raster *r, int cx, int cy, int rx, int ry, int c)
{
    long a2 = (long)rx * rx, b2 = (long)ry * ry;
    long x = 0, y = ry, px = 0, py = 2 * a2 * ry, p;

    if (rx <= 0 || ry <= 0) {
        ras_line(r, cx - rx, cy - ry, cx + rx, cy + ry, c);
        return;
    }
    p = b2 - a2 * ry + a2 / 4;
    while (px < py) {
        plot4(r, cx, cy, (int)x, (int)y, c);
        x++;
        px += 2 * b2;
        if (p < 0) {
            p += b2 + px;
        } else {
            y--;
            py -= 2 * a2;
            p += b2 + px - py;
        }
    }
    p = b2 * (2 * x + 1) * (2 * x + 1) / 4 + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y >= 0) {
        plot4(r, cx, cy, (int)x, (int)y, c);
        y--;
        py -= 2 * a2;
        if (p > 0) {
            p += a2 - py;
        } else {
            x++;
            px += 2 * b2;
            p += a2 - py + px;
        }
    }
}

/* Scanline flood fill with an explicit stack of seeds. */
int ras_paint(const Raster *r, int x, int y, int c, int border)
{
    static int sx[PAINT_STACK], sy[PAINT_STACK];
    int n = 0, overflow = 0, polls = 0;

#define FILLABLE(px, py) (r->point(r->ctx, px, py) != border && r->point(r->ctx, px, py) != c)
#define PUSH(px, py) do { if (n < PAINT_STACK) { sx[n] = (px); sy[n] = (py); n++; } \
                          else overflow = 1; } while (0)

    if (x < 0 || y < 0 || x > r->maxX || y > r->maxY)
        return 0;
    PUSH(x, y);
    while (n > 0) {
        int lx, rx, i, row;
        n--;
        x = sx[n];
        y = sy[n];
        if (!FILLABLE(x, y))
            continue;
        lx = rx = x;
        while (lx > 0 && FILLABLE(lx - 1, y))
            lx--;
        while (rx < r->maxX && FILLABLE(rx + 1, y))
            rx++;
        hline(r, lx, rx, y, c);
        for (row = y - 1; row <= y + 1; row += 2) {
            int inSpan = 0;
            if (row < 0 || row > r->maxY)
                continue;
            for (i = lx; i <= rx; i++) {
                if (FILLABLE(i, row)) {
                    if (!inSpan)
                        PUSH(i, row);
                    inSpan = 1;
                } else {
                    inSpan = 0;
                }
            }
        }
        if (r->stop && ++polls == 16) {
            polls = 0;
            if (r->stop(r->ctx))
                return 1;
        }
    }
#undef FILLABLE
#undef PUSH
    return overflow ? -1 : 0;
}
