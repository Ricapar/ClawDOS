/* CLAUDE.EXE tool calls: the bridge's TOOL/ARG/CALL lines in, OUT/DONE out
 * (docs/PROTOCOL.md), with safe mode asking before anything changes. */
#ifndef CXT_TOOLS_H
#define CXT_TOOLS_H

void tools_init(int safe, int unattended);
int  tools_safe(void);
void tools_toggle_safe(void);

/* After STOP is sent and until the reply ends, calls that still arrive are
   answered "stopped" without running. */
void tools_stopping(int on);

/* Forget a half-received call and any STOP in progress (the link was lost). */
void tools_reset(void);

/* Handle a protocol line if it belongs to a tool call; returns 1 if consumed.
   A CALL runs the tool before returning. */
int  tools_line(const char *verb, const char *payload);

#endif
