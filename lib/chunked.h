/* HTTP/1.1 chunked transfer decoding as a byte-at-a-time state machine, so it
 * works on whatever piece of the body the socket hands over.
 */
#ifndef CLAWDOS_CHUNKED_H
#define CLAWDOS_CHUNKED_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ByteFn)(void *ctx, const char *p, unsigned n);   /* 0 ok */

typedef struct Chunked { int state; unsigned long left; int done; } Chunked;

void chunked_init(Chunked *c);
/* Feed raw body bytes; decoded payload goes to out. Returns 0, 1 once the
   terminating chunk (and its trailer CRLF) is consumed, -1 on a malformed stream. */
int chunked_feed(Chunked *c, const char *in, unsigned n, ByteFn out, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
