/* Claude-XT raster: line, ellipse and flood fill on any pixel surface given
 * as plot/point callbacks (gfx.c uses CGA memory; the unit tests use a
 * bitmap in RAM). Coordinates outside 0..maxX, 0..maxY are clipped. */
#ifndef CXT_RASTER_H
#define CXT_RASTER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Raster {
    void *ctx;
    void (*plot)(void *ctx, int x, int y, int c);   /* called only for on-surface pixels */
    int  (*point)(void *ctx, int x, int y);
    int  (*stop)(void *ctx);                        /* polled during long fills; may be NULL */
    int maxX, maxY;
} Raster;

void ras_plot(const Raster *r, int x, int y, int c);
void ras_line(const Raster *r, int x1, int y1, int x2, int y2, int c);
void ras_box(const Raster *r, int x1, int y1, int x2, int y2, int c, int fill);
void ras_ellipse(const Raster *r, int cx, int cy, int rx, int ry, int c);
/* Fill the area around (x, y) bounded by `border`: 0 done, 1 stopped,
   -1 too complex (out of seed stack; partly filled). */
int  ras_paint(const Raster *r, int x, int y, int c, int border);

#ifdef __cplusplus
}
#endif

#endif
