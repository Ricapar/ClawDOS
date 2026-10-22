/* CLAUDE.EXE tool calls: see tools.h.
 *
 * The bridge streams TOOL <id> <name>, ARG <key> <chunk>... and CALL. On CALL
 * we show the call in the transcript, ask first if it's a W (write) tool and
 * safe mode is on, run its executor, and send the result back as OUT chunks
 * and DONE <id> <status>. Executors live in lib/ (toolapi.h); the small
 * memory, port and text art tools live here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>

#include "toolapi.h"
#include "dosfs.h"
#include "sysinfo.h"
#include "sound.h"
#include "gfx.h"
#include "screen.h"
#include "console.h"
#include "tools.h"
#include "ui.h"

#define OUT_CHUNK 400                 /* raw bytes per OUT line (escaping can double it) */
#define PEEK_MAX  256

struct ToolDef {
    const char *name;
    const char *title;                /* shown as Title(arg) */
    const char *headerKey;            /* argument shown in the header */
    char cls;                         /* R read-only, H harmless output, W writes */
    const char *busy;                 /* spinner verb */
    ToolFn fn;
};

static int safe = 1;
static int unattended;
static char callId[16];
static char callName[16];
static int argsOverflow;


/* --- the small tools ---------------------------------------------------------- */

static int toolSysinfo(ToolCtx *t)
{
    char ip[20];
    tool_sysinfo(t);
    ui_local_address(ip);
    tout_printf(t, "IP: %s\n", ip);
    return TS_OK;
}

static int toolPeek(ToolCtx *t)
{
    unsigned seg, off;
    long n = targ_num("len", 64);

    if (targ_addr("addr", &seg, &off))
        return tool_fail(t, "addr must be hex seg:off, e.g. 0040:0000");
    if (n < 1 || n > PEEK_MAX)
        n = n < 1 ? 64 : PEEK_MAX;
    tout_hexdump(t, (const unsigned char __far *)MK_FP(seg, off), (unsigned)n, seg, off);
    sprintf(t->summary, "%u bytes", (unsigned)n);
    return TS_OK;
}

static int toolPoke(ToolCtx *t)
{
    unsigned char bytes[PEEK_MAX];
    unsigned seg, off, n = 0;
    const char *p = targ("bytes");
    unsigned char __far *dst;

    if (targ_addr("addr", &seg, &off))
        return tool_fail(t, "addr must be hex seg:off, e.g. B800:0000");
    if (p == NULL)
        return tool_fail(t, "missing bytes");
    while (*p) {
        char *end;
        unsigned long v;
        while (*p == ' ' || *p == ',')
            p++;
        if (!*p)
            break;
        v = strtoul(p, &end, 16);
        if (end == p || v > 255 || n >= PEEK_MAX)
            return tool_fail(t, "bytes must be up to 256 hex values like \"41 1E\"");
        bytes[n++] = (unsigned char)v;
        p = end;
    }
    dst = (unsigned char __far *)MK_FP(seg, off);
    for (unsigned i = 0; i < n; i++)
        dst[i] = bytes[i];
    sprintf(t->summary, "%u bytes written", n);
    tout_printf(t, "wrote %u bytes at %04X:%04X\n", n, seg, off);
    return TS_OK;
}

static int toolPort(ToolCtx *t)
{
    unsigned port;

    if (targ_hex("port", &port))
        return tool_fail(t, "port must be hex, e.g. 3D9");
    if (targ("value") && *targ("value")) {
        long v = targ_num("value", -1);
        if (v < 0 || v > 255)
            return tool_fail(t, "value must be 0-255");
        outp(port, (int)v);
        sprintf(t->summary, "wrote 0x%02lX to port %X", v, port);
    } else {
        int v = inp(port);
        sprintf(t->summary, "port %X = 0x%02X", port, v);
    }
    tout_printf(t, "%s\n", t->summary);
    return TS_OK;
}

