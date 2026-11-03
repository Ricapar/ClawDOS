/* Server-sent events: splits a text/event-stream body into (event, data)
 * pairs, one byte at a time so socket pieces can end anywhere.
 */
#ifndef CLAWDOS_SSE_H
#define CLAWDOS_SSE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*SseEventFn)(void *ctx, const char *event, const char *data, unsigned datalen);

/* An event whose data outgrows the buffer, in pieces: SSE_BIG_BEGIN with the
   event name and the data so far, SSE_BIG_MORE with each piece after it,
   SSE_BIG_END at its end. Return nonzero from BEGIN to refuse it (it is then
   dropped and sse_feed returns -1, as without a handler); later return values
   are ignored (the handler keeps its own error state). */
enum { SSE_BIG_BEGIN, SSE_BIG_MORE, SSE_BIG_END };
typedef int (*SseBigFn)(void *ctx, int phase, const char *event, const char *data, unsigned n);

typedef struct Sse {
    char *buf;              /* data payload being collected */
    unsigned max, len;
    char event[32];
    unsigned evlen;
    int field;              /* which field the current line is (private) */
    unsigned fieldPos;
    int overflow;           /* a data payload exceeded max; the event was dropped */
    unsigned maxLen;        /* the longest data payload seen (diagnostics) */
    int dropping;
    SseBigFn big;           /* optional: takes events too long for buf (set after sse_init) */
    int bigging;            /* the current event is going to `big` in pieces */
    unsigned long bigLen;   /* its length so far */
} Sse;

void sse_init(Sse *s, char *buf, unsigned max);
/* Feed decoded body bytes. Lines: "event: x", "data: y" (a second data line
   appends with '\n'), ":" comments, blank line = dispatch (events without data
   are ignored). Returns 0, or -1 once a data payload exceeds max and no `big`
   handler takes it (s->overflow set; that event is dropped and parsing
   continues with the next). maxLen counts the events `big` took too. */
int sse_feed(Sse *s, const char *in, unsigned n, SseEventFn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
