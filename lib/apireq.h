/* Writing a Messages API request: the head, and the body streamed out of the
 * prompt file, the tools file and the history file. The body writer runs twice,
 * into a counting sink for Content-Length and then into the socket.
 */
#ifndef CLAWDOS_APIREQ_H
#define CLAWDOS_APIREQ_H

#include "sink.h"
#include "cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ApiReq {
    const Provider *p;
    const char *promptPath;      /* PROMPT.TXT: CP437 text with {name} and {memories} */
    const char *toolsPath;       /* TOOLS.JSN: the tools array, copied verbatim */
    const char *name;            /* for {name} */
    const char *memories;        /* for {memories}; "" shows as "(none)" */
    int noTools;                 /* the last round: tool_choice none */
} ApiReq;

/* The JSON body into the sink. 0 ok, -1 a file could not be read, -2 the sink failed. */
int api_body(Sink *s, const ApiReq *r);
/* "POST path HTTP/1.1\r\n" + headers + "\r\n" for a body of `length` bytes. */
int api_head(Sink *s, const Provider *p, unsigned long length);

#ifdef __cplusplus
}
#endif

#endif
