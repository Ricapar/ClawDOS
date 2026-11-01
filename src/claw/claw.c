/* CLAW.EXE: start CLAWDOS.EXE and stay resident while it runs, so that a
 * program the model runs can have the memory CLAWDOS holds.
 *
 * CLAW passes its own command line to CLAWDOS (plus -launched). When CLAWDOS
 * hands over a run (exit code RELAUNCH_EXIT, lib/relaunch.h) only this
 * program is left in memory: it runs the commands with lib/shell.c, exactly
 * as CLAWDOS's run tool would, writes the captured output and starts
 * CLAWDOS -resume, which carries the conversation on. Any other exit code is
 * CLAWDOS quitting, and CLAW quits with it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <io.h>
#include <dos.h>
#include <i86.h>
#include "shell.h"
#include "relaunch.h"
#include "ticks.h"

#define TAIL_MAX 126                  /* a DOS command tail */

static char exe[80];                  /* CLAWDOS.EXE, beside us */
static char args[TAIL_MAX + 16];
static char cmds[SHELL_CMDS_MAX * (SHELL_CMD_MAX + 2) + 1];

/* Our command tail as DOS gave it to us, from the PSP: spawnl passes a single
   argument through unchanged, so CLAWDOS sees exactly what the user typed. */
static void ownTail(char *out)
{
    unsigned char __far *psp = (unsigned char __far *)MK_FP(_psp, 0x80);
    unsigned n = psp[0];

    if (n > TAIL_MAX)
        n = TAIL_MAX;
    _fmemcpy(out, psp + 1, n);
    out[n] = 0;
    while (n && (out[n - 1] == '\r' || out[n - 1] == ' '))
        out[--n] = 0;
}

static void findExe(const char *argv0)
{
    const char *slash = argv0 ? strrchr(argv0, '\\') : NULL;

    if (slash && slash - argv0 < (int)sizeof exe - 13) {
        memcpy(exe, argv0, slash - argv0 + 1);
        exe[slash - argv0 + 1] = 0;
    }
    strcat(exe, "CLAWDOS.EXE");
}

int main(int argc, char *argv[])
{
    int code, resuming = 0;

    (void)argc;
    findExe(argv[0]);
    ownTail(args);
    strcat(args, " -launched");
    for (;;) {
        code = spawnlp(P_WAIT, exe, exe, args, NULL);
        if (code == -1) {
            printf("CLAW: cannot run %s\n", exe);
            return 1;
        }
        if (code != RELAUNCH_EXIT) {
            /* CLAWDOS deletes the state once it has resumed: still there, the
               conversation waits (the network or the tunnel was down). */
            if (code != 0 && resuming && access(RELAUNCH_STATE, 0) == 0)
                printf("CLAW: the conversation was not resumed (CLAWDOS left with code %d).\n"
                       "      Fix what it said, then CLAW -resume carries it on.\n", code);
            return code;
        }
        {
            ToolCtx t;
            int hold = 5, status;
            unsigned long start = TICKS_NOW;

            tout_reset(&t);
            if (relaunch_read_run(cmds, sizeof cmds, &hold))
                status = tool_fail(&t, "CLAW: no " RELAUNCH_RUN);
            else
                status = shell_run(&t, cmds, hold);
            if (relaunch_write_out(status, ticks_elapsed(TICKS_NOW, start), &t))
                printf("CLAW: cannot write %s\n", RELAUNCH_OUT);
        }
        strcpy(args, "-resume -launched");
        resuming = 1;
    }
}
