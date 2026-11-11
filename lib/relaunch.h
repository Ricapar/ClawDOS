/* ClawDOS relaunch: how HARNESS.EXE hands a `run` to the launcher, CLAWDOS.EXE,
 * and gets the result back, so the program runs with the memory CLAWDOS held.
 *
 * CLAWDOS writes RUN.TXT, saves its state (RESUME.TXT, SCREEN.SAV) and exits
 * with RELAUNCH_EXIT. The launcher, which stays resident while CLAWDOS is
 * gone, runs the commands with lib/shell.c, writes RUN.OUT and starts
 * CLAWDOS -resume, which reopens the conversation, shows the result and
 * carries the turn on. Both programs link this file.
 */
#ifndef CLAWDOS_RELAUNCH_H
#define CLAWDOS_RELAUNCH_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RELAUNCH_EXIT    200
#define RELAUNCH_DIR     "C:\\CLAWDOS"
#define RELAUNCH_RUN     RELAUNCH_DIR "\\RUN.TXT"     /* the command lines */
#define RELAUNCH_OUT     RELAUNCH_DIR "\\RUN.OUT"     /* "status N ticks T show A B\n", the summary line, the output */
#define RELAUNCH_STATE   RELAUNCH_DIR "\\RESUME.TXT"  /* CLAWDOS's own "key value" lines */
#define RELAUNCH_SCREEN  RELAUNCH_DIR "\\SCREEN.SAV"  /* the chat screen while a program has the display */

int relaunch_write_run(const char *cmds);
int relaunch_read_run(char *cmds, unsigned max);                         /* -1 if there is no file */
/* status is the TS_ code shell_run returned, ticks how long it took; show
   carries t->showFrom and showTo. */
int relaunch_write_out(int status, unsigned long ticks, const ToolCtx *t);
int relaunch_read_out(ToolCtx *t, int *status, unsigned long *ticks);    /* into a reset t; -1 if no file */

#ifdef __cplusplus
}
#endif

#endif
