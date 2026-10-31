/* Server-sent events: splits a text/event-stream body into (event, data)
 * pairs, one byte at a time so socket pieces can end anywhere.
 */
#ifndef CLAWDOS_SSE_H
#define CLAWDOS_SSE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*SseEventFn)(void *ctx, const char *event, const char *data, unsigned datalen);

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
} Sse;

void sse_init(Sse *s, char *buf, unsigned max);
/* Feed decoded body bytes. Lines: "event: x", "data: y" (a second data line
   appends with '\n'), ":" comments, blank line = dispatch (events without data
   are ignored). Returns 0, or -1 once a data payload exceeds max (s->overflow
   set; that event is dropped and parsing continues with the next). */
int sse_feed(Sse *s, const char *in, unsigned n, SseEventFn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