/* Coloured text into the transcript: ~X sets the colour, ~~ is a tilde. */
static int toolTextart(ToolCtx *t)
{
    const char *p = targ("text");
    unsigned lines = 0;
    int col = 0;

    if (p == NULL)
        return tool_fail(t, "missing text");
    con_margins(2, SCR_COLS);
    con_ensure_blank_line();
    con_attr(SCR_ATTR(C_LIGHTGRAY, C_BLACK));
    for (; *p && lines < 40; p++) {
        if (*p == '~' && p[1]) {
            char c = *++p;
            if (c != '~') {
                int v = c >= '0' && c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
                if (v >= 0 && v < 16)
                    con_attr(SCR_ATTR(v, C_BLACK));
                continue;
            }
        }
        if (*p == '\n') {
            con_newline();
            lines++;
            col = 0;
        } else if (*p != '\r' && col < 78) {
            con_putc((unsigned char)*p);
            col++;
        }
    }
    if (col)
        lines++;
    con_newline();
    sprintf(t->summary, "%u lines", lines);
    tout_printf(t, "printed %u lines\n", lines);
    return TS_OK;
}

/* A picture in CGA graphics; the chat comes back afterwards. */
static int toolDraw(ToolCtx *t)
{
    const char *script = targ("script");
    unsigned short *save;
    unsigned viewed;
    char err[90];
    int rc;

    if (script == NULL)
        return tool_fail(t, "missing script");
    save = (unsigned short *)malloc(SCR_SAVE_BYTES);
    if (save == NULL)
        return tool_fail(t, "not enough memory to save the screen");
    scr_suspend(save);
    rc = gfx_draw(script, unattended ? 3 : 0, err, sizeof(err), &viewed);
    scr_resume(save);
    free(save);
    ui_redraw();
    if (rc < 0) {
        tout_printf(t, "%s\n(everything before that line was drawn and shown for %us)\n", err, viewed);
        strncpy(t->summary, err, sizeof(t->summary) - 1);
        t->summary[sizeof(t->summary) - 1] = 0;
        return TS_ERROR;
    }
    if (rc > 0) {
        strcpy(t->summary, "stopped by user");
        tout_puts(t, "stopped by user before the picture was finished\n");
        return TS_STOPPED;
    }
    sprintf(t->summary, "drawn, viewed %us", viewed);
    tout_printf(t, "drawn; the user looked at it for %u seconds\n", viewed);
    return TS_OK;
}

static const ToolDef tools[] = {
    { "sysinfo", "SysInfo", NULL,      'R', "Inspecting", toolSysinfo },
    { "dir",     "Dir",     "path",    'R', "Listing",    tool_dir },
    { "read",    "Read",    "path",    'R', "Reading",    tool_read },
    { "peek",    "Peek",    "addr",    'R', "Peeking",    toolPeek },
    { "write",   "Write",   "path",    'W', "Writing",    tool_write },
    { "delete",  "Delete",  "path",    'W', "Deleting",   tool_delete },
    { "mkdir",   "MkDir",   "path",    'W', "Creating",   tool_mkdir },
    { "rename",  "Rename",  "from",    'W', "Renaming",   tool_rename },
    { "poke",    "Poke",    "addr",    'W', "Poking",     toolPoke },
    { "port",    "Port",    "port",    'W', "Probing",    toolPort },
    { "textart", "TextArt", NULL,      'H', "Painting",   toolTextart },
    { "play",    "Play",    NULL,      'H', "Playing",    tool_play },
    { "draw",    "Draw",    NULL,      'H', "Drawing",    toolDraw },
};


/* --- calls ---------------------------------------------------------------------- */

void tools_init(int safeMode, int noUser)
{
    safe = safeMode;
    unattended = noUser;
}

int tools_safe(void) { return safe; }
void tools_toggle_safe(void) { safe = !safe; }

static const ToolDef *findTool(const char *name)
{
    for (unsigned i = 0; i < sizeof(tools) / sizeof(tools[0]); i++)
        if (!strcmp(tools[i].name, name))
            return &tools[i];
    return NULL;
}

/* "12 lines, 340 bytes, overwrites existing file" */
static void writePreview(char *buf)
{
    const char *text = targ("text");
    const char *path = targ("path");
    unsigned long bytes = 0;
    unsigned lines = 0;
    long size = path ? fs_size(path) : -1;

    for (const char *p = text ? text : ""; *p; p++) {
        if (*p == '\r')
            continue;
        bytes += *p == '\n' ? 2 : 1;
        if (*p == '\n' || p[1] == 0)
            lines++;
    }
    sprintf(buf, "%u line%s, %lu bytes, %s", lines, lines == 1 ? "" : "s", bytes,
            targ_num("append", 0) ? (size >= 0 ? "appends" : "new file")
                                  : (size >= 0 ? "overwrites existing file" : "new file"));
}

