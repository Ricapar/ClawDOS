/* ClawDOS edit tool: replace exact text in a text file, so the model can
 * change a few lines without sending the whole file again. The file is held
 * in memory with CRLF line ends folded to LF (written back as they were), and
 * replaced through a temporary file, so a full disk never leaves half of it.
 */
#ifndef CXT_EDIT_H
#define CXT_EDIT_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EDIT_FILE_MAX 60000u         /* bytes: the file must fit one segment */

/* Non-overlapping occurrences of `old` in `src`, left to right; *first is the
   offset of the first (left alone when there is none). */
unsigned edit_count(const char *src, const char *old, unsigned *first);

/* 1-based line number of offset `pos` in `src`. */
unsigned edit_line_of(const char *src, unsigned pos);

/* Lines in `s`: newlines, plus one for text after the last of them. */
unsigned edit_lines(const char *s);

/* `src` with `old` replaced by `repl` (every occurrence if `all`, else the
   first), handed to out() in pieces. out() returns nonzero to give up. */
typedef int (*EditOut)(void *ctx, const char *p, unsigned n);
int edit_replace(const char *src, const char *old, const char *repl, int all,
                 EditOut out, void *ctx);

int tool_edit(ToolCtx *t);

#ifdef __cplusplus
}
#endif

#endif
