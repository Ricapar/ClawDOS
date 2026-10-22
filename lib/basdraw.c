/* Claude-XT basdraw: see basdraw.h. Numbers are fixed point with 8
 * fractional bits, so ".5" and "5/6" work for CIRCLE aspects. */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "basdraw.h"

#define FIX 256L

typedef struct {
    const DrawOps *ops;
    const char *p;
    int line;                         /* source line of the current statement */
    const char *kw;                   /* its keyword, for error messages */
    char *err;
    int errlen;
    int failed;

    int mode, fg;                     /* SCREEN and its default colour */
    int lastX, lastY;
    int row, col;                     /* PRINT cursor */
    int drawColor, scale, angle;      /* DRAW state; angle in degrees */
} Bas;

/* sin(0..90 degrees) * 256 */
static const unsigned char sin90[91] = {
      0,   4,   9,  13,  18,  22,  27,  31,  36,  40,  44,  49,  53,  58,  62,  66,
     71,  75,  79,  83,  88,  92,  96, 100, 104, 108, 112, 116, 120, 124, 128, 132,
    136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187,
    190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228,
    230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251,
    252, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255
};

static int fail(Bas *b, const char *msg)
{
    char buf[120];
    if (b->failed)
        return -1;
    sprintf(buf, "line %d: %s: %s", b->line, b->kw, msg);
    strncpy(b->err, buf, b->errlen - 1);
    b->err[b->errlen - 1] = 0;
    b->failed = 1;
    return -1;
}

static void spaces(Bas *b)
{
    while (*b->p == ' ' || *b->p == '\t' || *b->p == '\r')
        b->p++;
}

static int accept(Bas *b, char c)
{
    spaces(b);
    if (*b->p == c) {
        b->p++;
        return 1;
    }
    return 0;
}

static int expect(Bas *b, char c)
{
    char msg[16];
    if (accept(b, c))
        return 0;
    sprintf(msg, "expected %c", c);
    return fail(b, msg);
}

/* Is the next word `w` (case-insensitive)? Consumes it if so. */
static int word(Bas *b, const char *w)
{
    unsigned n = strlen(w), i;
    spaces(b);
    for (i = 0; i < n; i++)
        if (toupper((unsigned char)b->p[i]) != w[i])
            return 0;
    if (isalpha((unsigned char)b->p[n]))
        return 0;
    b->p += n;
    return 1;
}

static int roundFix(long v)
{
    return (int)(v >= 0 ? (v + FIX / 2) / FIX : -((-v + FIX / 2) / FIX));
}


/* --- expressions --------------------------------------------------------------- */

static long expr(Bas *b);

static long factor(Bas *b)
{
    spaces(b);
    if (*b->p == '-') {
        b->p++;
        return -factor(b);
    }
    if (*b->p == '+') {
        b->p++;
        return factor(b);
    }
    if (*b->p == '(') {
        long v;
        b->p++;
        v = expr(b);
        expect(b, ')');
        return v;
    }
    if (isdigit((unsigned char)*b->p) || (*b->p == '.' && isdigit((unsigned char)b->p[1]))) {
        long whole = 0, frac = 0, scale = 1;
        while (isdigit((unsigned char)*b->p)) {
            whole = whole * 10 + (*b->p++ - '0');
            if (whole > 30000)
                whole = 30000;
        }
        if (*b->p == '.') {
            b->p++;
            while (isdigit((unsigned char)*b->p)) {
                if (scale < 10000) {
                    frac = frac * 10 + (*b->p - '0');
                    scale *= 10;
                }
                b->p++;
            }
        }
        return whole * FIX + frac * FIX / scale;
    }
    fail(b, "expected a number");
    return 0;
}

static long term(Bas *b)
{
    long v = factor(b);
    for (;;) {
        spaces(b);
        if (*b->p == '*') {
            b->p++;
            v = v * factor(b) / FIX;
        } else if (*b->p == '/') {
            long d;
            b->p++;
            d = factor(b);
            if (d == 0) {
                fail(b, "division by zero");
                return 0;
            }
            v = v * FIX / d;
        } else {
            return v;
        }
    }
}

