/* ClawDOS PLAY: see play.h. */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "play.h"

/* Octave 6 in hundredths of a hertz; lower octaves halve it. */
static const unsigned long octave6[12] = {
    209300ul, 221746ul, 234932ul, 248902ul, 263702ul, 279383ul,
    295996ul, 313596ul, 332244ul, 352000ul, 372931ul, 395107ul
};
static const signed char semitone[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A..G */

typedef struct {
    const char *start, *p;
    char *err;
    int errlen;
} Parser;

static int fail(Parser *ps, const char *at, const char *msg)
{
    char buf[80];
    sprintf(buf, "col %d: %s", (int)(at - ps->start) + 1, msg);
    strncpy(ps->err, buf, ps->errlen - 1);
    ps->err[ps->errlen - 1] = 0;
    return -1;
}

/* An optional number; -1 if there isn't one. */
static long number(Parser *ps)
{
    long n = -1;
    while (isdigit((unsigned char)*ps->p)) {
        n = (n < 0 ? 0 : n * 10) + (*ps->p++ - '0');
        if (n > 9999)
            n = 9999;
    }
    return n;
}

/* A required number in [lo, hi] for command `cmd`. */
static int ranged(Parser *ps, char cmd, int lo, int hi, int *out)
{
    const char *at = ps->p;
    char msg[40];
    long n = number(ps);
    if (n < 0) {
        sprintf(msg, "%c needs a number", cmd);
        return fail(ps, at, msg);
    }
    if (n < lo || n > hi) {
        sprintf(msg, "%c out of range", cmd);
        return fail(ps, at, msg);
    }
    *out = (int)n;
    return 0;
}

static unsigned note_hz(int n)
{
    int k = 6 - (n - 1) / 12;
    return (unsigned)((octave6[(n - 1) % 12] + (50ul << k)) / (100ul << k));
}

int play_run(const char *music, const PlayOps *ops, unsigned long *total_ms,
             char *err, int errlen)
{
    Parser ps;
    int tempo = 120, length = 4, octave = 4, eighths = 7;

    ps.start = ps.p = music;
    ps.err = err;
    ps.errlen = errlen;
    *total_ms = 0;
    err[0] = 0;

    while (*ps.p) {
        const char *at = ps.p;
        char c = (char)toupper((unsigned char)*ps.p++);
        int note = -1, len = length, v;
        unsigned long ms;

        if (c == ' ' || c == ';' || c == ',')
            continue;
        if (c >= 'A' && c <= 'G') {
            note = octave * 12 + semitone[c - 'A'] + 1;
            if (*ps.p == '#' || *ps.p == '+')
                note++, ps.p++;
            else if (*ps.p == '-')
                note--, ps.p++;
            if (note < 1)
                note = 1;
            if (note > 84)
                note = 84;
            if (isdigit((unsigned char)*ps.p) && ranged(&ps, c, 1, 64, &len))
                return -1;
        } else if (c == 'N') {
            if (ranged(&ps, c, 0, 84, &note))
                return -1;
        } else if (c == 'P' || c == 'R') {
            note = 0;
            if (isdigit((unsigned char)*ps.p) && ranged(&ps, c, 1, 64, &len))
                return -1;
        } else if (c == 'O') {
            if (ranged(&ps, c, 0, 6, &octave))
                return -1;
            continue;
        } else if (c == '<' || c == '>') {
            octave += c == '<' ? -1 : 1;
            octave = octave < 0 ? 0 : octave > 6 ? 6 : octave;
            continue;
        } else if (c == 'L') {
            if (ranged(&ps, c, 1, 64, &length))
                return -1;
            continue;
        } else if (c == 'T') {
            if (ranged(&ps, c, 32, 255, &tempo))
                return -1;
            continue;
        } else if (c == 'M') {
            v = toupper((unsigned char)*ps.p);
            if (v == 'N')
                eighths = 7;
            else if (v == 'L')
                eighths = 8;
            else if (v == 'S')
                eighths = 6;
            else if (v != 'F' && v != 'B')
                return fail(&ps, ps.p, "M must be MN, ML, MS, MF or MB");
            ps.p++;
            continue;
        } else {
            char msg[40];
            sprintf(msg, "unexpected '%c'", *at);
            return fail(&ps, at, msg);
        }

        ms = 240000ul / ((unsigned long)tempo * len);
        {
            unsigned long add = ms;
            while (*ps.p == '.') {
                add /= 2;
                ms += add;
                ps.p++;
            }
        }
        *total_ms += ms;
        if (note == 0) {
            ops->tone(ops->ctx, 0, (unsigned)ms);
        } else {
            unsigned on = (unsigned)(ms * eighths / 8);
            ops->tone(ops->ctx, note_hz(note), on);
            if (ms > on)
                ops->tone(ops->ctx, 0, (unsigned)(ms - on));
        }
        if (ops->stop(ops->ctx))
            return 1;
    }
    return 0;
}
