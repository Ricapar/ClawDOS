/* Claude-XT shell: run a DOS command line for the model, on a plain DOS
 * screen, and capture what it prints. */
#ifndef CXT_SHELL_H
#define CXT_SHELL_H

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
   it is used instead. Afterwards the last exit code is shown until a key is
   pressed or holdSecs pass. The caller saves and restores the screen around
   this. */
int shell_run(ToolCtx *t, const char *cmds, int holdSecs);

/* How many commands `cmds` holds, and its first one (trimmed) in `first`
   (SHELL_CMD_MAX + 1 bytes), for showing the call. */
unsigned shell_count(const char *cmds, char *first);

#ifdef __cplusplus
}
#endif

#endif
