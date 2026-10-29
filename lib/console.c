/* Claude-XT console: see console.h. */

#include <string.h>
#include "screen.h"
#include "console.h"

static int row, col;
static int lm, rm = SCR_COLS;           /* margins: text in [lm, rm) */
static int track = 1;                   /* move the hardware cursor */
static unsigned char attr = SCR_ATTR(C_LIGHTGRAY, C_BLACK);
static unsigned short line[SCR_COLS];   /* mirror of the current row */
static int soft_wrapped;                /* current row began with a word wrap */
static int used;                        /* anything written on the current row */

static int mark_row = -1, mark_col;
static unsigned short mark_line[SCR_COLS];
static int mark_soft;

static void blank_line(unsigned short *cells)
{
    int i;
    for (i = 0; i < SCR_COLS; i++)
        cells[i] = ' ' | (attr << 8);
}

static void place_cursor(void)
{
    if (track)
        scr_cursor(row, col < SCR_COLS ? col : SCR_COLS - 1);
}

static int last_row(void)
{
    return SCR_ROWS - 1 - scr_footer_rows();
}

static void scroll_up(void)
{
    scr_scroll(attr);
    if (mark_row >= 0)
        mark_row--;
}

static void next_row(void)
{
    if (row >= last_row())
        scroll_up();
    else
        row++;
    blank_line(line);
    col = lm;
    used = 0;
}

void con_init(void)
{
    row = 0;
    col = lm;
    soft_wrapped = 0;
    used = 0;
    mark_row = -1;
    blank_line(line);
    scr_clear(attr);
    place_cursor();
}

void con_attr(unsigned char a) { attr = a; }
int  con_row(void) { return row; }
int  con_col(void) { return col; }
void con_track_cursor(int on) { track = on; }

void con_margins(int left, int right)
{
    lm = left;
    rm = right;
}

void con_make_room(int footer_rows)
{
    while (row > SCR_ROWS - 1 - footer_rows) {
        scroll_up();
        row--;
    }
}

void con_newline(void)
{
    next_row();
    soft_wrapped = 0;
    place_cursor();
}

void con_ensure_blank_line(void)
{
    if (used)
        con_newline();
}

void con_line(const unsigned short *cells)
{
    con_ensure_blank_line();
    memcpy(line, cells, sizeof(line));
    scr_putn(row, 0, line, SCR_COLS);
    con_newline();
}

static void wrap(void)
{
    int k = rm - 1;
    int n;

    while (k > lm && (line[k] & 0xFF) != ' ')
        k--;
    if (k <= lm || k == rm - 1) {
        next_row();                         /* no break point: hard wrap */
    } else {
        unsigned short tail[SCR_COLS];

        n = rm - 1 - k;                     /* move the partial word down */
        memcpy(tail, line + k + 1, n * sizeof(tail[0]));
        scr_fill(row, k + 1, n, ' ', attr);
        next_row();
        memcpy(line + lm, tail, n * sizeof(tail[0]));
        scr_putn(row, lm, line + lm, n);
        col = lm + n;
        used = 1;
    }
    soft_wrapped = 1;
}

void con_putc(unsigned char c)
{
    if (c == '\n') {
        con_newline();
        return;
    }
    if (c == '\r')
        return;
    if (col >= rm) {                        /* the row is full: break before this char, not
                                               when the last column filled (a NL may follow) */
        if (c == ' ') {                     /* a space at the margin is the break itself */
            next_row();
            soft_wrapped = 1;
            place_cursor();
            return;
        }
        wrap();
    }
    if (c == ' ' && col == lm && soft_wrapped)
        return;                             /* don't start a wrapped line with a space */
    used = 1;
    line[col] = c | (attr << 8);
    scr_putc(row, col, c, attr);
    col++;
    place_cursor();
}

void con_write(const char *s)
{
    while (*s)
        con_putc((unsigned char)*s++);
}

void con_mark(void)
{
    mark_row = row;
    mark_col = col;
    mark_soft = soft_wrapped;
    memcpy(mark_line, line, sizeof(line));
}

int con_rewind(void)
{
    int r;

    if (mark_row < 0)
        return 0;
    scr_fill(mark_row, mark_col, SCR_COLS - mark_col, ' ', attr);
    for (r = mark_row + 1; r <= row; r++)
        scr_fill(r, 0, SCR_COLS, ' ', attr);
    row = mark_row;
    col = mark_col;
    soft_wrapped = mark_soft;
    memcpy(line, mark_line, sizeof(line));
    place_cursor();
    return 1;
}
