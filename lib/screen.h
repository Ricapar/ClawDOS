/* Claude-XT screen: direct 80x25 color text-mode output for CGA (and compatibles).
 *
 * On a real CGA the whole screen scrolls in hardware by moving the CRTC start
 * address through the card's 16 KB of video RAM, which wraps around. That
 * makes scrolling cost one row of writes instead of 24, and leaves the last
 * ~77 lines in VRAM as free scrollback. Writes wait for horizontal retrace
 * so the IBM CGA doesn't show "snow"; scr_set_snowcheck(0) turns that off for
 * cards that don't need it. Other adapters fall back to software scrolling.
 *
 * Rows/cols are logical screen positions (0..24, 0..79).
 */
#ifndef CXT_SCREEN_H
#define CXT_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#define SCR_COLS 80
#define SCR_ROWS 25

/* CGA attribute helpers: SCR_ATTR(fg, bg) */
#define SCR_ATTR(fg, bg) ((unsigned char)(((bg) << 4) | (fg)))
enum {
    C_BLACK, C_BLUE, C_GREEN, C_CYAN, C_RED, C_MAGENTA, C_BROWN, C_LIGHTGRAY,
    C_DARKGRAY, C_LIGHTBLUE, C_LIGHTGREEN, C_LIGHTCYAN, C_LIGHTRED,
    C_LIGHTMAGENTA, C_YELLOW, C_WHITE
};

void scr_init(void);                 /* 80x25 color text, cleared */
void scr_done(void);                 /* back to a clean DOS screen */
void scr_set_snowcheck(int on);
int  scr_has_hwscroll(void);

void scr_putc(int row, int col, unsigned char ch, unsigned char attr);
void scr_puts(int row, int col, const char *s, unsigned char attr);
void scr_putn(int row, int col, const unsigned short *cells, int n);  /* char|attr<<8 */
void scr_fill(int row, int col, int n, unsigned char ch, unsigned char attr);
void scr_clear(unsigned char attr);

void scr_scroll(unsigned char attr); /* whole screen up one row, new bottom row blank */

/* Scrollback: view `lines` rows above the live screen (0 = live). Returns the
   amount actually applied, which is limited by the history available. */
int  scr_view(int lines);
int  scr_viewing(void);              /* current scrollback offset */

void scr_cursor(int row, int col);
void scr_cursor_visible(int on);

#ifdef __cplusplus
}
#endif

#endif
