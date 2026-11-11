/* Messages API request writer, see apireq.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apireq.h"
#include "json.h"
#include "history.h"

#define PIECE 512

/* Copy a file into the sink as is. 0 ok, -1 unreadable, -2 sink failed. */
static int copyFile(Sink *s, const char *path)
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
    while ((n = fread(buf, 1, PIECE, f)) > 0)
        if (sink_put(s, buf, (unsigned)n)) {
            rc = -2;
            break;
        }
    free(buf);
    fclose(f);
    return rc;
}

/* The prompt file, JSON-escaped, with CRLF -> LF and {name}/{memories} filled in.
   A '{' starts a possible placeholder; up to 12 bytes are held back until the
   '}' decides. 0 ok, -1 unreadable, -2 sink failed. */
static int promptText(Sink *s, const ApiReq *r)
{
    FILE *f = fopen(r->promptPath, "rb");
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
        int i;
        for (i = 0; i < n && rc == 0; i++) {
            char c = buf[i];
            if (c == '\r')
                continue;
            if (held) {
                hold[held++] = c;
                if (c == '}') {
                    hold[held] = 0;
                    if (!strcmp(hold, "{name}"))
                        rc = json_escape_cp437(s, r->name, strlen(r->name)) ? -2 : 0;
                    else if (!strcmp(hold, "{memories}")) {
                        const char *m = r->memories && r->memories[0] ? r->memories : "(none)";
                        rc = json_escape_cp437(s, m, strlen(m)) ? -2 : 0;
                    } else
                        rc = json_escape_cp437(s, hold, held) ? -2 : 0;
                    held = 0;
                } else if (held >= sizeof hold - 2) {
                    rc = json_escape_cp437(s, hold, held) ? -2 : 0;
                    held = 0;
                }
                continue;
            }
            if (c == '{') {
                hold[held++] = c;
                continue;
            }
            if (json_escape_cp437(s, &c, 1))
                rc = -2;
        }
    }
    if (rc == 0 && held)
        rc = json_escape_cp437(s, hold, held) ? -2 : 0;
    free(buf);
    fclose(f);
    return rc;
}

#define PUT(z) do { if (sink_puts(s, (z))) return -2; } while (0)

int api_body(Sink *s, const ApiReq *r)
{
    const Provider *p = r->p;
    char num[16];
    int rc;

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
    PUT("\"system\":[{\"type\":\"text\",\"text\":\"");
    if ((rc = promptText(s, r)) != 0)
        return rc;
    PUT("\"");
    if (p->cache)
        PUT(",\"cache_control\":{\"type\":\"ephemeral\"}");
    PUT("}],\"tools\":");
    if ((rc = copyFile(s, r->toolsPath)) != 0)
        return rc;
    PUT(",\"messages\":[");
    if (hist_copy(s))
        return -2;
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
