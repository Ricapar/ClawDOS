/* Stream handling, see apistream.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apistream.h"
#include "json.h"
#include "utf8.h"

enum { B_NONE, B_TEXT, B_THINKING, B_REDACTED, B_TOOL };

#define SCRATCH 1024
static char *raw8;             /* unescaped UTF-8 of a delta */
static char *cp;               /* its CP437 form */

static int emitHist(ApiStream *a, const char *p, unsigned n)
{
    return a->hist ? a->hist(a->ctx, p, n) : 0;
}

static int emitHistZ(ApiStream *a, const char *z)
{
    return emitHist(a, z, strlen(z));
}

/* A JSON string value at path, unescaped into dst (NUL-terminated). */
static void field(const char *data, unsigned n, const char *path, char *dst, unsigned max)
{
    const char *v;
    unsigned len;
    dst[0] = 0;
    if (json_find(data, n, path, &v, &len) == JSON_STRING) {
        len = json_unescape(v, len, dst, max - 1);
        dst[len] = 0;
    }
}

static long number(const char *data, unsigned n, const char *path, long def)
{
    const char *v;
    unsigned len;
    if (json_find(data, n, path, &v, &len) == JSON_NUMBER)
        return atol(v);
    return def;
}

static void fail(ApiStream *a, const char *what, const char *detail)
{
    sprintf(a->error, "%.60s%.90s", what, detail ? detail : "");
}

void apistream_init(ApiStream *a, char *toolInputBuf, unsigned max)
{
    memset(a, 0, sizeof *a);
    a->toolInput = toolInputBuf;
    a->toolInputMax = max;
    if (toolInputBuf && max)
        toolInputBuf[0] = 0;
    if (!raw8)
        raw8 = (char *)malloc(SCRATCH);
    if (!cp)
        cp = (char *)malloc(SCRATCH);
}

/* How many raw bytes the token at raw[i] spans: an escape (6 for \uXXXX, 12
   for a surrogate pair, 2 otherwise), a UTF-8 sequence (1-4) or one byte. */
