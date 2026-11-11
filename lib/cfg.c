/* CLAWDOS.CFG parsing, see cfg.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg.h"

void cfg_defaults(Cfg *c)
{
    memset(c, 0, sizeof *c);
    strcpy(c->name, "Claude");
}

static void providerDefaults(Provider *p, const char *id)
{
    memset(p, 0, sizeof *p);
    strncpy(p->id, id, sizeof p->id - 1);
    strcpy(p->path, "/v1/messages");
    strcpy(p->auth, "x-api-key");
    p->cache = 1;
    p->maxTokens = CFG_MAX_TOKENS;
}

static void setStr(char *dst, unsigned max, const char *val)
{
    strncpy(dst, val, max - 1);
    dst[max - 1] = 0;
}

/* Store key=val in the section (or the globals when p is NULL); 0 ok, -1 unknown key. */
static int setValue(Cfg *c, Provider *p, const char *key, const char *val)
{
    if (p == NULL) {
        if (!stricmp(key, "provider")) setStr(c->provider, sizeof c->provider, val);
        else if (!stricmp(key, "theme") || !stricmp(key, "timezone") || !stricmp(key, "clock"))
            ;                         /* gone: older CFGs still load */
        else if (!stricmp(key, "name")) setStr(c->name, sizeof c->name, val);
        else return -1;
        return 0;
    }
    if (!stricmp(key, "type")) setStr(p->type, sizeof p->type, val);
    else if (!stricmp(key, "tunnel")) setStr(p->tunnel, sizeof p->tunnel, val);
    else if (!stricmp(key, "host")) setStr(p->host, sizeof p->host, val);
    else if (!stricmp(key, "path")) setStr(p->path, sizeof p->path, val);
    else if (!stricmp(key, "auth")) setStr(p->auth, sizeof p->auth, val);
    else if (!stricmp(key, "key")) setStr(p->key, sizeof p->key, val);
    else if (!stricmp(key, "keyfile")) setStr(p->keyfile, sizeof p->keyfile, val);
    else if (!stricmp(key, "model")) setStr(p->model, sizeof p->model, val);
    else if (!stricmp(key, "effort")) setStr(p->effort, sizeof p->effort, val);
    else if (!stricmp(key, "betas")) setStr(p->betas, sizeof p->betas, val);
    else if (!stricmp(key, "name")) setStr(p->name, sizeof p->name, val);
    else if (!stricmp(key, "cache")) p->cache = atoi(val);
    else if (!stricmp(key, "progress")) p->progress = atoi(val);
    else if (!stricmp(key, "dropthinking")) p->dropThinking = atoi(val);
    else if (!stricmp(key, "threads")) p->threads = atoi(val);
    else if (!stricmp(key, "cachettl")) {
        if (!stricmp(val, "5m"))
            p->cacheTtl1h = 0;
        else if (!stricmp(val, "1h"))
            p->cacheTtl1h = 1;
        else
            return -1;                /* the API knows no other lifetime */
    }
    else if (!stricmp(key, "maxtokens")) {
        char *end;
        long n = strtol(val, &end, 10);
        if (end == val || *end || n < 1)
            return -1;                /* reported as a bad line */
        p->maxTokens = n;
    }
    else return -1;
    return 0;
}

int cfg_parse(Cfg *c, const char *text)
{
    Provider *p = NULL;
    int lineNo = 0;
    char line[160];

    while (*text) {
        const char *e = strchr(text, '\n');
        unsigned n = e ? (unsigned)(e - text) : strlen(text);
        char *key, *val, *cut;

        lineNo++;
        if (n >= sizeof line)
            n = sizeof line - 1;
        memcpy(line, text, n);
        line[n] = 0;
        text = e ? e + 1 : text + n;

        key = line;
        while (*key == ' ' || *key == '\t')
            key++;
        if (*key == 0 || *key == '#' || *key == '\r')
            continue;
        if (*key == '[') {
            cut = strchr(key, ']');
            if (!cut || c->nproviders >= CFG_PROVIDERS)
                return lineNo;
            *cut = 0;
            p = &c->providers[c->nproviders++];
            providerDefaults(p, key + 1);
            continue;
        }
        val = key;
        while (*val && *val != ' ' && *val != '\t')
            val++;
        if (*val)
            *val++ = 0;
        while (*val == ' ' || *val == '\t')
            val++;
        if (stricmp(key, "key") && stricmp(key, "keyfile") && (cut = strchr(val, '#')) != NULL)
            *cut = 0;
        cut = val + strlen(val);
        while (cut > val && (cut[-1] == ' ' || cut[-1] == '\t' || cut[-1] == '\r'))
            *--cut = 0;
        if (setValue(c, p, key, val))
            return lineNo;
    }
    return 0;
}

int cfg_load(Cfg *c, const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long size;
    int rc, i;

    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 16000L) {
        fclose(f);
        return -1;
    }
    text = (char *)malloc((unsigned)size + 1);
    if (!text) {
        fclose(f);
        return -1;
    }
    size = fread(text, 1, (unsigned)size, f);
    fclose(f);
    text[size] = 0;
    text[strcspn(text, "\x1A")] = 0;  /* EDLIN and COPY CON end a file with Ctrl-Z */
    rc = cfg_parse(c, text);
    free(text);
    for (i = 0; i < c->nproviders; i++) {
        Provider *p = &c->providers[i];
        if (p->key[0] == 0 && p->keyfile[0]) {
            f = fopen(p->keyfile, "rb");
            if (f) {
                unsigned n = fread(p->key, 1, sizeof p->key - 1, f);
                fclose(f);
                p->key[n] = 0;
                p->key[strcspn(p->key, "\r\n ")] = 0;
            }
        }
    }
    return rc;
}

Provider *cfg_provider(Cfg *c, const char *id)
{
    int i;
    for (i = 0; i < c->nproviders; i++)
        if (!stricmp(c->providers[i].id, id))
            return &c->providers[i];
    return NULL;
}

const char *cfg_missing(const Provider *p)
{
    if (!p->tunnel[0]) return "tunnel";
    if (!p->host[0]) return "host";
    if (!p->key[0]) return "key";
    if (!p->model[0]) return "model";
    return NULL;
}