static void sendResult(const char *status, ToolCtx *t)
{
    static char line[OUT_CHUNK * 2 + 8];
    const char *p = t->out;
    unsigned left = t->len;
    char done[40];

    while (left > 0) {
        unsigned n = left < OUT_CHUNK ? left : OUT_CHUNK, k = 0;
        for (unsigned i = 0; i < n; i++) {
            unsigned char c = (unsigned char)p[i];
            if (c == '\\') {
                line[k++] = '\\';
                line[k++] = '\\';
            } else if (c == '\n') {
                line[k++] = '\\';
                line[k++] = 'n';
            } else {
                line[k++] = c < 32 && c != '\t' ? '.' : (char)c;
            }
        }
        line[k] = 0;
        ui_send("OUT", line);
        p += n;
        left -= n;
    }
    if (t->truncated)
        ui_send("OUT", "\\n...(truncated)");
    sprintf(done, "%s %s", callId, status);
    ui_send("DONE", done);
}

static void runCall(void)
{
    static const char *const statusNames[] = { "ok", "error", "denied", "stopped" };
    const ToolDef *d = findTool(callName);
    const char *arg;
    char text[100];
    ToolCtx t;
    int status = -1;

    tout_reset(&t);
    t.unattended = unattended;
    if (d == NULL) {
        ui_tool_header(callName, "");
        sprintf(text, "unknown tool %.20s", callName);
        status = tool_fail(&t, text);
    } else {
        arg = d->headerKey ? targ(d->headerKey) : NULL;
        ui_tool_header(d->title, arg ? arg : "");
        if (argsOverflow) {
            status = tool_fail(&t, "arguments too long");
        } else if (d->cls == 'W' && safe) {
            int key;
            if (!strcmp(d->name, "write")) {
                writePreview(text);
                ui_tool_detail(text);
            }
            sprintf(text, "Allow %s(%.40s)? (y/n)", d->title, arg ? arg : "");
            key = ui_ask(text);
            if (key == 'n')
                status = TS_DENIED;
            else if (key == 27)
                status = TS_STOPPED;
        }
        if (status < 0) {
            ui_tool_busy(d->busy);
            status = d->fn(&t);
        }
    }

    if (status == TS_DENIED)
        ui_tool_note("Denied by user", 1);
    else if (status == TS_STOPPED)
        ui_tool_note("Interrupted by user", 1);
    else
        ui_tool_note(t.summary[0] ? t.summary : (status == TS_OK ? "done" : "failed"),
                     status != TS_OK);
    sendResult(statusNames[status], &t);
    if (status == TS_STOPPED)
        ui_send("STOP", NULL);
    ui_tool_done();
}

int tools_line(const char *verb, const char *payload)
{
    if (!strcmp(verb, "TOOL")) {
        const char *sp = strchr(payload, ' ');
        unsigned n = sp ? (unsigned)(sp - payload) : strlen(payload);
        if (n >= sizeof(callId))
            n = sizeof(callId) - 1;
        memcpy(callId, payload, n);
        callId[n] = 0;
        strncpy(callName, sp ? sp + 1 : "", sizeof(callName) - 1);
        callName[sizeof(callName) - 1] = 0;
        targ_reset();
        argsOverflow = 0;
        return 1;
    }
    if (!strcmp(verb, "ARG")) {
        char key[16];
        const char *sp = strchr(payload, ' ');
        unsigned n = sp ? (unsigned)(sp - payload) : strlen(payload);
        if (n >= sizeof(key))
            n = sizeof(key) - 1;
        memcpy(key, payload, n);
        key[n] = 0;
        if (targ_append(key, sp ? sp + 1 : ""))
            argsOverflow = 1;
        return 1;
    }
    if (!strcmp(verb, "CALL")) {
        runCall();
        return 1;
    }
    return 0;
}
