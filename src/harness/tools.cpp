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
#include <io.h>

#include "toolapi.h"
#include "dosfs.h"
#include "edit.h"
#include "search.h"
#include "memory.h"
#include "ems.h"
#include "sysinfo.h"
#include "sound.h"
#include "shell.h"
#include "shot.h"
#include "shotpng.h"
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
    long n;

    if (targ_addr("addr", &seg, &off))
        return tool_fail(t, "addr must be hex seg:off, e.g. 0040:0000");
    if (targ_long(t, "len", 64, &n))
        return TS_ERROR;
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
        while (*p == ' ' || *p == '\t' || *p == ',')
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
        long v;
        if (targ_long(t, "value", -1, &v))
            return TS_ERROR;
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
static char shotLine[40];             /* the transcript's line about the screenshot */
static int shotAttached;              /* SHOT_PNG goes with the result */

void tools_set_launched(int on) { launched = on; }

const char *tools_result_image(void) { return shotAttached ? SHOT_PNG : NULL; }

void tools_result_image_done(void)
{
    if (shotAttached)
        remove(SHOT_PNG);
    shotAttached = 0;
}

/* The screenshot a run asks for: 1 (info's force and color set, *ticks to
   wait, 0 for at exit), 0 none, -1 after failing the call over a bad
   argument. Any of the shot arguments asks for one. */
static int shotArgs(ToolCtx *t, ShotInfo *info, unsigned *ticks)
{
    const char *at = targ("shot_at"), *mode = targ("shot_mode");
    int want;
    long color;

    info->force = SHOT_AUTO;
    info->color = SHOT_NO_COLOR;
    *ticks = 0;
    if (targ_bool(t, "screenshot", &want))
        return -1;
    if (mode && *mode) {
        if (!stricmp(mode, "320x200"))
            info->force = SHOT_320;
        else if (!stricmp(mode, "640x200"))
            info->force = SHOT_640;
        else if (!stricmp(mode, "text"))
            info->force = SHOT_TEXT;
        else
            return tool_fail(t, "shot_mode must be 320x200, 640x200 or text"), -1;
        want = 1;
    }
    if (targ("shot_color") && *targ("shot_color")) {
        if (targ_long(t, "shot_color", 0, &color))
            return -1;
        if (color < 0 || color > 127)
            return tool_fail(t, "shot_color must be 0-127 (the colour register's value, +64 for the burst off)"), -1;
        info->color = (unsigned char)color;
        want = 1;
    }
    if (at && *at) {                  /* seconds, to a tenth: "2", "0.5" */
        unsigned tenths;
        if (shot_seconds(at, &tenths))
            return tool_fail(t, "shot_at must be seconds after the first command starts, 0.1 to 300"), -1;
        *ticks = (unsigned)((tenths * 182UL + 50) / 100);
        if (*ticks == 0)
            *ticks = 1;
        want = 1;
    }
    return want;
}

/* What the model gets from a captured frame (lib/shot.h): its text in the
   result (a text mode), or SHOT_PNG made to go with it; shotLine gets the
   transcript's line. 1 when SHOT_PNG is to be attached. */
