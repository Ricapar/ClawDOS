/* Messages API request writer, see apireq.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apireq.h"
#include "json.h"
#include "history.h"

#define PIECE 2048              /* file reads (heap, freed after use) */
#define MESSAGES "\"messages\":["

/* The anthropic-beta values the fixed settings need (docs/API.md). */
#define BETA_PROGRESS "thinking-display-updates-2026-08-18"
#define BETA_BINDING  "thinking-binding-controls-2026-08-01"
#define BETA_THREADS  "message-threads-2026-08-12"

void api_fixed_from(ApiFixed *f, const Provider *p)
{
    f->progress = p->progress != 0;
    f->dropThinking = p->dropThinking != 0;
    f->cache = p->cache != 0;
    f->ttl1h = p->cacheTtl1h != 0;
    f->threads = p->threads != 0;
}

void api_req_settings(ApiReq *r, const Provider *p)
{
    r->p = *p;
}

/* A cache_control value; both markers of a request use this, so their ttl agrees. */
static const char *cacheMarker(int ttl1h)
{
    return ttl1h ? "{\"type\":\"ephemeral\",\"ttl\":\"1h\"}" : "{\"type\":\"ephemeral\"}";
}

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

/* The prompt file, JSON-escaped, with CRLF -> LF and {name}, {memories} and
   {skills} filled in. Runs of ordinary text go to the escaper whole; a '{'
   starts a possible placeholder, and up to 12 bytes are held back until the
   '}' decides.
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
                    else if (!strcmp(hold, "{memories}") || !strcmp(hold, "{skills}")) {
                        const char *m = hold[1] == 'm' ? x->memories : x->skills;
                        if (m == NULL || !m[0])
                            m = "(none)";
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
    if (x->cache) {
        PUT(",\"cache_control\":");
        PUT(cacheMarker(x->ttl1h));
    }
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

/* The prefix starts "system":[{"type":"text","text":"<prompt>" and the
   prompt's closing quote is the tenth one not escaped; what follows it up to
   "tools" holds the marker, if there is one. */
int api_prefix_cache(const char *path, int *cache, int *ttl1h)
{
    FILE *f = fopen(path, "rb");
    char after[72];
    unsigned n = 0;
    int c, quotes = 0, esc = 0;

    if (!f)
        return -1;
    while (quotes < 10 && (c = getc(f)) != EOF) {
        if (esc)
            esc = 0;
        else if (c == '\\')
            esc = 1;
        else if (c == '"')
            quotes++;
    }
    while (quotes == 10 && n < sizeof after - 1 && (c = getc(f)) != EOF)
        after[n++] = (char)c;
    after[n] = 0;
    fclose(f);
    if (strstr(after, "\"tools\":") == NULL)
        return -1;
    *strstr(after, "\"tools\":") = 0;
    *cache = strstr(after, "\"cache_control\":") != NULL;
    *ttl1h = *cache && strstr(after, "\"ttl\":\"1h\"") != NULL;
    return 0;
}

/* The prefix file again with only its system block's marker changed: the
   bytes up to the prompt's closing quote, the new marker, then everything
   from the "}],\"tools\":" after it. Written beside it first, then renamed.
   Its size; -1 unreadable or not a prefix, -2 the file could not be written. */
static long recache(const char *path, int cache, int ttl1h)
{
    static const char tools[] = "}],\"tools\":";
    char tmp[80], after[72], *buf, *t;
    FILE *in, *out;
    unsigned n = strlen(path);
    int c, quotes = 0, esc = 0, rc = 0;
    long size = 0;

    if (n == 0 || n >= sizeof tmp)
        return -1;
    strcpy(tmp, path);
    tmp[n - 1] = '$';
    if ((in = fopen(path, "rb")) == NULL)
        return -1;
    if ((buf = (char *)malloc(PIECE)) == NULL || (out = fopen(tmp, "wb")) == NULL) {
        free(buf);
        fclose(in);
        return -2;
    }
    while (quotes < 10 && (c = getc(in)) != EOF) {
        if (esc)
            esc = 0;
        else if (c == '\\')
            esc = 1;
        else if (c == '"')
            quotes++;
        putc(c, out);
        size++;
    }
    n = quotes == 10 ? fread(after, 1, sizeof after - 1, in) : 0;
    after[n] = 0;
    if ((t = strstr(after, tools)) == NULL)
        rc = -1;
    else {
        unsigned k = (unsigned)(after + n - t);
        if (cache) {
            fputs(",\"cache_control\":", out);
            fputs(cacheMarker(ttl1h), out);
            size += 17 + strlen(cacheMarker(ttl1h));
        }
        size += fwrite(t, 1, k, out);
        while ((k = fread(buf, 1, PIECE, in)) > 0)
            size += fwrite(buf, 1, k, out);
    }
    free(buf);
    if (ferror(in))
        rc = -1;
    fclose(in);
    if ((ferror(out) | fclose(out)) && rc == 0)
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
    return size;
}

