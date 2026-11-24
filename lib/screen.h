/* ClawDOS screen: direct 80x25 color text-mode output for CGA, EGA and VGA.
 *
 * The whole screen scrolls in hardware by moving the CRTC start address down
 * through the first 16 KB of video RAM (all a CGA has). That makes scrolling
 * cost one row of writes instead of 24, and leaves the rows above the screen
 * as free scrollback (25 to 77 of them). The display doesn't wrap at the end
 * of VRAM, so near the end the screen is moved back to the start. Writes wait
 * for horizontal retrace so the IBM CGA doesn't show "snow"; EGA/VGA need no
 * waits.
 *
 * A footer of up to SCR_FOOT_MAX rows can be pinned to the bottom of the
 * screen (an input box, a status line); scr_scroll() then scrolls only the
 * rows above it. With hardware scrolling the footer has to be rewritten one
 * row further on in VRAM each time, so that happens in the vertical blanking
 * interval, together with the CRTC update, and the footer never visibly moves.
 *
 * Rows/cols are logical screen positions (0..24, 0..79).
 */
#ifndef CLAWDOS_SCREEN_H
#define CLAWDOS_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#define SCR_COLS 80
#define SCR_ROWS 25
#define SCR_FOOT_MAX 10

/* CGA attribute helpers: SCR_ATTR(fg, bg) */
#define SCR_ATTR(fg, bg) ((unsigned char)(((bg) << 4) | (fg)))
enum {
    C_BLACK, C_BLUE, C_GREEN, C_CYAN, C_RED, C_MAGENTA, C_BROWN, C_LIGHTGRAY,
    C_DARKGRAY, C_LIGHTBLUE, C_LIGHTGREEN, C_LIGHTCYAN, C_LIGHTRED,
    C_LIGHTMAGENTA, C_YELLOW, C_WHITE
};

void scr_init(void);                 /* 80x25 color text, cleared */
void scr_done(void);                 /* back to a clean DOS screen */
int  scr_is_cga(void);               /* a CGA, not an EGA/VGA */

void scr_putc(int row, int col, unsigned char ch, unsigned char attr);
void scr_puts(int row, int col, const char *s, unsigned char attr);
void scr_putn(int row, int col, const unsigned short *cells, int n);  /* char|attr<<8 */
void scr_fill(int row, int col, int n, unsigned char ch, unsigned char attr);
void scr_clear(unsigned char attr);

/* Read back n cells (char|attr<<8) of a row from video memory. Reads are not
   synced to retrace, so they may snow on the IBM CGA: for tests only. */
void scr_getn(int row, int col, unsigned short *cells, int n);

void scr_scroll(unsigned char attr); /* rows above the footer up one, new row blank */

/* Pin the bottom `rows` rows (0..SCR_FOOT_MAX). Rows given up by a smaller
   footer are blanked; the caller draws the footer's contents. */
void scr_footer(int rows, unsigned char attr);
int  scr_footer_rows(void);

/* Scrollback: view `lines` rows above the live screen (0 = live). Returns the
   amount actually applied, which is limited by the history available. */
int  scr_view(int lines);
int  scr_viewing(void);              /* current scrollback offset */

void scr_cursor(int row, int col);
void scr_cursor_visible(int on);

/* Hand the display to something else (a graphics picture, a DOS program) and
   take it back with the text, scrollback and cursor as they were.
   scr_suspend saves video memory into `save` (SCR_SAVE_BYTES) and leaves a
   clean 80x25 text screen; scr_resume restores it. */
#define SCR_SAVE_BYTES 16384u
void scr_suspend(unsigned short __far *save);
void scr_resume(const unsigned short __far *save);

/* The scrolling state behind a saved screen, so that a fresh process (CLAWDOS
   restarted around a program run, see relaunch.h) can scr_resume it: take it
   with the save, set it back just before scr_resume. */
typedef struct ScrState {
    unsigned top;
    int history;
    unsigned cur_pos;
} ScrState;
void scr_state_get(ScrState *s);
void scr_state_set(const ScrState *s);

/* The ticker: a spinner glyph and a seconds counter in one row that the timer
   interrupt (INT 1Ch) keeps moving by itself, so they don't freeze while the
   program is busy connecting, sending, or reading the disk. The caller draws
   the row, then hands over: `glyphs` (8) cycle at glyphCol, one step every two
   ticks; the seconds go at secsCol, counting on from `elapsed` ticks as of the
   tick reading `at`; `tail` (the cells drawn after the digits) is redrawn when
   the number grows a digit. The interrupt skips any tick while screen calls
   are running or the screen is suspended. scr_ticker_off unhooks it (and so
   does exit). */
void scr_ticker(int row, int glyphCol, const unsigned char *glyphs, unsigned char glyphAttr,
                int secsCol, unsigned char secsAttr, unsigned long elapsed, unsigned long at,
                const unsigned short *tail, int tailLen);
void scr_ticker_off(void);
int  scr_ticker_on(void);

#ifdef __cplusplus
}
#endif

#endif
