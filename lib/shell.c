/* Claude-XT shell: see shell.h.
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
#include <dos.h>
#include <i86.h>
#include <bios.h>
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

/* The 80x25 text screen, right-trimmed, without trailing blank rows. */
static void snapshot(ToolCtx *t)
{
    const unsigned short __far *vram = (const unsigned short __far *)MK_FP(0xB800, 0);
    char line[82];                    /* 80 columns, newline, NUL */
    int row, last = -1, col, n;

    for (row = 0; row < 25; row++)
        for (col = 0; col < 80; col++)
            if ((vram[row * 80 + col] & 0xFF) > ' ')
                last = row;
    tout_puts(t, "(screen text)\n");
    for (row = 0; row <= last; row++) {
        n = 0;
        for (col = 0; col < 80; col++) {
            unsigned char c = (unsigned char)vram[row * 80 + col];
            line[col] = c < ' ' ? ' ' : (char)c;
            if (c > ' ')
                n = col + 1;
        }
        line[n] = '\n';
        line[n + 1] = 0;
        tout_puts(t, line);
    }
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
    while (*cmd == ' ')
        cmd++;
    n = strcspn(cmd, " /;=,\t");
    if (n == 0 || n >= sizeof(name))
        return NULL;
    memcpy(name, cmd, n);
    name[n] = 0;
    strupr(name);
    args = cmd + n;
    while (*args == ' ')
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

int shell_run(ToolCtx *t, const char *cmd, int holdSecs)
{
    static char prog[80];
    const char *args;
    volatile unsigned long __far *ticks = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
    const char *comspec = getenv("COMSPEC");
    static char buf[TAIL_MAX + 1];    /* too big for our 4 KB stack */
    unsigned long start;
    unsigned i;
    int code;

    if (cmd == NULL || !*cmd)
        return tool_fail(t, "missing command");
    if (strlen(cmd) > SHELL_CMD_MAX)
        return tool_fail(t, "command too long (max 120)");
    if (comspec == NULL || !*comspec)
        comspec = "C:\\COMMAND.COM";

    teletype("C:\\> ");
    teletype(cmd);
    teletype("\n");

    headLen = tailStart = tailLen = 0;
    total = 0;
    /* Ctrl-C stays with the network stack's handler, which ignores it, as in
       mTCP's own shell-outs: the program can't be stopped with Ctrl-C unless
       it installs its own handler. */
    args = direct(cmd, prog);
    oldInt10 = _dos_getvect(0x10);
    _dos_setvect(0x10, int10Hook);
    if (args == NULL)
        code = spawnl(P_WAIT, comspec, comspec, "/C", cmd, NULL);
    else if (*args)
        code = spawnl(P_WAIT, prog, prog, args, NULL);
    else
        code = spawnl(P_WAIT, prog, prog, NULL);
    _dos_setvect(0x10, oldInt10);

    if (code == -1) {
        sprintf(buf, "could not start %s: %s", args ? prog : comspec, strerror(errno));
        teletype(buf);
        teletype("\n");
        return tool_fail(t, buf);
    }

    tout_printf(t, "exit code %d\n", code);
    if (total == 0) {
        snapshot(t);
    } else {
        memcpy(buf, head, headLen);
        buf[headLen] = 0;
        tout_puts(t, buf);
        if (total > HEAD_MAX + TAIL_MAX)
            tout_printf(t, "\n...(%lu bytes omitted)...\n", total - HEAD_MAX - TAIL_MAX);
        for (i = 0; i < tailLen; i++)
            buf[i] = tail[(tailStart + i) % TAIL_MAX];
        buf[tailLen] = 0;
        tout_puts(t, buf);
    }
    sprintf(t->summary, "exit code %d, %u lines of output", code, total ? countLines() : 0);

    sprintf(buf, "\n-- exit code %d - any key returns to Claude --", code);
    teletype(buf);
    while (_bios_keybrd(_KEYBRD_READY))
        _bios_keybrd(_KEYBRD_READ);
    start = *ticks;
    while (!_bios_keybrd(_KEYBRD_READY) && *ticks - start < (unsigned long)holdSecs * 182 / 10)
        ;
    if (_bios_keybrd(_KEYBRD_READY))
        _bios_keybrd(_KEYBRD_READ);
    return TS_OK;
}
