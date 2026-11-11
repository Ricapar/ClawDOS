/* ClawDOS memories: notes the model keeps across conversations as
 * C:\MEMORY\NAME.TXT, through memory_list, memory_read and memory_write. The
 * first line of a memory is its summary. These tools never ask the user.
 * Skills (C:\SKILLS\*.TXT) are listed here too, for the system prompt: the
 * build ships them, the model reads them with the read tool, and the memory
 * tools never reach them.
 */
#ifndef CLAWDOS_MEMORY_H
#define CLAWDOS_MEMORY_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_DIR      "C:\\MEMORY"
#define SKILL_DIR    "C:\\SKILLS"
#define MEM_MAX      4000            /* bytes: one memory fits one tool reply */
#define MEM_SUMMARY  60              /* longest summary shown by memory_list */

int  mem_name_ok(const char *name);  /* 1-8 of A-Z a-z 0-9 _ - */
/* The first non-blank line of text[0..n), trimmed and cut to MEM_SUMMARY
   characters ("..." marks a cut); out holds MEM_SUMMARY + 1. */
void mem_summary(const char *text, unsigned n, char *out);

/* For the memory manager (F4): the memories in C:\MEMORY, sorted by name.
   mem_scan fills up to `max` entries and returns how many it found (all of
   them, even past max). mem_load reads one into buf (at most max - 1 bytes,
   CRs dropped, up to a DOS EOF mark, NUL-terminated) and returns its length,
   or -1; *cut is set when the file held more. mem_delete returns 0 when the
   file is gone. */
typedef struct MemEntry {
    char name[9];
    unsigned long size;
    char summary[MEM_SUMMARY + 1];
} MemEntry;
int  mem_scan(MemEntry *out, int max);
int  mem_load(const char *name, char *buf, unsigned max, int *cut);
int  mem_delete(const char *name);

int tool_memory_list(ToolCtx *t);
int tool_memory_read(ToolCtx *t);
int tool_memory_write(ToolCtx *t);

/* For the system prompt: one line per C:\SKILLS\*.TXT (its whole path, its
   size and its first line), nothing when there are none. TS_OK, or TS_STOPPED
   after an Esc. */
int skill_list(ToolCtx *t);

#ifdef __cplusplus
}
#endif

#endif
