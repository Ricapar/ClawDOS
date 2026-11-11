/* CLAWDOS.EXE: the launcher the user types. It starts HARNESS.EXE (the chat
 * client itself) and stays resident while it runs, so that a program the
 * model runs can have the memory the harness holds.
 *
 * CLAWDOS passes its own command line to HARNESS (plus -launched). When the
 * harness hands over a run (exit code RELAUNCH_EXIT, lib/relaunch.h) only this
 * program is left in memory: it runs the commands with lib/shell.c, exactly
 * as the harness's run tool would, writes the captured output and starts
 * HARNESS -resume, which carries the conversation on. Any other exit code is
 * the harness quitting, and CLAWDOS quits with it. A run that asks for a
 * screenshot leaves the screen (or the copy taken at shot_at) in SHOT.RAW;
 * the resumed harness makes the PNG, so the encoder never sits in here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dos.h>
#include <i86.h>
#include "shell.h"
#include "shot.h"
#include "relaunch.h"
#include "ticks.h"

#define TAIL_MAX 126                  /* a DOS command tail */

static char exe[80];                  /* HARNESS.EXE, beside us */
static char args[TAIL_MAX + 16];
static char cmds[SHELL_CMDS_MAX * (SHELL_CMD_MAX + 2) + 32];   /* and the shot line */

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
    strcat(exe, "HARNESS.EXE");
}

/* Ctrl-C while a screenshot's timer hook is in: ignored, as the harness's
   network stack ignores it, so DOS cannot end us with the hook still in INT
   1Ch's chain (exit's atexit takes it out; a DOS abort would not). */
static void __interrupt __far noBreak(void)
{
}

/* The picture as the program left it (HARNESS sets its own mode later) or as
   shot_at took it, after its ShotInfo, into SHOT_RAW for the resumed harness. */
static int saveShot(ShotInfo *info)
{
    const unsigned char __far *frame = shot_frame(info);
    int fd = open(SHOT_RAW, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
    unsigned n1 = 0, n2 = 0;
    int rc;

    if (fd < 0)
        return -1;
    /* _dos_write: we are small model, and the frame is far */
    rc = _dos_write(fd, (const void __far *)info, sizeof *info, &n1)
         || _dos_write(fd, frame, SHOT_BYTES, &n2) || n1 != sizeof *info || n2 != SHOT_BYTES ? -1 : 0;
    close(fd);
    if (rc)
        remove(SHOT_RAW);
    return rc;
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
            printf("CLAWDOS: cannot run %s\n", exe);
            return 1;
        }
        if (code != RELAUNCH_EXIT) {
            /* CLAWDOS deletes the state once it has resumed: still there, the
               conversation waits (the network or the tunnel was down). */
            if (code != 0 && resuming && access(RELAUNCH_STATE, 0) == 0)
                printf("CLAWDOS: the conversation was not resumed (HARNESS left with code %d).\n"
                       "      Fix what it said, then CLAWDOS -resume carries it on.\n", code);
            return code;
        }
        {
            ToolCtx t;
            ShotInfo shot;
            int status, wantShot = 0;
            unsigned ticks = 0;
            const char *run = cmds;
            unsigned long start = TICKS_NOW;

            tout_reset(&t);
            remove(SHOT_RAW);
            if (relaunch_read_run(cmds, sizeof cmds)) {
                status = tool_fail(&t, "CLAWDOS: no " RELAUNCH_RUN);
            } else {
                if (!strncmp(cmds, RELAUNCH_SHOT, sizeof RELAUNCH_SHOT - 1)) {
                    char *p = cmds + sizeof RELAUNCH_SHOT - 1;
                    ticks = (unsigned)strtoul(p, &p, 10);
                    shot.force = (unsigned char)strtoul(p, &p, 10);
                    shot.color = (unsigned char)strtoul(p, &p, 10);
                    wantShot = 1;
                    run = cmds + strcspn(cmds, "\n");
                    if (ticks)
                        shot_arm(ticks);
                }
                if (ticks) {
                    void (__interrupt __far *oldBreak)() = _dos_getvect(0x23);
                    _dos_setvect(0x23, noBreak);
                    status = shell_run(&t, run);
                    shot_disarm();
                    _dos_setvect(0x23, oldBreak);
                } else {
                    status = shell_run(&t, run);
                }
            }
            if (wantShot) {
                shot_disarm();
                if (status == TS_OK && saveShot(&shot))
                    tout_puts(&t, "\n(no screenshot: CLAWDOS cannot write " SHOT_RAW ")\n");
                shot_release();
            }
            if (relaunch_write_out(status, ticks_elapsed(TICKS_NOW, start), &t))
                printf("CLAWDOS: cannot write %s\n", RELAUNCH_OUT);
        }
        strcpy(args, "-resume -launched");
        resuming = 1;
    }
}
