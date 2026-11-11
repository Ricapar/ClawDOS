/* ClawDOS shell: run a DOS command line for the model, on a plain DOS
 * screen, and capture what it prints. */
#ifndef CLAWDOS_SHELL_H
#define CLAWDOS_SHELL_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_CMD_MAX  120
#define SHELL_CMDS_MAX 12             /* commands in one call */

/* Run the command lines in `cmds` (one per line) in turn, each on its own
   or with COMMAND.COM /C, stopping after the first whose exit code isn't 0.
   Output written through the BIOS (which is where DOS console output ends
   up) is captured into t; if a command printed none, the screen text after
   it is used instead; t->showFrom..showTo marks that part of the last
   command that ran, for the transcript. It returns as soon as the last
   command ends. The caller saves and restores the screen around this. */
int shell_run(ToolCtx *t, const char *cmds);
/* Called just before each program starts, when set (lib/shot.c's timer). */
extern void (*shell_before_run)(void);

/* The text screen at vram (cols x 25 character and attribute words; 40 or
   80 columns), right-trimmed and without trailing blank rows: as many of the
   last rows as fit in `room` bytes. */
void shell_screen_text(ToolCtx *t, unsigned room, const unsigned char __far *vram, unsigned cols);

/* How many commands `cmds` holds, and its first one (trimmed) in `first`
   (SHELL_CMD_MAX + 1 bytes), for showing the call. */
unsigned shell_count(const char *cmds, char *first);

#ifdef __cplusplus
}
#endif

#endif
