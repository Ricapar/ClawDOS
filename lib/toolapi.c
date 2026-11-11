/* ClawDOS tool calls: see toolapi.h. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <bios.h>
#include "toolapi.h"
#include "utf8.h"

/* "key\0value\0key\0value\0...", in a far buffer (16 KB is too much for DGROUP) */
static char *store;
static unsigned used;
static int lastKey = -1;            /* offset of the newest key */

static char outBuf[TOOL_OUT_MAX + 1];

void targ_reset(void)
{
    if (store == NULL)
        store = (char *)malloc(TOOL_ARGS_MAX);
    used = 0;
    lastKey = -1;
}

int targ_set(const char *key, const char *utf8, unsigned n)
{
    unsigned klen = strlen(key) + 1, room, got;

    if (store == NULL)
        targ_reset();
    if (store == NULL || used + klen + 1 > TOOL_ARGS_MAX)
        return -1;
    memcpy(store + used, key, klen);
    room = TOOL_ARGS_MAX - (used + klen) - 1;
    got = utf8_to_cp437_tabs(utf8, n, store + used + klen, room);
    if (got >= room) {                 /* did not fit: forget this key */
        store[used] = 0;
        return -1;
    }
    lastKey = used;
    used += klen + got;
    store[used++] = 0;
    return 0;
}

const char *targ(const char *key)
{
    unsigned i = 0;
    while (i < used) {
        const char *k = store + i;
        const char *v = k + strlen(k) + 1;
        if (!strcmp(k, key))
            return v;
        i = (unsigned)(v - store) + strlen(v) + 1;
    }
    return NULL;
}

int targ_flag(const char *key)
{
    const char *v = targ(key);

    if (v == NULL || !*v)
        return 0;
    if (!strcmp(v, "1") || !stricmp(v, "true"))
        return 1;
    if (!strcmp(v, "0") || !stricmp(v, "false"))
        return 0;
    return -1;
}

int targ_bool(ToolCtx *t, const char *key, int *on)
{
    char msg[48];

    if ((*on = targ_flag(key)) >= 0)
        return 0;
    sprintf(msg, "%.20s must be true or false", key);
    tool_fail(t, msg);
    return -1;
}

int targ_long(ToolCtx *t, const char *key, long def, long *n)
{
    const char *v = targ(key);
    char *end, msg[48];

    *n = def;
    if (v == NULL || !*v)
        return 0;
    /* decimal, or 0x hex; never octal, so "010" is ten */
    *n = strtol(v, &end, v[0] == '0' && (v[1] == 'x' || v[1] == 'X') ? 16 : 10);
    if (end != v && *end == 0)
        return 0;
    *n = def;
    sprintf(msg, "%.20s must be a whole number", key);
    tool_fail(t, msg);
    return -1;
}

static int parse_hex(const char *s, unsigned *out, const char **end)
{
    unsigned long n = 0;
    const char *p = s;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    if (!isxdigit((unsigned char)*p))
        return -1;
    while (isxdigit((unsigned char)*p)) {
        int c = toupper((unsigned char)*p++);
        n = n * 16 + (c <= '9' ? c - '0' : c - 'A' + 10);
        if (n > 0xFFFFul)
            return -1;
    }
    if (*p == 'h' || *p == 'H')
        p++;
    *out = (unsigned)n;
    *end = p;
    return 0;
}

int targ_hex(const char *key, unsigned *out)
{
    const char *v = targ(key), *end;
    if (v == NULL || parse_hex(v, out, &end) || *end)
        return -1;
    return 0;
}

int targ_addr(const char *key, unsigned *seg, unsigned *off)
{
    const char *v = targ(key), *end;
    if (v == NULL || parse_hex(v, seg, &end) || *end != ':' || parse_hex(end + 1, off, &end) || *end)
        return -1;
    return 0;
}

void tout_reset(ToolCtx *t)
{
    t->out = outBuf;
    t->len = 0;
    t->truncated = 0;
    t->summary[0] = 0;
    t->showFrom = t->showTo = 0;
    outBuf[0] = 0;
}

void tout_puts(ToolCtx *t, const char *s)
{
    unsigned n = strlen(s);
    if (t->len + n > TOOL_OUT_MAX) {
        n = TOOL_OUT_MAX - t->len;
        t->truncated = 1;
    }
    memcpy(t->out + t->len, s, n);
    t->len += n;
    t->out[t->len] = 0;
}

void tout_end_list(ToolCtx *t, const char *more)
{
    unsigned need = strlen(more) + 1;

    if (!t->truncated)
        return;
    while (t->len > 0 && (t->out[t->len - 1] != '\n' || t->len + need > TOOL_OUT_MAX))
        t->len--;
    memcpy(t->out + t->len, more, need - 1);
    t->len += need;
    t->out[t->len - 1] = '\n';
    t->out[t->len] = 0;
}

void tout_printf(ToolCtx *t, const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    tout_puts(t, buf);
}

int tout_room(ToolCtx *t, unsigned n)
{
    if (!t->truncated && t->len + n <= TOOL_OUT_MAX - TOUT_RESERVE)
        return 1;
    t->truncated = 1;
    return 0;
}

int tout_line(ToolCtx *t, const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    if (!tout_room(t, strlen(buf)))
        return -1;
    tout_puts(t, buf);
    return 0;
}

void tout_hexdump(ToolCtx *t, const unsigned char __far *p, unsigned n,
                  unsigned seg, unsigned long off)
{
    char line[90];
    unsigned i, j;

    for (i = 0; i < n; i += 16) {
        int col;
        if (seg == 0xFFFF)
            col = sprintf(line, "%08lX ", off + i);
        else
            col = sprintf(line, "%04X:%04X ", seg, (unsigned)(off + i));
        for (j = 0; j < 16; j++) {
            if (i + j < n)
                col += sprintf(line + col, " %02X", p[i + j]);
            else
                col += sprintf(line + col, "   ");
        }
        col += sprintf(line + col, "  ");
        for (j = 0; j < 16 && i + j < n; j++) {
            unsigned char c = p[i + j];
            line[col++] = c >= 32 && c < 127 ? c : '.';
        }
        line[col++] = '\n';
        line[col] = 0;
        tout_puts(t, line);
    }
}

int tool_fail(ToolCtx *t, const char *msg)
{
    tout_puts(t, msg);
    strncpy(t->summary, msg, sizeof(t->summary) - 1);
    t->summary[sizeof(t->summary) - 1] = 0;
    return TS_ERROR;
}

void (*tool_idle)(void);

int tool_esc(void)
{
    int esc = 0;

    if (tool_idle)
        tool_idle();
    while (_bios_keybrd(_KEYBRD_READY)) {
        if ((_bios_keybrd(_KEYBRD_READ) & 0xFF) == 27)
            esc = 1;
    }
    return esc;
}

const char *dos_error(int code)
{
    switch (code) {
    case 2:  return "file not found";
    case 3:  return "path not found";
    case 4:  return "too many open files";
    case 5:  return "access denied";
    case 6:  return "invalid handle";
    case 8:  return "out of memory";
    case 15: return "invalid drive";
    case 16: return "can't remove the current directory";
    case 17: return "not the same drive";
    case 18: return "no matching files";
    case 19: return "disk is write-protected";
    case 21: return "drive not ready";
    case 29: return "write fault";
    case 30: return "read fault";
    case 32: return "sharing violation";
    case 80: return "file exists";
    case 82: return "cannot make directory";
    case 83: return "drive not ready or disk error";
    default: return "DOS error";
    }
}