static long expr(Bas *b)
{
    long v = term(b);
    for (;;) {
        spaces(b);
        if (*b->p == '+') {
            b->p++;
            v += term(b);
        } else if (*b->p == '-') {
            b->p++;
            v -= term(b);
        } else {
            return v;
        }
    }
}

static int intExpr(Bas *b)
{
    return roundFix(expr(b));
}

/* "," followed by an optional value; returns 1 if a value was given. A
   missing comma counts as "not given" without consuming anything. */
static int optArg(Bas *b, long *v)
{
    if (!accept(b, ','))
        return 0;
    spaces(b);
    if (*b->p == ',' || *b->p == ':' || *b->p == '\n' || *b->p == 0 || *b->p == '\''
        || isalpha((unsigned char)*b->p))
        return 0;
    *v = expr(b);
    return 1;
}

/* [STEP] (x, y) */
static int point(Bas *b, int *x, int *y)
{
    int step = word(b, "STEP");
    if (expect(b, '('))
        return -1;
    *x = intExpr(b);
    if (expect(b, ','))
        return -1;
    *y = intExpr(b);
    if (expect(b, ')'))
        return -1;
    if (step) {
        *x += b->lastX;
        *y += b->lastY;
    }
    return b->failed ? -1 : 0;
}

static int colorArg(Bas *b, int def)
{
    long v;
    return optArg(b, &v) ? roundFix(v) : def;
}


/* --- statements ---------------------------------------------------------------- */

static int stPset(Bas *b, int preset)
{
    int x, y, c;
    if (point(b, &x, &y))
        return -1;
    c = colorArg(b, preset ? 0 : b->fg);
    b->ops->pset(b->ops->ctx, x, y, c);
    b->lastX = x;
    b->lastY = y;
    return 0;
}

static int stLine(Bas *b)
{
    int x1 = b->lastX, y1 = b->lastY, x2, y2, c = b->fg, box = 0;
    long v;

    spaces(b);
    if (*b->p != '-' && point(b, &x1, &y1))
        return -1;
    if (expect(b, '-') || point(b, &x2, &y2))
        return -1;
    if (optArg(b, &v))
        c = roundFix(v);
    if (accept(b, ',')) {
        if (word(b, "BF"))
            box = 2;
        else if (word(b, "B"))
            box = 1;
        else
            return fail(b, "expected B or BF");
    }
    if (box)
        b->ops->box(b->ops->ctx, x1, y1, x2, y2, c, box == 2);
    else
        b->ops->line(b->ops->ctx, x1, y1, x2, y2, c);
    b->lastX = x2;
    b->lastY = y2;
    return 0;
}

static int stCircle(Bas *b)
{
    int x, y, r, c = b->fg, rx, ry;
    long v, aspect = b->mode == 1 ? 5 * FIX / 6 : 5 * FIX / 12;

    if (point(b, &x, &y) || expect(b, ','))
        return -1;
    r = intExpr(b);
    if (optArg(b, &v))
        c = roundFix(v);
    optArg(b, &v);                    /* start and end angles: arcs aren't supported */
    optArg(b, &v);
    if (optArg(b, &v)) {
        if (v <= 0)
            return fail(b, "aspect must be positive");
        aspect = v;
    }
    if (aspect < FIX) {
        rx = r;
        ry = roundFix((long)r * aspect);
    } else {
        ry = r;
        rx = roundFix((long)r * FIX * FIX / aspect);
    }
    b->ops->ellipse(b->ops->ctx, x, y, rx, ry, c);
    b->lastX = x;
    b->lastY = y;
    return 0;
}

static int stPaint(Bas *b)
{
    int x, y, c, border;
    if (point(b, &x, &y))
        return -1;
    c = colorArg(b, b->fg);
    border = colorArg(b, c);
    if (b->ops->paint(b->ops->ctx, x, y, c, border) < 0)
        return fail(b, "area too complex");
    b->lastX = x;
    b->lastY = y;
    return 0;
}

/* A quoted string into buf (at most n-1 characters). */
static int string(Bas *b, char *buf, unsigned n)
{
    unsigned len = 0;
    if (expect(b, '"'))
        return -1;
    while (*b->p && *b->p != '"' && *b->p != '\n') {
        if (len + 1 < n)
            buf[len++] = *b->p;
        b->p++;
    }
    buf[len] = 0;
    if (*b->p == '"')
        b->p++;
    return 0;
}

