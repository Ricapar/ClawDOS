/* JSON toolkit, see json.h. Everything is a single forward pass over the text
 * with a cursor; nothing is copied or allocated.
 */
#include <string.h>
#include "json.h"
#include "utf8.h"

struct Cur { const char *p, *end; };

static void skipWs(struct Cur *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n'))
        c->p++;
}

/* Past the closing quote of the string starting at the opening quote. -1 if unterminated. */
static int skipString(struct Cur *c)
{
    c->p++;                                   /* opening quote */
    while (c->p < c->end) {
        if (*c->p == '\\') {
            c->p += 2;
            continue;
        }
        if (*c->p == '"') {
            c->p++;
            return 0;
        }
        c->p++;
    }
    return -1;
}

/* Skip one value at the cursor; reports its type and raw extent (for strings:
   the body inside the quotes). JSON_NONE if malformed or truncated. */
static int value(struct Cur *c, const char **val, unsigned *len)
{
    const char *start;
    int type;

    skipWs(c);
    if (c->p >= c->end)
        return JSON_NONE;
    start = c->p;
    switch (*c->p) {
    case '"':
        if (skipString(c))
            return JSON_NONE;
        *val = start + 1;
        *len = (unsigned)(c->p - start - 2);
        return JSON_STRING;
    case '{':
    case '[': {
        int depth = 0;
        type = *c->p == '{' ? JSON_OBJECT : JSON_ARRAY;
        while (c->p < c->end) {
            char ch = *c->p;
            if (ch == '"') {
                if (skipString(c))
                    return JSON_NONE;
                continue;
            }
            if (ch == '{' || ch == '[')
                depth++;
            else if (ch == '}' || ch == ']')
                depth--;
            c->p++;
            if (depth == 0) {
                *val = start;
                *len = (unsigned)(c->p - start);
                return type;
            }
        }
        return JSON_NONE;
    }
    default:
        if (!strncmp(c->p, "true", 4) && c->p + 4 <= c->end) { type = JSON_TRUE; c->p += 4; }
        else if (!strncmp(c->p, "false", 5) && c->p + 5 <= c->end) { type = JSON_FALSE; c->p += 5; }
        else if (!strncmp(c->p, "null", 4) && c->p + 4 <= c->end) { type = JSON_NULL; c->p += 4; }
        else {
            type = JSON_NUMBER;
            while (c->p < c->end && strchr("+-0123456789.eE", *c->p))
                c->p++;
            if (c->p == start)
                return JSON_NONE;
        }
        *val = start;
        *len = (unsigned)(c->p - start);
        return type;
    }
}

/* Members of the object at the cursor: calls fn for each until it returns
   non-zero. Returns that, 0 at the closing brace, -1 if malformed. */
static int members(struct Cur *c, JsonEachFn fn, void *ctx)
{
    skipWs(c);
    if (c->p >= c->end || *c->p != '{')
        return -1;
    c->p++;
    for (;;) {
        const char *key, *val;
        unsigned klen, vlen;
        int type, rc;

        skipWs(c);
        if (c->p >= c->end)
            return -1;
        if (*c->p == '}')
            return 0;
        if (*c->p == ',') {
            c->p++;
            continue;
        }
        if (*c->p != '"')
            return -1;
        key = c->p + 1;
        if (skipString(c))
            return -1;
        klen = (unsigned)(c->p - key - 1);
        skipWs(c);
        if (c->p >= c->end || *c->p != ':')
            return -1;
        c->p++;
        type = value(c, &val, &vlen);
        if (type == JSON_NONE)
            return -1;
        if ((rc = fn(ctx, key, klen, val, vlen, type)) != 0)
            return rc;
    }
}

int json_each(const char *obj, unsigned len, JsonEachFn fn, void *ctx)
{
    struct Cur c;
    c.p = obj;
    c.end = obj + len;
    return members(&c, fn, ctx);
}

struct Find { const char *seg; unsigned seglen; const char *val; unsigned len; int type; };

static int findFn(void *ctx, const char *key, unsigned klen, const char *val, unsigned vlen, int type)
{
    struct Find *f = (struct Find *)ctx;
    if (klen == f->seglen && !memcmp(key, f->seg, klen)) {
        f->val = val;
        f->len = vlen;
        f->type = type;
        return 1;
    }
    return 0;
}

