/* ClawDOS sysinfo tool: a description of the machine for the model. */
#ifndef CLAWDOS_SYSINFO_H
#define CLAWDOS_SYSINFO_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Everything but the IP address, which the caller (who owns the network
   stack) appends. */
int tool_sysinfo(ToolCtx *t);

#ifdef __cplusplus
}
#endif

#endif
