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

typedef struct ApiStream {
    /* callbacks */
    void (*text)(void *ctx, const char *cp437, unsigned n);  /* visible reply text */
    void (*thinking)(void *ctx);                             /* a thinking delta arrived */
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
    /* private */
    int blocks, inBlock, blockType, sigOpen, started;
} ApiStream;

void apistream_init(ApiStream *a, char *toolInputBuf, unsigned max);
/* One SSE event. 0 continue, 1 message_stop seen, -1 error (a->error says why). */
int apistream_event(ApiStream *a, const char *event, const char *data, unsigned n);

#ifdef __cplusplus
}
#endif

#endif
