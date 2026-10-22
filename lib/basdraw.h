/* Claude-XT basdraw: a GW-BASIC-style drawing script, run statement by
 * statement against a DrawOps backend (gfx.h draws on the CGA; the unit
 * tests record the calls).
 *
 * Statements, separated by newlines or ':' (leading line numbers and REM /
 * ' comments are ignored): SCREEN 1|2, COLOR bg[,palette], CLS,
 * PSET/PRESET (x,y)[,c], LINE [(x1,y1)]-(x2,y2)[,[c][,B|BF]],
 * CIRCLE (x,y),r[,[c][,[start][,[end][,aspect]]]] (start/end are ignored),
 * PAINT (x,y)[,[c][,border]], DRAW "turtle", LOCATE row[,col],
 * PRINT ["text"][;], SLEEP n, END. STEP(dx,dy) is relative to the last
 * point. Arguments are numbers with + - * / and parentheses; no variables.
 */
#ifndef CXT_BASDRAW_H
#define CXT_BASDRAW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DrawOps {
    void *ctx;
    void (*screen)(void *ctx, int mode);                       /* 1 or 2 */
    void (*color)(void *ctx, int bg, int palette);
    void (*cls)(void *ctx);
    void (*pset)(void *ctx, int x, int y, int c);
    void (*line)(void *ctx, int x1, int y1, int x2, int y2, int c);
    void (*box)(void *ctx, int x1, int y1, int x2, int y2, int c, int fill);
    void (*ellipse)(void *ctx, int x, int y, int rx, int ry, int c);
    int  (*paint)(void *ctx, int x, int y, int c, int border); /* -1: too complex */
    void (*text)(void *ctx, int row, int col, const char *s, int c);
    void (*sleep)(void *ctx, int secs);
    int  (*stop)(void *ctx);                                   /* checked after each statement */
} DrawOps;

/* 0 = done, 1 = stopped, -1 = error (err gets "line N: KEYWORD: message";
   everything before the failing statement has been drawn). The caller sets
   up SCREEN 1 before running. */
int bas_draw(const char *script, const DrawOps *ops, char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif
