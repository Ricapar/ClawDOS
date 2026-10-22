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
   a helper called from here never ran in testing (most likely the large
   model's SS == DGROUP assumption, which doesn't hold on a foreign stack). */
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
    char line[81];
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

int shell_run(ToolCtx *t, const char *cmd, int holdSecs)
{
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
    oldInt10 = _dos_getvect(0x10);
    _dos_setvect(0x10, int10Hook);
    code = spawnl(P_WAIT, comspec, comspec, "/C", cmd, NULL);
    _dos_setvect(0x10, oldInt10);

    if (code == -1) {
        sprintf(buf, "could not start %s: %s", comspec, strerror(errno));
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