static int stPrint(Bas *b)
{
    char text[82];
    spaces(b);
    text[0] = 0;
    if (*b->p == '"' && string(b, text, sizeof(text)))
        return -1;
    if (text[0])
        b->ops->text(b->ops->ctx, b->row, b->col, text, b->fg);
    if (accept(b, ';')) {
        b->col += strlen(text);
    } else {
        b->row++;
        b->col = 1;
    }
    return 0;
}

/* Turn (dx, dy) by the DRAW angle, counterclockwise on screen. */
static void rotate(Bas *b, long *dx, long *dy)
{
    int a = ((b->angle % 360) + 360) % 360;
    long s, c, x = *dx, y = *dy;
    int q = a / 90, r = a % 90;

    while (q--) {                     /* quarter turns first, exactly */
        long t = x;
        x = y;
        y = -t;
    }
    if (r) {
        s = sin90[r];
        c = sin90[90 - r];
        *dx = (x * c + y * s) / FIX;
        *dy = (y * c - x * s) / FIX;
    } else {
        *dx = x;
        *dy = y;
    }
}

static int turtle(Bas *b, const char *s)
{
    static const signed char dirs[8][2] = {
        { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 }, { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }
    };
    Bas sub = *b;                     /* parse numbers inside the string */

    while (*s) {
        int noDraw = 0, back = 0, x0 = b->lastX, y0 = b->lastY, x, y;
        char c;
        const char *d;

        while (*s == ' ' || *s == ';')
            s++;
        if (!*s)
            break;
        for (;;) {
            c = (char)toupper((unsigned char)*s);
            if (c == 'B' && !noDraw) {
                noDraw = 1;
                s++;
            } else if (c == 'N' && !back) {
                back = 1;
                s++;
            } else {
                break;
            }
            while (*s == ' ')
                s++;
        }
        s++;
        sub.p = s;
        sub.failed = 0;
        if ((d = strchr("UDLREFGH", c)) != NULL && c) {
            long dx, dy, n = FIX;
            spaces(&sub);
            if (isdigit((unsigned char)*sub.p) || *sub.p == '(')
                n = expr(&sub);
            dx = dirs[d - "UDLREFGH"][0] * n * b->scale / 4;
            dy = dirs[d - "UDLREFGH"][1] * n * b->scale / 4;
            rotate(b, &dx, &dy);
            x = x0 + roundFix(dx);
            y = y0 + roundFix(dy);
        } else if (c == 'M') {
            int rel;
            spaces(&sub);
            rel = *sub.p == '+' || *sub.p == '-';
            x = intExpr(&sub);
            if (!accept(&sub, ','))
                return fail(b, "M needs x,y");
            y = intExpr(&sub);
            if (rel) {
                long dx = (long)x * FIX * b->scale / 4, dy = (long)y * FIX * b->scale / 4;
                rotate(b, &dx, &dy);
                x = x0 + roundFix(dx);
                y = y0 + roundFix(dy);
            }
        } else if (c == 'C' || c == 'S' || c == 'A' || (c == 'T' && toupper((unsigned char)*s) == 'A')) {
            int v;
            if (c == 'T')
                sub.p++;
            v = intExpr(&sub);
            if (c == 'C')
                b->drawColor = v;
            else if (c == 'S')
                b->scale = v > 0 ? v : 4;
            else if (c == 'A')
                b->angle = (v & 3) * 90;
            else
                b->angle = v;
            if (sub.failed)
                return fail(b, "bad number in DRAW string");
            s = sub.p;
            continue;
        } else {
            char msg[30];
            sprintf(msg, "unknown command %c", c ? c : '?');
            return fail(b, msg);
        }
        if (sub.failed)
            return fail(b, "bad number in DRAW string");
        s = sub.p;
        if (!noDraw)
            b->ops->line(b->ops->ctx, x0, y0, x, y, b->drawColor);
        if (!back) {
            b->lastX = x;
            b->lastY = y;
        }
    }
    return 0;
}