static unsigned tokenLen(const char *raw, unsigned len, unsigned i)
{
    unsigned char c = (unsigned char)raw[i];
    if (c == '\\') {
        if (i + 1 < len && raw[i + 1] == 'u') {
            if (i + 5 < len && (raw[i + 2] == 'd' || raw[i + 2] == 'D')
                && strchr("89abAB", raw[i + 3]) && i + 7 < len && raw[i + 6] == '\\' && raw[i + 7] == 'u')
                return 12;                      /* high surrogate + its low half */
            return 6;
        }
        return 2;
    }
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

/* Where to cut a raw (escaped) string so that no token straddles the cut:
   the end of the last whole token at or before `want`. */
static unsigned safeCut(const char *raw, unsigned len, unsigned want)
{
    unsigned i = 0;
    if (len <= want)
        return len;
    for (;;) {
        unsigned n = tokenLen(raw, len, i);
        if (i + n > want)
            return i ? i : n;                   /* a single token longer than want: take it whole */
        i += n;
    }
}

/* A text delta: raw escaped JSON to the history, decoded CP437 to the screen. */
static int textDelta(ApiStream *a, const char *raw, unsigned len)
{
    if (emitHist(a, raw, len))
        return -1;
    if (!a->text || !raw8 || !cp)
        return 0;
    while (len) {
        unsigned take = safeCut(raw, len, SCRATCH / 2), n8, nc;
        n8 = json_unescape(raw, take, raw8, SCRATCH);
        nc = utf8_to_cp437(raw8, n8, cp, SCRATCH);
        a->text(a->ctx, cp, nc);
        raw += take;
        len -= take;
    }
    return 0;
}

/* A tool input delta: the unescaped JSON text to the input buffer and the history. */
static int inputDelta(ApiStream *a, const char *raw, unsigned len)
{
    while (len) {
        unsigned take = safeCut(raw, len, SCRATCH / 2), n8;
        n8 = json_unescape(raw, take, raw8, SCRATCH);
        if (emitHist(a, raw8, n8))
            return -1;
        if (a->toolInputLen + n8 < a->toolInputMax) {
            memcpy(a->toolInput + a->toolInputLen, raw8, n8);
            a->toolInputLen += n8;
            a->toolInput[a->toolInputLen] = 0;
        } else {
            a->toolInputOverflow = 1;
        }
        a->sigOpen = 1;                     /* some input was written */
        raw += take;
        len -= take;
    }
    return 0;
}

static int blockStart(ApiStream *a, const char *data, unsigned n)
{
    char type[24];
    const char *v;
    unsigned len;

    field(data, n, "content_block.type", type, sizeof type);
    if (a->blocks && emitHistZ(a, ","))
        return -1;
    a->blocks++;
    a->inBlock = 1;
    a->sigOpen = 0;
    if (!strcmp(type, "text")) {
        a->blockType = B_TEXT;
        return emitHistZ(a, "{\"type\":\"text\",\"text\":\"");
    }
    if (!strcmp(type, "thinking")) {
        a->blockType = B_THINKING;
        return emitHistZ(a, "{\"type\":\"thinking\",\"thinking\":\"");
    }
    if (!strcmp(type, "redacted_thinking")) {
        a->blockType = B_REDACTED;
        if (emitHistZ(a, "{\"type\":\"redacted_thinking\",\"data\":\""))
            return -1;
        if (json_find(data, n, "content_block.data", &v, &len) == JSON_STRING && emitHist(a, v, len))
            return -1;
        return emitHistZ(a, "\"}");
    }
    if (!strcmp(type, "tool_use")) {
        a->blockType = B_TOOL;
        a->hasTool = 1;
        a->toolInputLen = 0;
        if (a->toolInput)
            a->toolInput[0] = 0;
        field(data, n, "content_block.id", a->toolId, sizeof a->toolId);
        field(data, n, "content_block.name", a->toolName, sizeof a->toolName);
        if (emitHistZ(a, "{\"type\":\"tool_use\",\"id\":\"") || emitHistZ(a, a->toolId)
            || emitHistZ(a, "\",\"name\":\"") || emitHistZ(a, a->toolName) || emitHistZ(a, "\",\"input\":"))
            return -1;
        return 0;
    }
    fail(a, "unknown block type ", type);
    return -1;
}

static int blockDelta(ApiStream *a, const char *data, unsigned n)
{
    char type[24];
    const char *v;
    unsigned len;

    field(data, n, "delta.type", type, sizeof type);
    if (!strcmp(type, "text_delta")) {
        if (json_find(data, n, "delta.text", &v, &len) == JSON_STRING)
            return textDelta(a, v, len);
    } else if (!strcmp(type, "thinking_delta")) {
        if (a->thinking)
            a->thinking(a->ctx);
        if (json_find(data, n, "delta.thinking", &v, &len) == JSON_STRING)
            return emitHist(a, v, len);
    } else if (!strcmp(type, "signature_delta")) {
        if (json_find(data, n, "delta.signature", &v, &len) == JSON_STRING) {
            if (emitHistZ(a, "\",\"signature\":\"") || emitHist(a, v, len))
                return -1;
            a->sigOpen = 1;
        }
    } else if (!strcmp(type, "input_json_delta")) {
        if (json_find(data, n, "delta.partial_json", &v, &len) == JSON_STRING)
            return inputDelta(a, v, len);
    }
    return 0;
}

static int blockStop(ApiStream *a)
{
    int rc = 0;
    switch (a->blockType) {
    case B_TEXT:
        rc = emitHistZ(a, "\"}");
        break;
    case B_THINKING:
        rc = emitHistZ(a, a->sigOpen ? "\"}" : "\",\"signature\":\"\"}");
        break;
    case B_TOOL:
        rc = emitHistZ(a, a->sigOpen ? "}" : "{}}");
        break;
    default:
        break;
    }
    a->inBlock = 0;
    a->blockType = B_NONE;
    return rc;
}

int apistream_event(ApiStream *a, const char *event, const char *data, unsigned n)
{
    char type[24];

    if (!event || !event[0])
        field(data, n, "type", type, sizeof type);
    else
        strncpy(type, event, sizeof type - 1), type[sizeof type - 1] = 0;

    if (!strcmp(type, "message_start")) {
        a->started = 1;
        field(data, n, "message.model", a->model, sizeof a->model);
        a->inTok = number(data, n, "message.usage.input_tokens", 0);
        a->cacheRead = number(data, n, "message.usage.cache_read_input_tokens", 0);
        a->cacheWrite = number(data, n, "message.usage.cache_creation_input_tokens", 0);
        return emitHistZ(a, "{\"role\":\"assistant\",\"content\":[");
    }
    if (!strcmp(type, "content_block_start"))
        return blockStart(a, data, n);
    if (!strcmp(type, "content_block_delta"))
        return blockDelta(a, data, n);
    if (!strcmp(type, "content_block_stop"))
        return blockStop(a);
    if (!strcmp(type, "message_delta")) {
        field(data, n, "delta.stop_reason", a->stopReason, sizeof a->stopReason);
        a->outTok = number(data, n, "usage.output_tokens", a->outTok);
        return 0;
    }
    if (!strcmp(type, "message_stop")) {
        if (a->inBlock && blockStop(a))
            return -1;
        return emitHistZ(a, "]}") ? -1 : 1;
    }
    if (!strcmp(type, "error")) {
        field(data, n, "error.message", a->error, sizeof a->error);
        if (!a->error[0])
            strcpy(a->error, "API error");
        return -1;
    }
    return 0;                                   /* ping and anything new */
}