static int shotResult(ToolCtx *t, const ShotInfo *info, const unsigned char __far *frame)
{
    static const char *const names[] = { "", "320x200", "640x200" };
    unsigned char rgb[12];
    int kind = info->force;
    unsigned colors;

    if (kind == SHOT_AUTO)            /* the BIOS's modes 4 and 5, 6, 0 to 3 */
        kind = info->mode == 4 || info->mode == 5 ? SHOT_320 : info->mode == 6 ? SHOT_640
             : info->mode <= 3 ? SHOT_TEXT : SHOT_AUTO;
    if (info->how == SHOT_LATE)
        tout_puts(t, "\n(shot_at was not reached: this is the screen at exit)");
    else if (info->how == SHOT_NOBUF)
        tout_puts(t, "\n(no memory for the shot_at copy: this is the screen at exit)");
    if (kind == SHOT_AUTO) {
        tout_printf(t, "\n(no screenshot: video mode %u; give shot_mode)\n", info->mode);
        strcpy(shotLine, "no screenshot: unknown video mode");
        return 0;
    }
    if (kind == SHOT_TEXT) {
        unsigned cols = info->mode <= 1 ? 40 : 80;
        tout_printf(t, "\n(screenshot: text %ux25)\n", cols);
        shell_screen_text(t, t->len < TOOL_OUT_MAX - 1 ? TOOL_OUT_MAX - 1 - t->len : 0, frame, cols);
        sprintf(shotLine, "screenshot: text %ux25", cols);
        return 0;
    }
    colors = shot_palette(info, kind, rgb);
    if (png_write_cga(SHOT_PNG, frame, kind == SHOT_320 ? 2 : 1, rgb, colors)) {
        tout_puts(t, "\n(no screenshot: cannot write " SHOT_PNG ")\n");
        strcpy(shotLine, "no screenshot: cannot write the file");
        return 0;
    }
    tout_printf(t, "\n(screenshot: %s, colour register %02Xh)\n", names[kind],
                info->color != SHOT_NO_COLOR ? info->color : info->colorReg);
    sprintf(shotLine, "screenshot %s, 16 KB", names[kind]);   /* 16,280 bytes and the palette's */
    return 1;
}

/* DOS commands on a plain DOS screen; the chat comes back afterwards. With
   full_memory under the launcher the commands are written out for it and the
   harness leaves, so the program gets the memory the harness held
   (relaunch.h); otherwise they run here, beside the harness. A screenshot is
   taken before the chat screen comes back (or at shot_at, by the timer hook)
   and made into a PNG here. */
static int toolRun(ToolCtx *t)
{
    const char *cmd = targ("command");
    unsigned short __far *save;
    ShotInfo shot;
    unsigned ticks;
    int status, big, want;

    if (targ_bool(t, "full_memory", &big))
        return TS_ERROR;
    if ((want = shotArgs(t, &shot, &ticks)) < 0)
        return TS_ERROR;
    if (big && launched) {
        char head[32];
        if (cmd == NULL)
            return tool_fail(t, "missing command");
        head[0] = 0;
        if (want)
            sprintf(head, RELAUNCH_SHOT "%u %u %u\n", ticks, shot.force, shot.color);
        if (relaunch_write_run(head, cmd))
            return tool_fail(t, "cannot write " RELAUNCH_RUN);
        return TS_RELAUNCH;
    }
    if ((save = tools_save_screen()) == NULL)
        return tool_fail(t, "not enough memory to save the screen");
    if (want && ticks)
        shot_arm(ticks);
    status = shell_run(t, cmd);
    if (want) {
        shot_disarm();
        if (status == TS_OK)
            shotAttached = shotResult(t, &shot, shot_frame(&shot));
        shot_release();
    }
    tools_restore_screen(save);
    if (big && !launched)
        tout_puts(t, "\n(full_memory had no effect: HARNESS.EXE was started without CLAWDOS.EXE,"
                     " so the program ran beside it)\n");
    return status;
}

void tools_resume_shot(int status, ToolCtx *t)
{
    ShotInfo shot;
    unsigned char __far *frame = NULL;
    unsigned seg = 0;

    shotLine[0] = 0;
    shotAttached = 0;
    if (access(SHOT_RAW, 0))
        return;                       /* no screenshot asked for */
    /* The frame for the moment: the EMS page, else a DOS block given back
       after (the C heap would keep 16 KB from every later program). */
    if (status == TS_OK && (frame = (unsigned char __far *)ems_map(EMS_SHOT_PAGE)) == NULL
        && _dos_allocmem(SHOT_BYTES / 16, &seg) == 0)
        frame = (unsigned char __far *)MK_FP(seg, 0);
    if (frame != NULL && shot_load_raw(&shot, frame) == 0)
        shotAttached = shotResult(t, &shot, frame);
    else if (status == TS_OK)
        tout_puts(t, "\n(no screenshot: the launcher's copy of the screen could not be read)\n");
    if (seg)
        _dos_freemem(seg);
    remove(SHOT_RAW);
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

void tools_init(int safeMode)
{
    safe = safeMode;
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
            targ_flag("append") > 0 ? (size >= 0 ? "appends" : "new file")
                                  : (size >= 0 ? "overwrites existing file" : "new file"));
}

