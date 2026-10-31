/* ClawDOS memories: notes the model keeps across conversations as
 * C:\MEMORY\NAME.TXT, through memory_list, memory_read and memory_write. The
 * first line of a memory is its summary. These tools never ask the user.
 */
#ifndef CLAWDOS_MEMORY_H
#define CLAWDOS_MEMORY_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_DIR      "C:\\MEMORY"
#define MEM_MAX      4000            /* bytes: one memory fits one tool reply */
#define MEM_SUMMARY  60              /* longest summary shown by memory_list */

int  mem_name_ok(const char *name);  /* 1-8 of A-Z a-z 0-9 _ - */
/* The first non-blank line of text[0..n), trimmed and cut to MEM_SUMMARY
   characters ("..." marks a cut); out holds MEM_SUMMARY + 1. */
void mem_summary(const char *text, unsigned n, char *out);

int tool_memory_list(ToolCtx *t);
int tool_memory_read(ToolCtx *t);
int tool_memory_write(ToolCtx *t);

#ifdef __cplusplus
}
#endif

#endif
