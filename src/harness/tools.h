/* HARNESS.EXE tool calls: a tool_use from the model in, its result out, with
 * safe mode asking before anything changes. */
#ifndef CLAWDOS_TOOLS_H
#define CLAWDOS_TOOLS_H

#include "toolapi.h"

void tools_init(int safe);
int  tools_safe(void);
void tools_toggle_safe(void);
/* Started by CLAWDOS.EXE (-launched): run hands its commands to the launcher and
   returns TS_RELAUNCH instead of running them here (see relaunch.h). */
void tools_set_launched(int on);
/* After the relaunch: the note under the Run header, as the call would have shown. */
void tools_show_result(int status, const ToolCtx *t);

/* Hand the screen over (scr_suspend into an EMS page or the heap; NULL when
   neither has room) and take it back, footer redrawn (ui_redraw). */
unsigned short __far *tools_save_screen(void);
void tools_restore_screen(unsigned short __far *save);

/* Run one tool call: `input` is the tool_use input object (UTF-8 JSON, len
   bytes). Shows the header, asks in safe mode, runs the executor. `t` holds the
   CP437 output; returns TS_*. */
int  tools_call(const char *name, const char *input, unsigned len, ToolCtx *t);
/* The text the model gets for a finished call: the output when ok, else the
   fixed error/denied/stopped sentences (the error one prefixed to the output). */
const char *tools_result_text(int status, const ToolCtx *t);

#endif
