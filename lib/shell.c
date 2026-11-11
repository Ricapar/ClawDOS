/* ClawDOS shell: see shell.h.
 *
 * Output is captured by hooking INT 10h function 0Eh (teletype). DOS console
 * output (INT 21h functions 02h, 09h, 40h and INT 29h) is printed through it,
 * as is most programs' BIOS output, so each character is seen exactly once.
 * The first HEAD_MAX bytes are kept, then a ring keeps the last TAIL_MAX:
 * errors are usually at the end.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <process.h>
#include <fcntl.h>
#include <io.h>
#include <dos.h>
#include <i86.h>
#include "shell.h"

#define HEAD_MAX 900
#define TAIL_MAX 2800

static char head[HEAD_MAX];
static char tail[TAIL_MAX];
/* Changed only by the interrupt hook: volatile, or the optimizer carries the
   values from before spawnl() across the call. */
static volatile unsigned headLen;
static volatile unsigned tailStart, tailLen;  /* ring */
static volatile unsigned long total;          /* bytes captured, including dropped ones */

static void (__interrupt __far *oldInt10)();

void (*shell_before_run)(void);

/* Runs on the program's stack. Keep it free of calls to our own functions:
   a helper called from here never ran in testing, and the cause wasn't found
   (code compiled for SS == DGROUP running on a foreign stack is a suspect). */
static void __interrupt __far int10Hook(union INTPACK r)
{
    unsigned char c = r.h.al;

    if (r.h.ah != 0x0E || c == '\r' || c == 7) {
        /* not printing, or nothing worth keeping */
    } else if (c == 8) {              /* backspace: take the last byte back */
        if (tailLen) {
            tailLen--;
            total--;
        } else if (headLen && total <= HEAD_MAX) {
            headLen--;
            total--;
        }
    } else {
        total++;
        if (headLen < HEAD_MAX) {
            head[headLen++] = c;
        } else if (tailLen < TAIL_MAX) {
            tail[(tailStart + tailLen++) % TAIL_MAX] = c;
        } else {
            tail[tailStart] = c;
            tailStart = (tailStart + 1) % TAIL_MAX;
        }
    }
    _chain_intr(oldInt10);
}

static void teletype(const char *s)
{
    union REGS r;
    for (; *s; s++) {
        if (*s == '\n') {
            r.x.ax = 0x0E0D;
            int86(0x10, &r, &r);
        }
        r.h.ah = 0x0E;
        r.h.al = (unsigned char)*s;
        r.h.bh = 0;
        r.h.bl = 7;
        int86(0x10, &r, &r);
    }
}

void shell_screen_text(ToolCtx *t, unsigned room, const unsigned char __far *vram, unsigned cols)
{
    char line[82];                    /* 80 columns, newline, NUL */
    unsigned char lens[25];
    int row, first, last = -1;
    unsigned col, n, used = 0;

    for (row = 0; row < 25; row++) {
        lens[row] = 0;
        for (col = 0; col < cols; col++)
            if (vram[(row * cols + col) * 2] > ' ')
                lens[row] = (unsigned char)(col + 1);
        if (lens[row])
            last = row;
    }
    for (first = last + 1; first > 0 && used + lens[first - 1] + 1 <= room; first--)
        used += lens[first - 1] + 1;
    for (row = first; row <= last; row++) {
        n = lens[row];
        for (col = 0; col < n; col++) {
            unsigned char c = vram[(row * cols + col) * 2];
            line[col] = c < ' ' ? ' ' : (char)c;
        }
        line[n] = '\n';
        line[n + 1] = 0;
        tout_puts(t, line);
    }
}

/* The screen's text when a program printed nothing through the BIOS. A
   graphics screen has none: its bytes would read as rubbish. */
static void snapshot(ToolCtx *t, unsigned room)
{
    unsigned char mode = *(const unsigned char __far *)MK_FP(0x40, 0x49);

    if (mode > 3) {
        tout_printf(t, "(graphics screen, video mode %u: no text; a screenshot shows it)\n", mode);
        return;
    }
    tout_puts(t, "(screen text)\n");
    t->showFrom = t->len;             /* the transcript shows the rows, not the label */
    shell_screen_text(t, room > 15 ? room - 15 : 0, (const unsigned char __far *)MK_FP(0xB800, 0), 80);
}