int api_fixed_switch(ApiFixed *f, const Provider *p, const char *prefixPath)
{
    int cache, ttl1h;

    api_fixed_from(f, p);
    if (api_prefix_cache(prefixPath, &cache, &ttl1h)) {
        remove(prefixPath);               /* none, or unreadable: rendered with f at the next request */
        return 0;
    }
    if (cache == f->cache && (!cache || ttl1h == f->ttl1h))
        return 0;
    if (recache(prefixPath, f->cache, f->ttl1h) >= 0)
        return 1;
    f->cache = cache;
    f->ttl1h = ttl1h;
    return -1;
}

/* The fields before the prefix, each followed by a comma. thinking and the
   top-level cache_control come from the conversation's fixed settings, the
   rest from the provider's settings the request took (api_req_settings). */
static int fields(Sink *s, const ApiReq *r)
{
    const Provider *p = &r->p;
    const ApiFixed *f = r->fixed;
    char num[16];

    PUT("{\"model\":\"");
    PUT(p->model);
    sprintf(num, "%ld", p->maxTokens);
    PUT("\",\"max_tokens\":");
    PUT(num);
    PUT(",\"stream\":true,");
    if (f->progress || f->dropThinking) {
        PUT("\"thinking\":{\"type\":\"adaptive\"");
        if (f->progress)
            PUT(",\"display\":\"updates\"");
        if (f->dropThinking)
            PUT(",\"block_binding\":{\"prefix_mismatch_behavior\":\"drop_block\"}");
        PUT("},");
    }
    if (p->effort[0]) {
        PUT("\"output_config\":{\"effort\":\"");
        PUT(p->effort);
        PUT("\"},");
    }
    if (f->cache) {
        PUT("\"cache_control\":");
        PUT(cacheMarker(f->ttl1h));
        PUT(",");
    }
    if (r->noTools)
        PUT("\"tool_choice\":{\"type\":\"none\"},");
    else
        PUT("\"tool_choice\":{\"type\":\"auto\",\"disable_parallel_tool_use\":true},");
    if (r->thread == API_TH_CREATE)
        PUT("\"thread\":{\"type\":\"create\"},");
    else if (r->thread == API_TH_CONTINUE) {
        PUT("\"thread\":{\"type\":\"continue\",\"previous_message_id\":\"");
        PUT(r->prevId);                   /* an id of letters, digits and _ (chat.cpp checks) */
        PUT("\"},");
    }
    return 0;
}

unsigned long api_length(const ApiReq *r)
{
    Sink count;

    sink_count(&count);
    fields(&count, r);                    /* a few short strings in memory */
    if (r->thread == API_TH_CONTINUE)     /* no prefix, the history from histFrom */
        return count.count + (sizeof MESSAGES - 1) + (unsigned long)(hist_size() - r->histFrom) + 2;
    return count.count + (unsigned long)r->prefixLen + 1 + (sizeof MESSAGES - 1)
           + (unsigned long)hist_size() + 2;
}

int api_body(Sink *s, const ApiReq *r)
{
    int rc;

    if ((rc = fields(s, r)) != 0)
        return rc;
    if (r->thread != API_TH_CONTINUE) {   /* a continue request has no system or tools */
        if ((rc = copyFile(s, r->prefixPath, r->prefixLen)) != 0)
            return rc;
        PUT(",");
    }
    PUT(MESSAGES);
    if ((rc = hist_copy_from(s, r->thread == API_TH_CONTINUE ? r->histFrom : 0)) != 0)
        return rc;                        /* -1: the history could not be read */
    PUT("]}");
    return 0;
}

/* --- message threads ---------------------------------------------------------- */