/* "replaces 3 lines with 5" */
static void editPreview(char *buf)
{
    const char *old = targ("old"), *repl = targ("new");
    unsigned o = old ? edit_lines(old) : 0, n = repl ? edit_lines(repl) : 0;

    sprintf(buf, "replaces %u line%s with %u%s", o, o == 1 ? "" : "s", n,
            targ_flag("all") > 0 ? ", everywhere it occurs" : "");
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

#define SHOWN_LINES 6                 /* of a run's output, under its note */

/* The last lines of a run's output (t->showFrom..showTo), dim under its note,
   after a line counting those above them: the program's screen is gone once
   the chat is back, and the model's copy is the whole result. */
static void showLastLines(const ToolCtx *t)
{
    const char *from = t->out + t->showFrom, *end = t->out + t->showTo, *p;
    char line[SCR_COLS - 8];          /* one row: these lines start in column 8 */
    unsigned lines = 1, shown;

    while (end > from && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
        end--;                        /* trailing blank lines */
    if (end == from)
        return;
    for (p = from; p < end; p++)
        lines += *p == '\n';
    shown = lines < SHOWN_LINES ? lines : SHOWN_LINES;
    if (lines > shown) {
        sprintf(line, "... %u earlier line%s", lines - shown, lines - shown == 1 ? "" : "s");
        ui_tool_detail(line);
    }
    for (p = end; ; p--)              /* back to the start of the first line shown */
        if (p == from || (p[-1] == '\n' && --shown == 0))
            break;
    while (p < end) {
        unsigned n = 0;
        for (; p < end && *p != '\n'; p++)
            if (*p != '\r' && n < sizeof line - 1)
                line[n++] = (unsigned char)*p < ' ' ? ' ' : *p;
        line[n] = 0;
        ui_tool_detail(line);
        p++;
    }
}

/* The argument shown in d's header (from the store): a run's first command,
   with how many follow in *cmds. NULL if it has none. */
static const char *headerArg(const ToolDef *d, unsigned *cmds)
{
    static char first[SHELL_CMD_MAX + 16];
    const char *arg = d->headerKey ? targ(d->headerKey) : NULL;

    *cmds = 0;
    if (!strcmp(d->name, "run") && arg) {   /* a list of commands: the first, and how many */
        *cmds = shell_count(arg, first);
        if (*cmds > 1)
            sprintf(first + strlen(first), " +%u more", *cmds - 1);
        arg = first;
    }
    return arg;
}

/* Show, approve and run the call named `name` whose arguments are in the store. */
static int execute(const char *name, int overflow, ToolCtx *t)
{
    const ToolDef *d = findTool(name);
    const char *arg;
    char text[100];
    int status = -1;

    tout_reset(t);
    if (d == NULL) {
        ui_tool_header(name, "");
        sprintf(text, "unknown tool %.20s", name);
        status = tool_fail(t, text);
    } else {
        unsigned cmds;
        arg = headerArg(d, &cmds);
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
    showLastLines(t);
    if (shotLine[0])
        ui_tool_detail(shotLine);
    return status;
}

void tools_show_result(int status, const ToolCtx *t)
{
    if (status == TS_STOPPED)
        ui_tool_note("Interrupted by user", 1);
    else
        ui_tool_note(t->summary[0] ? t->summary : (status == TS_OK ? "done" : "failed"),
                     status != TS_OK);
    showLastLines(t);
    if (shotLine[0])
        ui_tool_detail(shotLine);
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
    tools_result_image_done();        /* a picture never written (the turn ended) goes too */
    shotLine[0] = 0;
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

void tools_show_call(const char *name, const char *input, unsigned len)
{
    const ToolDef *d = findTool(name);
    const char *arg;
    unsigned cmds;
    int overflow = 0;

    targ_reset();
    if (len)                          /* an input cut short still sets the members before the cut */
        json_each(input, len, setArg, &overflow);
    arg = d ? headerArg(d, &cmds) : NULL;
    ui_tool_header(d ? d->title : name, arg ? arg : "");
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
