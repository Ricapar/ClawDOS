/* ClawDOS search tool: find a piece of text in files, with line numbers,
 * without starting a program (FIND took 10-79 s a call on the DOS PC). */
#ifndef CLAWDOS_SEARCH_H
#define CLAWDOS_SEARCH_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Does `line` contain `text`? Ignoring case unless matchCase. */
int search_match(const char *line, const char *text, int matchCase);

int tool_search(ToolCtx *t);

#ifdef __cplusplus
}
#endif

#endif
