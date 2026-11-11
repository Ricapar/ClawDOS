/* The conversation history: a file holding the elements of the API's
 * "messages" array (complete JSON objects separated by ",\r\n"), appended as
 * the conversation goes and copied into each request. Nothing is kept in
 * memory, so a long session costs disk, not the 640K.
 *
 * A turn may be cut short (Esc, lost link, API error): the file is then
 * truncated back to the last complete message or to where the turn started.
 *
 * Writes are buffered (2 KB) and reach the disk when the buffer fills, when a
 * message is complete (hist_message_done, which also commits) and before the
 * file is read or closed (hist_reopen_last, hist_copy, hist_close). A rollback
 * cuts the file at a boundary, which is always on disk, so the bytes it holds
 * are dropped unwritten. After a failed write nothing more is taken and
 * hist_message_done fails until a rollback.
 */
#ifndef CLAWDOS_HISTORY_H
#define CLAWDOS_HISTORY_H

#include "sink.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open (create or append). An existing file is scanned so a dangling separator
   or a half-written message after the last complete one is truncated away.
   0 ok, -1 error (the file is left as it was if it could not be scanned). */
int  hist_open(const char *path);
#ifdef DOSUNIT
extern int hist_test_scan_reads;      /* >= 0: the scan's read after that many fails */
extern int hist_test_writes;          /* DOS writes made so far */
extern int hist_test_write_ok;        /* >= 0: writes after that many fail (a full disk), */
extern int hist_test_write_bad;       /* this many of them (0: all) */
#endif
void hist_close(void);
long hist_size(void);                 /* bytes hist_copy would produce */
/* Append raw bytes of the current message. The first write after a boundary
   (in a non-empty file) first emits the ",\r\n" separator. */
int  hist_write(const char *p, unsigned n);
int  hist_puts(const char *z);
/* The message is complete: it is written out and committed, and becomes the
   new boundary. -1 if it could not be written (no new boundary). */
int  hist_message_done(void);
void hist_turn_begin(void);           /* remember where this turn started */
int  hist_rollback_message(void);     /* truncate to the last boundary */
int  hist_rollback_turn(void);        /* truncate to the turn start */
int  hist_turn_messages(void);        /* complete messages since turn_begin */
/* Take the last n bytes off the last complete message (its closing "]}") so
   more can be appended to it; finish with hist_message_done. A later
   hist_rollback_turn restores the message if turn_begin preceded the reopen. */
int  hist_reopen_last(unsigned n);
/* File contents (no brackets) into the sink: 0, -1 the file could not be
   read (or written out first), -2 the sink failed. */
int  hist_copy(Sink *s);

#ifdef __cplusplus
}
#endif

#endif
