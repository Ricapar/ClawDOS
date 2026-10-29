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
#include <dos.h>

#include "toolapi.h"
#include "dosfs.h"
#include "edit.h"
#include "search.h"
#include "memory.h"
#include "ems.h"
#include "sysinfo.h"
#include "sound.h"
#include "shell.h"
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
    char cls;                         /* R read-only, H harmless output, W writes (asks
                                         in safe mode), M memory (never asks) */
    const char *busy;                 /* spinner verb */
    ToolFn fn;
};

static int safe = 1;
static int unattended;
static char callId[16];
static char callName[16];
static int argsOverflow;
static int stopping;                  /* STOP sent; don't start anything new */


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

/* Somewhere to keep the chat screen while a tool has the display: an EMS page
   when there is EMS (one page is exactly the CGA's 16 KB), so a program run
   meanwhile gets that conventional memory, or else the heap. */
static unsigned short __far *saveScreen(void)
{
    unsigned short __far *save = (unsigned short __far *)ems_page();
    if (save == NULL)
        save = (unsigned short __far *)malloc(SCR_SAVE_BYTES);
    if (save != NULL)
        scr_suspend(save);
    return save;
}

static void restoreScreen(unsigned short __far *save)
{
    unsigned short __far *page = (unsigned short __far *)ems_page();   /* remap it */
    scr_resume(save);
    if (save != page)
        free(save);
    ui_redraw();
}

/* DOS commands on a plain DOS screen; the chat comes back afterwards. */
static int toolRun(ToolCtx *t)
{
    const char *cmd = targ("command");
    unsigned short __far *save;
    int status;

    if ((save = saveScreen()) == NULL)
        return tool_fail(t, "not enough memory to save the screen");
    status = shell_run(t, cmd, unattended ? 1 : 5);
    restoreScreen(save);
    return status;
}

static const ToolDef tools[] = {
    { "sysinfo", "SysInfo", NULL,      'R', "Inspecting", toolSysinfo },
    { "dir",     "Dir",     "path",    'R', "Listing",    tool_dir },
    { "read",    "Read",    "path",    'R', "Reading",    tool_read },
    { "peek",    "Peek",    "addr",    'R', "Peeking",    toolPeek },
    { "search",  "Search",  "text",    'R', "Searching",  tool_search },
    { "write",   "Write",   "path",    'W', "Writing",    tool_write },
    { "edit",    "Edit",    "path",    'W', "Editing",    tool_edit },
    { "delete",  "Delete",  "path",    'W', "Deleting",   tool_delete },
    { "mkdir",   "MkDir",   "path",    'W', "Creating",   tool_mkdir },
    { "rename",  "Rename",  "from",    'W', "Renaming",   tool_rename },
    { "poke",    "Poke",    "addr",    'W', "Poking",     toolPoke },
    { "port",    "Port",    "port",    'W', "Probing",    toolPort },
    { "textart", "TextArt", NULL,      'H', "Painting",   toolTextart },
    { "play",    "Play",    NULL,      'H', "Playing",    tool_play },
    { "run",     "Run",     "command", 'W', "Running",    toolRun },
    /* 'M': memories are the model's own notes, so it never has to ask */
    { "memory_list",  "Memories", NULL,   'M', "Remembering", tool_memory_list },
    { "memory_read",  "Memory",   "name", 'M', "Remembering", tool_memory_read },
    { "memory_write", "Remember", "name", 'M', "Remembering", tool_memory_write },
};


/* --- calls ---------------------------------------------------------------------- */

/* DOS critical errors (no disk in A:, write-protected disk) fail the call
   instead of printing "Abort, Retry, Fail?" over the chat and waiting. */
static int __far criticalError(unsigned deverr, unsigned errcode, unsigned __far *devhdr)
{
    (void)deverr;
    (void)errcode;
    (void)devhdr;
    return _HARDERR_FAIL;
}

void tools_init(int safeMode, int noUser)
{
    safe = safeMode;
    unattended = noUser;
    _harderr(criticalError);
}

void tools_stopping(int on) { stopping = on; }

void tools_reset(void)
{
    callId[0] = 0;
    callName[0] = 0;
    targ_reset();
    argsOverflow = 0;
    stopping = 0;
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

/* "replaces 3 lines with 5" */
static void editPreview(char *buf)
{
    const char *old = targ("old"), *repl = targ("new");
    unsigned o = old ? edit_lines(old) : 0, n = repl ? edit_lines(repl) : 0;

    sprintf(buf, "replaces %u line%s with %u%s", o, o == 1 ? "" : "s", n,
            targ_num("all", 0) ? ", everywhere it occurs" : "");
}

/* The later commands of a run call, one per line under its header. */
static void runDetails(const char *cmds)
{
    char cmd[SHELL_CMD_MAX + 1];
    const char *p = cmds + strspn(cmds, " \t\r\n");

    p += strcspn(p, "\r\n");          /* the first is in the header */
    while (*(p += strspn(p, " \t\r\n"))) {
        unsigned n = strcspn(p, "\r\n");
        memcpy(cmd, p, n < SHELL_CMD_MAX ? n : SHELL_CMD_MAX);
        cmd[n < SHELL_CMD_MAX ? n : SHELL_CMD_MAX] = 0;
        ui_tool_detail(cmd);
        p += n;
    }
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

    if (tool_idle)                    /* ACK the call before a long tool keeps us busy */
        tool_idle();
    tout_reset(&t);
    t.unattended = unattended;
    if (stopping) {                   /* sent before our STOP reached the bridge */
        sprintf(text, "%s stopped", callId);
        ui_send("DONE", text);
        return;
    }
    if (d == NULL) {
        ui_tool_header(callName, "");
        sprintf(text, "unknown tool %.20s", callName);
        status = tool_fail(&t, text);
    } else {
        static char first[SHELL_CMD_MAX + 16];
        unsigned cmds = 0;
        arg = d->headerKey ? targ(d->headerKey) : NULL;
        if (!strcmp(d->name, "run") && arg) {   /* a list of commands: the first, and how many */
            cmds = shell_count(arg, first);
            if (cmds > 1)
                sprintf(first + strlen(first), " +%u more", cmds - 1);
            arg = first;
        }
        ui_tool_header(d->title, arg ? arg : "");
        if (cmds > 1)
            runDetails(targ("command"));
        if (argsOverflow) {
            status = tool_fail(&t, "arguments too long");
        } else if (d->cls == 'W' && safe) {
            int key;
            if (!strcmp(d->name, "write")) {
                writePreview(text);
                ui_tool_detail(text);
            } else if (!strcmp(d->name, "edit")) {
                editPreview(text);
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
    if (status == TS_STOPPED) {
        ui_send("STOP", NULL);
        stopping = 1;
    }
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
        if (callId[0])                /* not a call cut short by a lost link */
            runCall();
        callId[0] = 0;
        return 1;
    }
    return 0;
}