/* Byte i of what was captured: the head, then the tail ring. */
static char captured(unsigned i)
{
    return i < headLen ? head[i] : tail[(tailStart + i - headLen) % TAIL_MAX];
}

static void putCaptured(ToolCtx *t, unsigned from, unsigned to)
{
    char chunk[129];
    unsigned k = 0;

    for (; from < to; from++) {
        chunk[k++] = captured(from);
        if (k == sizeof(chunk) - 1 || from + 1 == to) {
            chunk[k] = 0;
            tout_puts(t, chunk);
            k = 0;
        }
    }
}

/* The captured output in at most `room` bytes: all of it, or its start and
   (mostly) its end, where errors usually are. */
static void putOutput(ToolCtx *t, unsigned room)
{
    unsigned n = headLen + tailLen, a = headLen, b = tailLen;

    if (n > room) {
        a = room / 4;
        b = room - a;
    }
    putCaptured(t, 0, a);
    if (total > (unsigned long)a + b)
        tout_printf(t, "\n...(%lu bytes omitted)...\n", total - a - b);
    putCaptured(t, n - b, n);
}

static unsigned countLines(void)
{
    unsigned n = 0, i;
    for (i = 0; i < headLen; i++)
        n += head[i] == '\n';
    for (i = 0; i < tailLen; i++)
        n += tail[(tailStart + i) % TAIL_MAX] == '\n';
    return n;
}

/* COMMAND.COM's built-in commands: these need the shell. */
static const char *const builtins[] = {
    "BREAK", "CALL", "CD", "CHCP", "CHDIR", "CLS", "COPY", "CTTY", "DATE", "DEL", "DIR",
    "ECHO", "ERASE", "EXIT", "FOR", "GOTO", "IF", "LH", "LOADHIGH", "MD", "MKDIR", "PATH",
    "PAUSE", "PROMPT", "RD", "REM", "REN", "RENAME", "RMDIR", "SET", "SHIFT", "TIME",
    "TRUENAME", "TYPE", "VER", "VERIFY", "VOL", NULL
};

static int exists(const char *path)
{
    struct find_t f;
    return _dos_findfirst(path, _A_NORMAL | _A_RDONLY | _A_ARCH, &f) == 0;
}

/* `dir` + `name` (+ `ext`) into `out` if that file exists. */
static int tryFile(char *out, const char *dir, const char *name, const char *ext)
{
    unsigned n = strlen(dir);
    if (n + strlen(name) + strlen(ext) + 2 > 80)
        return 0;
    strcpy(out, dir);
    if (n && out[n - 1] != '\\' && out[n - 1] != ':')
        strcat(out, "\\");
    strcat(out, name);
    strcat(out, ext);
    return exists(out);
}

/* If `cmd` is just a program and its arguments, the program's path (in `prog`)
   and where its arguments start; NULL when it needs COMMAND.COM: redirection,
   pipes, variables, built-ins, batch files, or a program DOS can't find. Started
   directly, the program gets the ~70 KB a second COMMAND.COM would take. */
