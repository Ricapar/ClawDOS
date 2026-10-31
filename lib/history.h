/* The conversation history: a file holding the elements of the API's
 * "messages" array (complete JSON objects separated by ",\r\n"), appended as
 * the conversation goes and copied into each request. Nothing is kept in
 * memory, so a long session costs disk, not the 640K.
 *
 * A turn may be cut short (Esc, lost link, API error): the file is then
 * truncated back to the last complete message or to where the turn started.
 */
#ifndef CLAWDOS_HISTORY_H
#define CLAWDOS_HISTORY_H

#include "sink.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open (create or append). An existing file is scanned so a dangling separator
   or a half-written message after the last complete one is truncated away.
   0 ok, -1 error. */
int  hist_open(const char *path);
void hist_close(void);
long hist_size(void);                 /* bytes hist_copy would produce */
/* Append raw bytes of the current message. The first write after a boundary
   (in a non-empty file) first emits the ",\r\n" separator. */
int  hist_write(const char *p, unsigned n);
int  hist_puts(const char *z);
int  hist_message_done(void);         /* the message is complete: new boundary */
void hist_turn_begin(void);           /* remember where this turn started */
int  hist_rollback_message(void);     /* truncate to the last boundary */
int  hist_rollback_turn(void);        /* truncate to the turn start */
int  hist_turn_messages(void);        /* complete messages since turn_begin */
int  hist_copy(Sink *s);              /* file contents (no brackets) into the sink */

#ifdef __cplusplus
}
#endif

#endif
