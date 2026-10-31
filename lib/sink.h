/* A byte sink: where a writer puts its output. The same writer runs once into
 * a counting sink (for Content-Length) and once into the socket, so request
 * bodies never have to fit in memory.
 */
#ifndef CLAWDOS_SINK_H
#define CLAWDOS_SINK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sink {
    int (*put)(struct Sink *s, const char *p, unsigned n);  /* NULL: count only; 0 ok, -1 failed */
    void *ctx;
    unsigned long count;                                     /* bytes offered so far */
} Sink;

int  sink_put(Sink *s, const char *p, unsigned n);   /* adds to count, calls put; 0 ok, -1 put failed */
int  sink_puts(Sink *s, const char *z);
void sink_count(Sink *s);                            /* a counting sink */
/* A sink into buf (at most max-1 bytes, always NUL-terminated); extra bytes are
   counted but dropped. */
void sink_buffer(Sink *s, char *buf, unsigned max);

#ifdef __cplusplus
}
#endif

#endif