int json_find(const char *doc, unsigned doclen, const char *path, const char **val, unsigned *len)
{
    struct Find f;
    const char *dot;

    f.val = doc;
    f.len = doclen;
    f.type = JSON_OBJECT;
    while (*path) {
        dot = strchr(path, '.');
        f.seg = path;
        f.seglen = dot ? (unsigned)(dot - path) : strlen(path);
        if (f.type != JSON_OBJECT)
            return JSON_NONE;
        if (json_each(f.val, f.len, findFn, &f) != 1)
            return JSON_NONE;
        path = dot ? dot + 1 : path + f.seglen;
    }
    *val = f.val;
    *len = f.len;
    return f.type;
}

static int hexVal(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* \uXXXX at p (4 hex digits after p); -1 if not. */
static long hex4(const char *p, const char *end)
{
    long v = 0;
    int i, h;
    if (p + 4 > end)
        return -1;
    for (i = 0; i < 4; i++) {
        if ((h = hexVal(p[i])) < 0)
            return -1;
        v = (v << 4) | h;
    }
    return v;
}

static unsigned putCp(unsigned long cp, char *out, unsigned outmax, unsigned len)
{
    char tmp[4];
    unsigned n = 0, i;
    if (cp < 0x80) { tmp[n++] = (char)cp; }
    else if (cp < 0x800) { tmp[n++] = (char)(0xC0 | (cp >> 6)); tmp[n++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000UL) {
        tmp[n++] = (char)(0xE0 | (cp >> 12)); tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        tmp[n++] = (char)(0xF0 | (cp >> 18)); tmp[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[n++] = (char)(0x80 | (cp & 0x3F));
    }
    for (i = 0; i < n && len < outmax; i++)
        out[len++] = tmp[i];
    return len;
}

unsigned json_unescape(const char *raw, unsigned n, char *out, unsigned outmax)
{
    const char *end = raw + n;
    unsigned len = 0;

    while (raw < end && len < outmax) {
        char ch = *raw++;
        if (ch != '\\') {
            out[len++] = ch;
            continue;
        }
        if (raw >= end)
            break;
        ch = *raw++;
        switch (ch) {
        case 'n': out[len++] = '\n'; break;
        case 't': out[len++] = '\t'; break;
        case 'r': out[len++] = '\r'; break;
        case 'b': out[len++] = '\b'; break;
        case 'f': out[len++] = '\f'; break;
        case 'u': {
            long cp = hex4(raw, end);
            if (cp < 0) {
                out[len++] = '?';
                break;
            }
            raw += 4;
            if (cp >= 0xD800 && cp < 0xDC00) {         /* high surrogate: needs \uDC00-DFFF next */
                long lo = (raw + 6 <= end && raw[0] == '\\' && raw[1] == 'u') ? hex4(raw + 2, end) : -1;
                if (lo >= 0xDC00 && lo < 0xE000) {
                    raw += 6;
                    cp = 0x10000L + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                    out[len++] = '?';
                    break;
                }
            } else if (cp >= 0xDC00 && cp < 0xE000) {  /* stray low surrogate */
                out[len++] = '?';
                break;
            }
            len = putCp((unsigned long)cp, out, outmax, len);
            break;
        }
        default: out[len++] = ch; break;                /* \" \\ \/ and anything odd */
        }
    }
    return len;
}

int json_escape_cp437(Sink *s, const char *text, unsigned n)
{
    static const char hex[] = "0123456789abcdef";
    char buf[8];
    unsigned i, k;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            buf[0] = '\\'; buf[1] = (char)c; k = 2;
        } else if (c == '\n') {
            buf[0] = '\\'; buf[1] = 'n'; k = 2;
        } else if (c == '\r') {
            buf[0] = '\\'; buf[1] = 'r'; k = 2;
        } else if (c == '\t') {
            buf[0] = '\\'; buf[1] = 't'; k = 2;
        } else if (c == 0x7F) {
            buf[0] = '\\'; buf[1] = 'u'; buf[2] = '0'; buf[3] = '0'; buf[4] = '7'; buf[5] = 'f'; k = 6;
        } else {
            k = cp437_to_utf8(c, buf);     /* controls 0x01-0x1F become their glyphs */
            if (k == 1 && (unsigned char)buf[0] < 0x20) {   /* a control left as is (0x00) */
                buf[0] = '\\'; buf[1] = 'u'; buf[2] = '0'; buf[3] = '0';
                buf[4] = hex[c >> 4]; buf[5] = hex[c & 15]; k = 6;
            }
        }
        if (sink_put(s, buf, k))
            return -1;
    }
    return 0;
}
