/* The chat list (F6 or /chats): a full screen listing C:\CLAWDOS\CHATS,
 * newest first, for picking an earlier conversation to reopen or deleting
 * one. Like the memory manager (memui.h) it borrows the screen and gives the
 * chat back as it was. */
#ifndef CLAWDOS_CHATUI_H
#define CLAWDOS_CHATUI_H

#define CHATS_DIR "C:\\CLAWDOS\\CHATS"

/* Takes the screen until Esc or Enter. `current` is the open conversation's
   file (it is marked, and cannot be deleted here); `idle` runs while waiting
   for keys; `log` takes a line for CLAWDOS.LOG (deletions, and with
   logScreen the list as video memory shows it, for the boot tests). On Enter
   `chosen` (40 bytes) gets the picked chat's path. Returns 1 when one was
   picked, 0 without, -1 if there was no memory for the list. */
int chatui_run(unsigned char accent, void (*idle)(void), void (*log)(const char *line), int logScreen,
               const char *current, char *chosen);

#endif
