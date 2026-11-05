/* The memory manager (F4 or /memories): a full screen for looking through
 * C:\MEMORY and deleting memories, without Claude. */
#ifndef CLAWDOS_MEMUI_H
#define CLAWDOS_MEMUI_H

/* Takes the screen until Esc, then gives the chat back. `accent` colours the
   titles; `idle` runs while waiting for keys (keep the network serviced).
   Returns how many memories were deleted, or -1 if there was no memory to
   save the screen in. */
int memui_run(unsigned char accent, void (*idle)(void));

#endif
