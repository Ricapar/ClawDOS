/* The model's streamed reply, one SSE event at a time: visible text goes to the
 * screen (as CP437), every content block goes to the history exactly as the API
 * sent it (thinking blocks included: the next request must echo them), and a
 * tool_use block is collected for the tool loop.
 */
#ifndef CLAWDOS_APISTREAM_H
#define CLAWDOS_APISTREAM_H

#ifdef __cplusplus
extern "C" {
#endif

enum { THINK_START, THINK_TEXT, THINK_END };

typedef struct ApiStream {
    /* callbacks */
    void (*text)(void *ctx, const char *cp437, unsigned n);  /* visible reply text */
    /* A thinking block: THINK_START when one begins, THINK_TEXT for each
       non-empty piece of its text as CP437 (the model's progress notes when
       the request asks for display "updates"; otherwise the text is empty
       and nothing comes), THINK_END when it ends. */
    void (*thinking)(void *ctx, int phase, const char *cp437, unsigned n);
    int  (*hist)(void *ctx, const char *p, unsigned n);      /* bytes of the assistant message; 0 ok */
    void *ctx;
    /* results */
    char stopReason[16];       /* end_turn, tool_use, max_tokens, refusal, ... */
    char model[48];
    long inTok, outTok, cacheRead, cacheWrite;
    int  hasTool;
    char toolId[64], toolName[24];
    char *toolInput;           /* the tool_use input object, JSON text */
    unsigned toolInputLen, toolInputMax;
    int  toolInputOverflow;    /* the input did not fit toolInputMax */
    char error[160];           /* why apistream_event returned -1 */
    char errorType[24];        /* an error event's error.type: overloaded_error, api_error, ... */
    /* private */
    int blocks, inBlock, blockType, sigOpen, started;
    int bigKind;               /* the long event being streamed (apistream_big) */
    int bigEsc, bigDone;       /* inside an escape; its string has ended */
} ApiStream;

void apistream_init(ApiStream *a, char *toolInputBuf, unsigned max);
/* One SSE event. 0 continue, 1 message_stop seen, -1 error (a->error says why). */
int apistream_event(ApiStream *a, const char *event, const char *data, unsigned n);
/* An event too long for the SSE buffer, in pieces (phase as in sse.h's
   SseBigFn). Only the kinds that are one long string are taken: a thinking
   block's signature_delta and a redacted_thinking block's data (both grow with
   how long the model thought, past any buffer). Their string goes straight to
   the history. 0 taken, 1 not a kind it can take (the caller reports the event
   as too long), -1 error (a->error says why). */
int apistream_big(ApiStream *a, int phase, const char *event, const char *data, unsigned n);

#ifdef __cplusplus
}
#endif

#endif
