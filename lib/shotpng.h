/* ClawDOS screenshots, the picture: a captured CGA frame (lib/shot.h) in a
 * graphics mode made into the PNG (SHOT_PNG) the tool_result carries as an
 * image block, in HARNESS.EXE (src/harness/tools.cpp decides what a frame
 * becomes; a text mode becomes the screen's text in the result).
 *
 * The PNG is indexed colour, 2 bits per pixel for 320x200 and 1 for 640x200:
 * CGA packs its pixels as PNG does (leftmost in the high bits, 80 bytes a
 * row), so each row is copied as it is, taken from its bank (even rows from
 * offset 0, odd from 2000h). The zlib stream uses one stored block (no
 * compression; an 8088 has no time for deflate), so the file is about 16 KB
 * and its base64 about 22 KB.
 */
#ifndef CLAWDOS_SHOTPNG_H
#define CLAWDOS_SHOTPNG_H

#include "shot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The RGBI colours of the picture into rgb (3 bytes each): 4 for 320x200
   (background, then the palette the colour register and mode pick), 2 for
   640x200 (black, the foreground). Returns how many. */
unsigned shot_palette(const ShotInfo *info, int kind, unsigned char *rgb);
/* The 16 KB interlaced frame as a PNG file: 2 bits per pixel (320x200) or 1
   (640x200), with `colors` RGB triples. 0, or -1 (no memory, a failed write;
   the file is then removed). */
int  png_write_cga(const char *path, const unsigned char __far *frame, int bpp,
                   const unsigned char *rgb, unsigned colors);
#define SHOT_AT_MAX 3000              /* shot_at, tenths of a second: 5 minutes */
/* shot_at's text ("2", "0.5", "1.25") as tenths of a second, 1 to
   SHOT_AT_MAX, in *tenths: 0, or -1 when it is not such a number. */
int  shot_seconds(const char *s, unsigned *tenths);
/* SHOT_RAW (the launcher's capture) into info and the 16 KB at frame: 0, -1. */
int  shot_load_raw(ShotInfo *info, unsigned char __far *frame);

#ifdef __cplusplus
}
#endif

#endif
