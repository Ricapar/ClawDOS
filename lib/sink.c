/* Byte sinks, see sink.h. */
#include <string.h>
#include "sink.h"

int sink_put(Sink *s, const char *p, unsigned n)
{
    s->count += n;
    return s->put ? s->put(s, p, n) : 0;
}

int sink_puts(Sink *s, const char *z)
{
    return sink_put(s, z, strlen(z));
}

void sink_count(Sink *s)
{
    s->put = 0;
    s->ctx = 0;
    s->count = 0;
}

struct BufCtx { char *buf; unsigned max, len; };
static struct BufCtx bufCtx;          /* one buffer sink at a time (tests) */

static int bufPut(Sink *s, const char *p, unsigned n)
{
    struct BufCtx *b = (struct BufCtx *)s->ctx;
    unsigned room = b->max - 1 - b->len;
    if (n > room)
        n = room;
    memcpy(b->buf + b->len, p, n);
    b->len += n;
    b->buf[b->len] = 0;
    return 0;
}

void sink_buffer(Sink *s, char *buf, unsigned max)
{
    bufCtx.buf = buf;
    bufCtx.max = max;
    bufCtx.len = 0;
    buf[0] = 0;
    s->put = bufPut;
    s->ctx = &bufCtx;
    s->count = 0;
}