static int stDraw(Bas *b)
{
    char s[256];
    if (string(b, s, sizeof(s)))
        return -1;
    return turtle(b, s);
}

static int statement(Bas *b)
{
    char kw[12];
    unsigned n = 0;
    long v;

    spaces(b);
    while (isalpha((unsigned char)*b->p) && n < sizeof(kw) - 1)
        kw[n++] = (char)toupper((unsigned char)*b->p++);
    kw[n] = 0;
    while (isalpha((unsigned char)*b->p))
        b->p++;
    b->kw = kw;

    if (!strcmp(kw, "REM")) {
        while (*b->p && *b->p != '\n')
            b->p++;
        return 0;
    }
    if (!strcmp(kw, "SCREEN")) {
        int m = intExpr(b);
        if (b->failed)
            return -1;
        if (m != 1 && m != 2)
            return fail(b, "only SCREEN 1 or 2");
        b->mode = m;
        b->fg = b->drawColor = m == 1 ? 3 : 1;
        b->ops->screen(b->ops->ctx, m);
    } else if (!strcmp(kw, "COLOR")) {
        int bg = intExpr(b), pal = 0;
        if (optArg(b, &v))
            pal = roundFix(v);
        if (!b->failed)
            b->ops->color(b->ops->ctx, bg, pal);
    } else if (!strcmp(kw, "CLS")) {
        b->ops->cls(b->ops->ctx);
    } else if (!strcmp(kw, "PSET") || !strcmp(kw, "PRESET")) {
        return stPset(b, kw[1] == 'R');
    } else if (!strcmp(kw, "LINE")) {
        return stLine(b);
    } else if (!strcmp(kw, "CIRCLE")) {
        return stCircle(b);
    } else if (!strcmp(kw, "PAINT")) {
        return stPaint(b);
    } else if (!strcmp(kw, "DRAW")) {
        return stDraw(b);
    } else if (!strcmp(kw, "LOCATE")) {
        b->row = intExpr(b);
        if (optArg(b, &v))
            b->col = roundFix(v);
    } else if (!strcmp(kw, "PRINT")) {
        return stPrint(b);
    } else if (!strcmp(kw, "SLEEP")) {
        int secs = intExpr(b);
        if (!b->failed)
            b->ops->sleep(b->ops->ctx, secs);
    } else if (!strcmp(kw, "END")) {
        return 1;
    } else {
        return fail(b, n ? "unknown statement" : "expected a statement");
    }
    return b->failed ? -1 : 0;
}

int bas_draw(const char *script, const DrawOps *ops, char *err, int errlen)
{
    Bas b;
    char emptyKw[1] = "";

    memset(&b, 0, sizeof(b));
    b.ops = ops;
    b.p = script;
    b.line = 1;
    b.kw = emptyKw;
    b.err = err;
    b.errlen = errlen;
    b.mode = 1;
    b.fg = b.drawColor = 3;
    b.row = b.col = 1;
    b.scale = 4;
    err[0] = 0;

    for (;;) {
        int rc;
        spaces(&b);
        if (*b.p == 0)
            return 0;
        if (*b.p == '\n') {
            b.p++;
            b.line++;
            while (isdigit((unsigned char)*b.p))    /* a BASIC line number */
                b.p++;
            continue;
        }
        if (*b.p == ':')  {
            b.p++;
            continue;
        }
        if (b.line == 1 && b.p == script)
            while (isdigit((unsigned char)*b.p))
                b.p++;
        spaces(&b);
        if (*b.p == '\'') {
            while (*b.p && *b.p != '\n')
                b.p++;
            continue;
        }
        if (*b.p == '\n' || *b.p == 0)
            continue;
        rc = statement(&b);
        if (rc < 0)
            return -1;
        if (rc > 0)
            return 0;
        spaces(&b);
        if (*b.p == '\'') {
            while (*b.p && *b.p != '\n')
                b.p++;
        } else if (*b.p && *b.p != '\n' && *b.p != ':') {
            char msg[30];
            sprintf(msg, "unexpected '%c'", *b.p);
            return fail(&b, msg);
        }
        if (ops->stop(ops->ctx))
            return 1;
    }
}
