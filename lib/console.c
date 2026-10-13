/* Claude-XT console: see console.h. */

#include <string.h>
#include "screen.h"
#include "console.h"

static int row, col;
static unsigned char attr = SCR_ATTR(C_LIGHTGRAY, C_BLACK);
static unsigned short line[SCR_COLS];   /* mirror of the current row */
static int soft_wrapped;                /* current row began with a word wrap */

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
    scr_cursor(row, col < SCR_COLS ? col : SCR_COLS - 1);
}

static void next_row(void)
{
    col = 0;
    if (row == SCR_ROWS - 1) {
        scr_scroll(attr);
        if (mark_row >= 0)
            mark_row--;
    } else {
        row++;
    }
    blank_line(line);
}

void con_init(void)
{
    row = col = 0;
    soft_wrapped = 0;
    mark_row = -1;
    blank_line(line);
    scr_clear(attr);
    place_cursor();
}

void con_attr(unsigned char a) { attr = a; }
int  con_row(void) { return row; }
int  con_col(void) { return col; }

void con_newline(void)
{
    next_row();
    soft_wrapped = 0;
    place_cursor();
}

void con_ensure_blank_line(void)
{
    if (col != 0)
        con_newline();
}

static void wrap(void)
{
    int k = SCR_COLS - 1;
    int n;

    while (k > 0 && (line[k] & 0xFF) != ' ')
        k--;
    if (k == 0 || k == SCR_COLS - 1) {
        next_row();                         /* no break point: hard wrap */
    } else {
        unsigned short tail[SCR_COLS];

        n = SCR_COLS - 1 - k;               /* move the partial word down */
        memcpy(tail, line + k + 1, n * sizeof(tail[0]));
        scr_fill(row, k + 1, n, ' ', attr);
        next_row();
        memcpy(line, tail, n * sizeof(tail[0]));
        scr_putn(row, 0, line, n);
        col = n;
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
    if (c == ' ' && col == 0 && soft_wrapped)
        return;                             /* don't start a wrapped line with a space */
    line[col] = c | (attr << 8);
    scr_putc(row, col, c, attr);
    if (++col == SCR_COLS)
        wrap();
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
