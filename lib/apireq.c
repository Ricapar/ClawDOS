/* Messages API request writer, see apireq.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apireq.h"
#include "json.h"
#include "history.h"

#define PIECE 2048              /* file reads (heap, freed after use) */
#define MESSAGES "\"messages\":["

/* Copy a file into the sink as is: all of it when want < 0, else exactly want
   bytes. 0 ok, -1 unreadable (or shorter than want), -2 sink failed. */
static int copyFile(Sink *s, const char *path, long want)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    int n, rc = 0;

    if (!f)
        return -1;
    buf = (char *)malloc(PIECE);
    if (!buf) {
        fclose(f);
        return -1;
    }
    while (want != 0) {
        unsigned ask = want < 0 || want > PIECE ? PIECE : (unsigned)want;
        if ((n = fread(buf, 1, ask, f)) <= 0) {
            if (want > 0)
                rc = -1;
            break;
        }
        if (sink_put(s, buf, (unsigned)n)) {
            rc = -2;
            break;
        }
        if (want > 0)
            want -= n;
    }
    free(buf);
    fclose(f);
    return rc;
}

/* The prompt file, JSON-escaped, with CRLF -> LF and {name}/{memories} filled in.
   Runs of ordinary text go to the escaper whole; a '{' starts a possible
   placeholder, and up to 12 bytes are held back until the '}' decides.
   0 ok, -1 unreadable, -2 sink failed. */
static int promptText(Sink *s, const ApiPrefix *x)
{
    FILE *f = fopen(x->promptPath, "rb");
    char *buf, hold[14];
    unsigned held = 0;
    int n, rc = 0;

    if (!f)
        return -1;
    buf = (char *)malloc(PIECE);
    if (!buf) {
        fclose(f);
        return -1;
    }
    while (rc == 0 && (n = fread(buf, 1, PIECE, f)) > 0) {
        int i, from = 0;
        for (i = 0; i < n && rc == 0; i++) {
            char c = buf[i];
            if (held) {
                if (c == '\r') {
                    from = i + 1;
                    continue;
                }
                hold[held++] = c;
                if (c == '}') {
                    hold[held] = 0;
                    if (!strcmp(hold, "{name}"))
                        rc = json_escape_cp437(s, x->name, strlen(x->name)) ? -2 : 0;
                    else if (!strcmp(hold, "{memories}")) {
                        const char *m = x->memories && x->memories[0] ? x->memories : "(none)";
                        rc = json_escape_cp437(s, m, strlen(m)) ? -2 : 0;
                    } else
                        rc = json_escape_cp437(s, hold, held) ? -2 : 0;
                    held = 0;
                } else if (held >= sizeof hold - 2) {
                    rc = json_escape_cp437(s, hold, held) ? -2 : 0;
                    held = 0;
                }
                from = i + 1;
                continue;
            }
            if (c == '\r' || c == '{') {      /* the run so far, then this byte apart */
                if (i > from && json_escape_cp437(s, buf + from, i - from))
                    rc = -2;
                if (c == '{')
                    hold[held++] = c;
                from = i + 1;
            }
        }
        if (rc == 0 && !held && n > from && json_escape_cp437(s, buf + from, n - from))
            rc = -2;
    }
    if (rc == 0 && held)
        rc = json_escape_cp437(s, hold, held) ? -2 : 0;
    free(buf);
    fclose(f);
    return rc;
}

#define PUT(z) do { if (sink_puts(s, (z))) return -2; } while (0)

int api_prefix(Sink *s, const ApiPrefix *x)
{
    int rc;

    PUT("\"system\":[{\"type\":\"text\",\"text\":\"");
    if ((rc = promptText(s, x)) != 0)
        return rc;
    PUT("\"");
    if (x->cache)
        PUT(",\"cache_control\":{\"type\":\"ephemeral\"}");
    PUT("}],\"tools\":");
    return copyFile(s, x->toolsPath, -1L);
}

static int filePut(Sink *s, const char *p, unsigned n)
{
    return fwrite(p, 1, n, (FILE *)s->ctx) == n ? 0 : -1;
}

long api_prefix_file(const char *path, const ApiPrefix *x)
{
    char tmp[80];
    Sink s;
    FILE *f;
    int rc;
    unsigned n = strlen(path);

    if (n == 0 || n >= sizeof tmp)
        return -1;
    strcpy(tmp, path);
    tmp[n - 1] = '$';                     /* CHATS\MMDDnnn.PR$ */
    if ((f = fopen(tmp, "wb")) == NULL)
        return -2;
    s.put = filePut;
    s.ctx = f;
    s.count = 0;
    rc = api_prefix(&s, x);
    if (fclose(f))
        rc = -2;
    if (rc == 0) {
        remove(path);
        if (rename(tmp, path))
            rc = -2;
    }
    if (rc) {
        remove(tmp);
        return rc;
    }
    return (long)s.count;
}

/* The fields before the prefix, each followed by a comma. */
static int fields(Sink *s, const ApiReq *r)
{
    const Provider *p = r->p;
    char num[16];

    PUT("{\"model\":\"");
    PUT(p->model);
    sprintf(num, "%ld", p->maxTokens);
    PUT("\",\"max_tokens\":");
    PUT(num);
    PUT(",\"stream\":true,");
    if (p->effort[0]) {
        PUT("\"output_config\":{\"effort\":\"");
        PUT(p->effort);
        PUT("\"},");
    }
    if (p->cache)
        PUT("\"cache_control\":{\"type\":\"ephemeral\"},");
    if (r->noTools)
        PUT("\"tool_choice\":{\"type\":\"none\"},");
    else
        PUT("\"tool_choice\":{\"type\":\"auto\",\"disable_parallel_tool_use\":true},");
    return 0;
}

unsigned long api_length(const ApiReq *r)
{
    Sink count;

    sink_count(&count);
    fields(&count, r);                    /* a few short strings in memory */
    return count.count + (unsigned long)r->prefixLen + 1 + (sizeof MESSAGES - 1)
           + (unsigned long)hist_size() + 2;
}

int api_body(Sink *s, const ApiReq *r)
{
    int rc;

    if ((rc = fields(s, r)) != 0)
        return rc;
    if ((rc = copyFile(s, r->prefixPath, r->prefixLen)) != 0)
        return rc;
    PUT(",");
    PUT(MESSAGES);
    if ((rc = hist_copy(s)) != 0)
        return rc;                        /* -1: the history could not be read */
    PUT("]}");
    return 0;
}

int api_head(Sink *s, const Provider *p, unsigned long length)
{
    char num[16];

    PUT("POST ");
    PUT(p->path);
    PUT(" HTTP/1.1\r\nHost: ");
    PUT(p->host);
    PUT("\r\n");
    if (p->key[0]) {                       /* no key: no header (the API says 401) */
        if (!stricmp(p->auth, "bearer")) {
            PUT("Authorization: Bearer ");
            PUT(p->key);
        } else {
            PUT("x-api-key: ");
            PUT(p->key);
        }
        PUT("\r\n");
    }
    PUT("anthropic-version: 2023-06-01\r\n");
    if (p->betas[0]) {
        PUT("anthropic-beta: ");
        PUT(p->betas);
        PUT("\r\n");
    }
    PUT("Content-Type: application/json\r\nAccept: text/event-stream\r\n"
        "Connection: keep-alive\r\nContent-Length: ");
    sprintf(num, "%lu", length);
    PUT(num);
    PUT("\r\n\r\n");
    return 0;
}
