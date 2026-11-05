/* HARNESS.EXE tool calls: see tools.h.
 *
 * The model's tool_use block arrives as a name and a JSON input object
 * (chat.cpp). We show the call in the transcript, ask first if it's a W
 * (write) tool and safe mode is on, run its executor, and hand the result back
 * for the tool_result message. Executors live in lib/ (toolapi.h); the small
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
#include "relaunch.h"
#include "screen.h"
#include "console.h"
#include "tools.h"
#include "ui.h"
#include "json.h"

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
unsigned short __far *tools_save_screen(void)
{
    unsigned short __far *save = (unsigned short __far *)ems_page();
    if (save == NULL)
        save = (unsigned short __far *)malloc(SCR_SAVE_BYTES);
    if (save != NULL)
        scr_suspend(save);
    return save;
}

void tools_restore_screen(unsigned short __far *save)
{
    unsigned short __far *page = (unsigned short __far *)ems_page();   /* remap it */
    scr_resume(save);
    if (save != page)
        free(save);
    ui_redraw();
}

static int launched;                  /* started by CLAWDOS.EXE: runs go to it */

void tools_set_launched(int on) { launched = on; }

/* DOS commands on a plain DOS screen; the chat comes back afterwards. With
   full_memory under the launcher the commands are written out for it and the
   harness leaves, so the program gets the memory the harness held
   (relaunch.h); otherwise they run here, beside the harness. */
static int toolRun(ToolCtx *t)
{
    const char *cmd = targ("command");
    const char *fm = targ("full_memory");     /* true arrives as "1" (setArg) */
    int big = fm && (*fm == '1' || !stricmp(fm, "true"));
    unsigned short __far *save;
    int status;

    if (big && launched) {
        if (relaunch_write_run(cmd, unattended ? 1 : 5))
            return tool_fail(t, "cannot write " RELAUNCH_RUN);
        return TS_RELAUNCH;
    }
    if ((save = tools_save_screen()) == NULL)
        return tool_fail(t, "not enough memory to save the screen");
    status = shell_run(t, cmd, unattended ? 1 : 5);
    tools_restore_screen(save);
    if (big && !launched)
        tout_puts(t, "\n(full_memory had no effect: HARNESS.EXE was started without CLAWDOS.EXE,"
                     " so the program ran beside it)\n");
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

/* Show, approve and run the call named `name` whose arguments are in the store. */
static int execute(const char *name, int overflow, ToolCtx *t)
{
    const ToolDef *d = findTool(name);
    const char *arg;
    char text[100];
    int status = -1;

    tout_reset(t);
    t->unattended = unattended;
    if (d == NULL) {
        ui_tool_header(name, "");
        sprintf(text, "unknown tool %.20s", name);
        status = tool_fail(t, text);
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
        if (overflow) {
            status = tool_fail(t, "arguments too long");
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
            status = d->fn(t);
        }
    }

    if (status == TS_RELAUNCH)
        return status;                /* tools_show_result shows the note after the relaunch */
    if (status == TS_DENIED)
        ui_tool_note("Denied by user", 1);
    else if (status == TS_STOPPED)
        ui_tool_note("Interrupted by user", 1);
    else
        ui_tool_note(t->summary[0] ? t->summary : (status == TS_OK ? "done" : "failed"),
                     status != TS_OK);
    return status;
}

void tools_show_result(int status, const ToolCtx *t)
{
    if (status == TS_STOPPED)
        ui_tool_note("Interrupted by user", 1);
    else
        ui_tool_note(t->summary[0] ? t->summary : (status == TS_OK ? "done" : "failed"),
                     status != TS_OK);
    ui_tool_done();
}

/* One member of the tool_use input: strings decoded, numbers as written,
   booleans 1/0, null skipped. */
static int setArg(void *ctx, const char *key, unsigned klen, const char *val, unsigned vlen, int type)
{
    static char *decoded;
    char k[16];
    int *overflow = (int *)ctx;
    unsigned n;

    if (klen >= sizeof k)
        klen = sizeof k - 1;
    memcpy(k, key, klen);
    k[klen] = 0;
    if (type == JSON_NULL)
        return 0;
    if (type == JSON_TRUE || type == JSON_FALSE)
        return targ_set(k, type == JSON_TRUE ? "1" : "0", 1) ? (*overflow = 1, 1) : 0;
    if (type != JSON_STRING)
        return targ_set(k, val, vlen) ? (*overflow = 1, 1) : 0;
    if (decoded == NULL)
        decoded = (char *)malloc(TOOL_ARGS_MAX);
    if (decoded == NULL) {
        *overflow = 1;
        return 1;
    }
    n = json_unescape(val, vlen, decoded, TOOL_ARGS_MAX);
    if (targ_set(k, decoded, n)) {
        *overflow = 1;
        return 1;
    }
    return 0;
}

int tools_call(const char *name, const char *input, unsigned len, ToolCtx *t)
{
    int overflow = 0;

    if (tool_idle)                    /* keep the connection alive before a long tool */
        tool_idle();
    targ_reset();
    if (len && json_each(input, len, setArg, &overflow) < 0 && !overflow) {
        tout_reset(t);
        ui_tool_header(name, "");
        ui_tool_done();
        return tool_fail(t, "arguments are not valid JSON");
    }
    int status = execute(name, overflow, t);
    ui_tool_done();
    return status;
}

const char *tools_result_text(int status, const ToolCtx *t)
{
    static char *errText;
    switch (status) {
    case TS_OK:
        return t->out;
    case TS_DENIED:
        return "The user denied this tool call.";
    case TS_STOPPED:
        return "Interrupted by the user.";
    default:
        if (errText == NULL)
            errText = (char *)malloc(TOOL_OUT_MAX + 8);
        if (errText == NULL)
            return "Error";
        sprintf(errText, "Error: %s", t->out);
        return errText;
    }
}
