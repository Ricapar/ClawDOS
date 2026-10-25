/* Claude-XT screen: direct 80x25 color text-mode output for CGA (and compatibles).
 *
 * On a real CGA the whole screen scrolls in hardware by moving the CRTC start
 * address down through the card's 16 KB of video RAM. That makes scrolling
 * cost one row of writes instead of 24, and leaves the rows above the screen
 * as free scrollback (25 to 77 of them). The display doesn't wrap at the end
 * of VRAM, so near the end the screen is moved back to the start. Writes wait for horizontal retrace
 * so the IBM CGA doesn't show "snow"; scr_set_snowcheck(0) turns that off for
 * cards that don't need it. Other adapters fall back to software scrolling.
 *
 * A footer of up to SCR_FOOT_MAX rows can be pinned to the bottom of the
 * screen (an input box, a status line); scr_scroll() then scrolls only the
 * rows above it. With hardware scrolling the footer has to be rewritten one
 * row further on in VRAM each time, so that happens in the vertical blanking
 * interval, together with the CRTC update, and the footer never visibly moves.
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
void scr_set_snowcheck(int on);
void scr_set_hwscroll(int on);       /* 0: scroll by copying (call right after scr_init) */
int  scr_has_hwscroll(void);

void scr_putc(int row, int col, unsigned char ch, unsigned char attr);
void scr_puts(int row, int col, const char *s, unsigned char attr);
void scr_putn(int row, int col, const unsigned short *cells, int n);  /* char|attr<<8 */
void scr_fill(int row, int col, int n, unsigned char ch, unsigned char attr);
void scr_clear(unsigned char attr);

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

/* Watching for other writers: keep a copy of all of video memory (`shadow`,
   SCR_SAVE_BYTES, filled from VRAM now) that every write here also updates.
   scr_check compares the next `cells` of VRAM with it (reading VRAM snows on
   a CGA, so this is a debugging aid) and returns the number of differing cells
   found, describing the first run of them in `d`. scr_repair puts the copy back. */
typedef struct {
    unsigned first, last;            /* VRAM cells (0..8191) of the first differing run */
    unsigned found[8], expected[8];  /* from `first` on */
} ScrDiff;
void scr_watch(unsigned short __far *shadow);
int  scr_check(unsigned cells, ScrDiff *d);
void scr_repair(void);
unsigned scr_top(void);              /* VRAM cell of live row 0 */

#ifdef __cplusplus
}
#endif

#endif
