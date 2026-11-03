/* SSE splitting, see sse.h. */
#include <string.h>
#include "sse.h"

enum { F_START, F_NAME, F_SKIPSPACE, F_DATA, F_EVENT, F_OTHER };

void sse_init(Sse *s, char *buf, unsigned max)
{
    s->buf = buf;
    s->max = max;
    s->len = 0;
    s->event[0] = 0;
    s->evlen = 0;
    s->field = F_START;
    s->fieldPos = 0;
    s->overflow = 0;
    s->dropping = 0;
    s->maxLen = 0;
    s->big = 0;
    s->bigging = 0;
    s->bigLen = 0;
}

static char nameBuf[8];                       /* field name being read */

static void endLine(Sse *s, SseEventFn fn, void *ctx)
{
    if (s->field == F_START) {                /* blank line: dispatch */
        if (s->len && s->buf[s->len - 1] == '\n')   /* the separator after the last data line */
            s->len--;
        if (s->bigging) {                     /* the end of a long event */
            s->bigLen += s->len;
            if (s->len)
                s->big(ctx, SSE_BIG_MORE, s->event, s->buf, s->len);
            s->big(ctx, SSE_BIG_END, s->event, s->buf, 0);
            if (s->bigLen > s->maxLen)
                s->maxLen = s->bigLen > 0xFFFFu ? 0xFFFFu : (unsigned)s->bigLen;
            s->bigging = 0;
            s->len = 0;
        }
        if (s->len > s->maxLen)
            s->maxLen = s->len;
        if (s->len && !s->dropping) {
            s->buf[s->len] = 0;
            s->event[s->evlen] = 0;
            fn(ctx, s->event, s->buf, s->len);
        }
        s->len = 0;
        s->evlen = 0;
        s->dropping = 0;
    } else if (s->field == F_DATA) {
        if (s->len < s->max - 1)              /* separator for a following data line */
            s->buf[s->len++] = '\n';
    }
    s->field = F_START;
    s->fieldPos = 0;
}

int sse_feed(Sse *s, const char *in, unsigned n, SseEventFn fn, void *ctx)
{
    unsigned i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        char ch = in[i];
        if (ch == '\r')
            continue;
        if (ch == '\n') {
            endLine(s, fn, ctx);
            continue;
        }
        switch (s->field) {
        case F_START:
            if (ch == ':') { s->field = F_OTHER; break; }        /* comment */
            s->field = F_NAME;
            s->fieldPos = 0;
            /* fall through */
        case F_NAME:
            if (ch == ':') {
                nameBuf[s->fieldPos] = 0;
                s->field = !strcmp(nameBuf, "data") ? F_DATA : !strcmp(nameBuf, "event") ? F_EVENT : F_OTHER;
                s->fieldPos = 0;
                if (s->field != F_OTHER)
                    s->field += 100;          /* expect one optional space first */
            } else if (s->fieldPos < sizeof nameBuf - 1) {
                nameBuf[s->fieldPos++] = ch;
            } else {
                s->field = F_OTHER;
            }
            break;
        case F_DATA + 100:
        case F_EVENT + 100:
            s->field -= 100;
            if (ch == ' ')
                break;
            /* fall through: first byte of the value */
        case F_DATA:
        case F_EVENT:
            if (s->field == F_DATA) {
                if (s->dropping)
                    break;
                if (s->len >= s->max - 1 && s->big) {   /* full: hand it on in pieces */
                    s->event[s->evlen] = 0;
                    if (s->bigging) {
                        s->bigLen += s->len;
                        s->big(ctx, SSE_BIG_MORE, s->event, s->buf, s->len);
                        s->len = 0;
                    } else if (s->big(ctx, SSE_BIG_BEGIN, s->event, s->buf, s->len) == 0) {
                        s->bigging = 1;
                        s->bigLen = s->len;
                        s->len = 0;
                    }
                }
                if (s->len >= s->max - 1) {
                    s->overflow = 1;
                    s->dropping = 1;
                    s->len = 0;
                    rc = -1;
                } else {
                    s->buf[s->len++] = ch;
                }
            } else if (s->evlen < sizeof s->event - 1) {
                s->event[s->evlen++] = ch;
            }
            break;
        default:                              /* F_OTHER: ignore to end of line */
            break;
        }
    }
    return rc;
}
