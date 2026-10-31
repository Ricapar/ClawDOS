/* CLAWDOS.EXE tool calls: the bridge's TOOL/ARG/CALL lines in, OUT/DONE out
 * (docs/PROTOCOL.md), with safe mode asking before anything changes. */
#ifndef CLAWDOS_TOOLS_H
#define CLAWDOS_TOOLS_H

#include "toolapi.h"

void tools_init(int safe, int unattended);
int  tools_safe(void);
void tools_toggle_safe(void);

/* After STOP is sent and until the reply ends, calls that still arrive are
   answered "stopped" without running. */
void tools_stopping(int on);

/* Forget a half-received call and any STOP in progress (the link was lost). */
void tools_reset(void);

/* Handle a protocol line if it belongs to a tool call; returns 1 if consumed.
   A CALL runs the tool before returning. (The old bridge path.) */
int  tools_line(const char *verb, const char *payload);

/* Run one tool call: `input` is the tool_use input object (UTF-8 JSON, len
   bytes). Shows the header, asks in safe mode, runs the executor. `t` holds the
   CP437 output; returns TS_*. */
int  tools_call(const char *name, const char *input, unsigned len, ToolCtx *t);
/* The text the model gets for a finished call: the output when ok, else the
   fixed error/denied/stopped sentences (the error one prefixed to the output). */
const char *tools_result_text(int status, const ToolCtx *t);

#endif