static const char *direct(const char *cmd, char *prog)
{
    static const char *const exts[] = { ".COM", ".EXE", ".BAT", NULL };
    char name[80], dir[80];
    const char *args, *path, *ext;
    unsigned n, i;

    if (strpbrk(cmd, "<>|%\""))
        return NULL;
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    n = strcspn(cmd, " /;=,\t");
    if (n == 0 || n >= sizeof(name))
        return NULL;
    memcpy(name, cmd, n);
    name[n] = 0;
    strupr(name);
    args = cmd + n;
    while (*args == ' ' || *args == '\t')
        args++;
    if (name[n - 1] == ':')
        return NULL;                          /* "A:": change drive */
    for (i = 0; builtins[i]; i++)
        if (!strcmp(name, builtins[i]))
            return NULL;

    ext = strrchr(name, '.');
    if (ext && strchr(ext, '\\'))
        ext = NULL;                           /* a dot in a directory name */
    if (strpbrk(name, "\\:")) {               /* an explicit path: only there */
        if (ext)
            strcpy(prog, name);
        else
            for (i = 0; exts[i] && !tryFile(prog, "", name, exts[i]); i++)
                ;
        if (!exists(prog))
            return NULL;
    } else {                                  /* the current directory, then PATH */
        path = getenv("PATH");
        strcpy(dir, ".");
        for (;;) {
            if (ext) {
                if (tryFile(prog, dir, name, ""))
                    break;
            } else {
                for (i = 0; exts[i]; i++)
                    if (tryFile(prog, dir, name, exts[i]))
                        break;
                if (exts[i])
                    break;
            }
            if (path == NULL || !*path)
                return NULL;
            n = strcspn(path, ";");
            if (n >= sizeof(dir))
                n = sizeof(dir) - 1;
            memcpy(dir, path, n);
            dir[n] = 0;
            path += n + (path[n] == ';');
        }
    }
    ext = strrchr(prog, '.');
    if (ext == NULL || (stricmp(ext, ".COM") && stricmp(ext, ".EXE")))
        return NULL;                          /* a batch file needs the shell */
    return args;
}

#define WHY_MAX 120                   /* "could not start ...: ..." */

/* The largest free block FreeCOM 0.86 (the image's COMMAND.COM) needs to run
   a command, with a margin. It loads with about 72 KB, but below 100,096
   bytes it cannot load its strings and asks on the keyboard for another
   COMMAND.COM to take them from: with standard input on NUL nobody answers,
   and the PC hangs (tests/unit/eatmem.c measured it, in steps of 256 bytes).
   CLAWDOS.EXE links this file too, where "set full_memory" would be the wrong
   advice; there it cannot happen, with the harness out of memory (about
   450 KB free, and the launcher is the whole of what full_memory gives). */
#define SHELL_NEEDS 102400UL

static unsigned long largestFree(void)
{
    union REGS r;
    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;                  /* more than there is: DOS says how much */
    intdos(&r, &r);
    return (unsigned long)r.x.bx * 16;
}

/* Run one command, capturing its output. The exit code, or -1 if it couldn't
   start (why is in `why`, WHY_MAX bytes). */
static int runOne(const char *cmd, char *why)
{
    static char prog[80];
    const char *args, *comspec = getenv("COMSPEC");
    int code, nul, savedIn = -1;

    if (comspec == NULL || !*comspec)
        comspec = "C:\\COMMAND.COM";
    teletype("C:\\> ");
    teletype(cmd);
    teletype("\n");
    headLen = tailStart = tailLen = 0;
    total = 0;
    args = direct(cmd, prog);
    if (args == NULL && largestFree() < SHELL_NEEDS) {
        snprintf(why, WHY_MAX, "not enough memory for COMMAND.COM (%luK needed, %luK free): set full_memory",
                 SHELL_NEEDS / 1024, largestFree() / 1024);
        teletype(why);
        teletype("\n");
        return -1;
    }
    /* Ctrl-C stays with the network stack's handler, which ignores it, as in
       mTCP's own shell-outs: the program can't be stopped with Ctrl-C unless
       it installs its own handler.
       The program inherits our handles. Nobody can type into it, so give it
       NUL as standard input: a program that reads stdin (FIND with no file,
       MORE, a prompt) gets end-of-file and exits instead of waiting forever. */
    nul = open("NUL", O_RDONLY);
    if (nul >= 0) {
        savedIn = dup(0);
        dup2(nul, 0);
    }
    if (shell_before_run)
        shell_before_run();
    oldInt10 = _dos_getvect(0x10);
    _dos_setvect(0x10, int10Hook);
    if (args == NULL)
        code = spawnl(P_WAIT, comspec, comspec, "/C", cmd, NULL);
    else if (*args)
        code = spawnl(P_WAIT, prog, prog, args, NULL);
    else
        code = spawnl(P_WAIT, prog, prog, NULL);
    _dos_setvect(0x10, oldInt10);
    if (nul >= 0) {
        if (savedIn >= 0) {
            dup2(savedIn, 0);
            close(savedIn);
        }
        close(nul);
    }

    if (code == -1) {
        snprintf(why, WHY_MAX, "could not start %s: %s", args ? prog : comspec, strerror(errno));
        teletype(why);
        teletype("\n");
    }
    return code;
}

