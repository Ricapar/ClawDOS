/* Claude-XT console: a scrolling, word-wrapping text stream on top of screen.h.
 * Text is written at the bottom of the screen like a terminal; lines that fill
 * up wrap at the last space. A mark lets an editor rewind and redraw text it
 * wrote (e.g. an input line after a backspace).
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

void con_mark(void);                 /* remember the current position */
int  con_rewind(void);               /* erase everything after the mark and return
                                        to it; 0 if the mark scrolled off */

#ifdef __cplusplus
}
#endif

#endif
