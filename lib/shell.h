/* Claude-XT shell: run a DOS command line for the model, on a plain DOS
 * screen, and capture what it prints. */
#ifndef CXT_SHELL_H
#define CXT_SHELL_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_CMD_MAX 120

/* Run `cmd` with COMMAND.COM /C. Output written through the BIOS (which is
   where DOS console output ends up) is captured into t; if there was none,
   the final screen text is used instead. Afterwards the exit code is shown
   until a key is pressed or holdSecs pass. The caller saves and restores
   the screen around this. */
int shell_run(ToolCtx *t, const char *cmd, int holdSecs);

#ifdef __cplusplus
}
#endif

#endif