/* The next command line of *cmds, trimmed, into `out` (cut to SHELL_CMD_MAX);
   its full length, or 0 when there are no more. */
static unsigned nextCmd(const char **cmds, char *out)
{
    const char *p = *cmds + strspn(*cmds, " \t\r\n");
    unsigned n = strcspn(p, "\r\n"), len = n;

    while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
        len--;
    *cmds = p + n;
    memcpy(out, p, len < SHELL_CMD_MAX ? len : SHELL_CMD_MAX);
    out[len < SHELL_CMD_MAX ? len : SHELL_CMD_MAX] = 0;
    return len;
}

unsigned shell_count(const char *cmds, char *first)
{
    static char cmd[SHELL_CMD_MAX + 1];
    unsigned n = 0;

    *first = 0;
    while (cmds && nextCmd(&cmds, n ? cmd : first))
        n++;
    return n;
}

#define NOTES_ROOM 300                /* kept free for the list of commands not run */
#define EARLY_ROOM 700                /* most output kept from a command that worked */

int shell_run(ToolCtx *t, const char *cmds)
{
    static char cmd[SHELL_CMD_MAX + 1], why[WHY_MAX];
    const char *p = cmds;
    unsigned count = 0, i, len, left, room;
    int code = 0;

    while (p && (len = nextCmd(&p, cmd)) != 0) {
        if (len > SHELL_CMD_MAX) {
            sprintf(why, "command %u is too long (max %u characters)", count + 1, SHELL_CMD_MAX);
            return tool_fail(t, why);
        }
        count++;
    }
    if (count == 0)
        return tool_fail(t, "missing command");
    if (count > SHELL_CMDS_MAX) {
        sprintf(why, "too many commands (max %u)", SHELL_CMDS_MAX);
        return tool_fail(t, why);
    }

    for (i = 0, p = cmds; i < count; i++) {
        nextCmd(&p, cmd);
        if (i > 0 && tool_esc()) {
            t->showFrom = t->showTo = 0;  /* the transcript's note is about this, not the last output */
            tout_printf(t, "stopped by the user before: %s\n", cmd);
            strcpy(t->summary, "stopped by user");
            return TS_STOPPED;
        }
        if (count > 1)
            tout_printf(t, "> %s\n", cmd);
        if ((code = runOne(cmd, why)) == -1) {
            t->showFrom = t->showTo = 0;
            tout_printf(t, "%s\n", why);
            strncpy(t->summary, why, sizeof(t->summary) - 1);
            t->summary[sizeof(t->summary) - 1] = 0;
            return TS_ERROR;
        }
        tout_printf(t, "exit code %d\n", code);
        left = TOOL_OUT_MAX - t->len;
        left = left > NOTES_ROOM ? left - NOTES_ROOM : 0;
        room = code != 0 || i == count - 1 ? left : (left / 3 < EARLY_ROOM ? left / 3 : EARLY_ROOM);
        t->showFrom = t->len;
        if (total == 0)
            snapshot(t, room);
        else
            putOutput(t, room);
        t->showTo = t->len;
        if (code != 0)
            break;
        if (count > 1)
            tout_puts(t, "\n");
    }

    if (count == 1) {
        sprintf(t->summary, "exit code %d, %u lines of output", code, total ? countLines() : 0);
    } else if (code != 0 && i + 1 < count) {
        sprintf(t->summary, "stopped at %u of %u, exit code %d", i + 1, count, code);
        tout_printf(t, "\nstopped: command %u of %u failed; not run:\n", i + 1, count);
        while (++i < count) {
            nextCmd(&p, cmd);
            tout_printf(t, "  %s\n", cmd);
        }
    } else {
        sprintf(t->summary, "%u commands, exit code %d", count, code);
    }
    return TS_OK;
}
