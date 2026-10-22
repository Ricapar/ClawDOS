/* Claude-XT gfx: CGA graphics for basdraw scripts (SCREEN 1 = 320x200x4,
 * SCREEN 2 = 640x200x2), drawn straight into video memory. */
#ifndef CXT_GFX_H
#define CXT_GFX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Switch to SCREEN 1, run `script` (basdraw.h), then keep the picture up
   until a key is pressed, or for at most holdSecs if that isn't 0. Returns
   bas_draw's result; *viewed is how long the finished picture was shown.
   The caller saves and restores the text screen around this. */
int gfx_draw(const char *script, int holdSecs, char *err, int errlen, unsigned *viewed);

#ifdef __cplusplus
}
#endif

#endif