unsigned long api_fingerprint(const Provider *p, int noTools)
{
    const char *parts[4];
    unsigned long h = 2166136261UL;       /* FNV-1a */
    int i;

    parts[0] = p->id;
    parts[1] = p->betas;
    parts[2] = p->model;
    parts[3] = p->effort;

    for (i = 0; i < 4; i++) {
        const char *c;
        for (c = parts[i]; ; c++) {
            h = (h ^ (unsigned char)*c) * 16777619UL;
            if (!*c)
                break;
        }
    }
    return h ^ (unsigned long)noTools;
}

int api_thread_kind(ApiThread *t, const ApiFixed *f, unsigned long fp)
{
    if (!f->threads)
        return API_TH_NONE;
    if (t->id[0] && t->fp == fp && hist_low() >= t->at && hist_size() > t->at + HIST_SEP_LEN)
        return API_TH_CONTINUE;
    t->id[0] = 0;
    return API_TH_CREATE;
}

/* An id to send back: letters, digits, _ and -, and not cut short. */
static int idOk(const char *id)
{
    unsigned n = 0;
    for (; id[n]; n++) {
        char c = id[n];
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_' && c != '-')
            return 0;
    }
    return n > 0 && n < sizeof ((ApiThread *)0)->id - 1;
}

void api_thread_keep(ApiThread *t, int sent, const char *id, const char *stopReason, unsigned long fp)
{
    t->id[0] = 0;
    if (sent == API_TH_NONE || !idOk(id) || (strcmp(stopReason, "end_turn") && strcmp(stopReason, "tool_use")))
        return;
    strcpy(t->id, id);
    t->at = hist_size();
    t->fp = fp;
    hist_low_reset();
}

int api_thread_set(ApiThread *t, const ApiThread *from)
{
    if (!idOk(from->id) || from->at <= 0 || from->at > hist_size())
        return -1;
    *t = *from;
    hist_low_reset();                     /* hist_open has, too: the file is as the run left it */
    return 0;
}

long api_thread_from(const ApiThread *t)
{
    return t->at + HIST_SEP_LEN;
}

/* Whether the comma-separated list holds the value v (blanks around it allowed). */
static int listHas(const char *list, const char *v)
{
    unsigned n = strlen(v);
    while (*list) {
        const char *end;
        while (*list == ' ' || *list == ',')
            list++;
        end = list;
        while (*end && *end != ',')
            end++;
        while (end > list && end[-1] == ' ')
            end--;
        if ((unsigned)(end - list) == n && !memcmp(list, v, n))
            return 1;
        while (*end && *end != ',')
            end++;
        list = end;
    }
    return 0;
}

/* "anthropic-beta: ...\r\n": the provider's line, then each value the fixed
   settings need that it lacks; nothing when there is neither. */
static int betaHeader(Sink *s, const Provider *p, const ApiFixed *f)
{
    const char *need[3];
    int n = 0, i, any = p->betas[0] != 0;

    if (f->progress && !listHas(p->betas, BETA_PROGRESS))
        need[n++] = BETA_PROGRESS;
    if (f->dropThinking && !listHas(p->betas, BETA_BINDING))
        need[n++] = BETA_BINDING;
    if (f->threads && !listHas(p->betas, BETA_THREADS))
        need[n++] = BETA_THREADS;
    if (!any && !n)
        return 0;
    PUT("anthropic-beta: ");
    PUT(p->betas);
    for (i = 0; i < n; i++) {
        if (any || i)
            PUT(",");
        PUT(need[i]);
    }
    PUT("\r\n");
    return 0;
}

int api_head(Sink *s, const Provider *p, const ApiFixed *f, unsigned long length)
{
    char num[16];

    PUT("POST ");
    PUT(p->path);
    PUT(" HTTP/1.1\r\nHost: ");
    PUT(p->host);
    PUT("\r\n");
    if (p->key[0] && stricmp(p->auth, "tunnel")) {   /* no key: no header (the API says 401); auth tunnel: the tunnel adds it */
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
    if (betaHeader(s, p, f))
        return -2;
    PUT("Content-Type: application/json\r\nAccept: text/event-stream\r\n"
        "Connection: keep-alive\r\nContent-Length: ");
    sprintf(num, "%lu", length);
    PUT(num);
    PUT("\r\n\r\n");
    return 0;
}
