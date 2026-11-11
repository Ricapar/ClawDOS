/* Writing a Messages API request: the head, and the body streamed out of the
 * conversation's prefix file and the history file.
 *
 * The prefix ("system" and "tools") is rendered once per conversation into a
 * file beside the chat (CHATS\MMDDnnn.PRE), from PROMPT.TXT with {name} and
 * {memories} filled in and TOOLS.JSN, and every request of the conversation
 * sends those bytes unchanged: thinking blocks are bound to the exact prefix
 * that produced them, and the prompt cache wants it too. A request is
 *
 *   {<fields>,<prefix>,"messages":[<history>]}
 *
 * whose length is known before a byte is written: the fields are a few short
 * strings, the prefix and the history are files of known size.
 */
#ifndef CLAWDOS_APIREQ_H
#define CLAWDOS_APIREQ_H

#include "sink.h"
#include "cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a conversation's prefix is made from. */
typedef struct ApiPrefix {
    const char *promptPath;      /* PROMPT.TXT: CP437 text with {name} and {memories} */
    const char *toolsPath;       /* TOOLS.JSN: the tools array, copied verbatim */
    const char *name;            /* for {name} */
    const char *memories;        /* for {memories}; "" shows as "(none)" */
    int cache;                   /* the system block's cache_control marker */
} ApiPrefix;

/* "system":[...],"tools":[...] into the sink. 0 ok, -1 a file could not be
   read, -2 the sink failed. */
int  api_prefix(Sink *s, const ApiPrefix *x);
/* The same into the file at path (written beside it first, then renamed, so
   the file is whole or absent). Its size, or -1. */
long api_prefix_file(const char *path, const ApiPrefix *x);

typedef struct ApiReq {
    const Provider *p;
    const char *prefixPath;      /* the conversation's prefix file */
    long prefixLen;              /* its size */
    int noTools;                 /* the last round: tool_choice none */
} ApiReq;

/* The body's length: what api_body will write, from the sizes alone. */
unsigned long api_length(const ApiReq *r);
/* The JSON body into the sink. 0 ok, -1 a file could not be read (or was not
   the size counted), -2 the sink failed. */
int api_body(Sink *s, const ApiReq *r);
/* "POST path HTTP/1.1\r\n" + headers + "\r\n" for a body of `length` bytes. */
int api_head(Sink *s, const Provider *p, unsigned long length);

#ifdef __cplusplus
}
#endif

#endif
