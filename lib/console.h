/* Claude-XT console: a scrolling, word-wrapping text stream on top of screen.h.
 * Text is written at the bottom of the scrolling area (above any footer, see
 * scr_footer) like a terminal; lines that fill up wrap at the last space.
 * Margins indent wrapped text, e.g. to hang it under a bullet. A mark lets an
 * editor rewind and redraw text it wrote (e.g. an input line after a backspace).
 */
#ifndef CXT_CONSOLE_H
#define CXT_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

void con_init(void);                 /* call after scr_init(); clears screen */
void con_attr(unsigned char attr);   /* attribute for following text */
void con_putc(unsigned char c);      /* '\n' = new line */
void con_write(const char *s);
void con_newline(void);
void con_ensure_blank_line(void);    /* newline unless at the start of an empty line */
int  con_row(void);
int  con_col(void);

/* Text runs from column `left` to before column `right` on new lines (the
   first line of a paragraph starts wherever the text already is). */
void con_margins(int left, int right);

/* Write a full prepared row (SCR_COLS cells) on a line of its own. */
void con_line(const unsigned short *cells);

/* Call before growing the footer to `footer_rows`: scrolls the text up if it
   would end up underneath. */
void con_make_room(int footer_rows);

/* Whether the hardware cursor follows the text (default on). */
void con_track_cursor(int on);

void con_mark(void);                 /* remember the current position */
int  con_rewind(void);               /* erase everything after the mark and return
                                        to it; 0 if the mark scrolled off */

#ifdef __cplusplus
}
#endif

#endif
