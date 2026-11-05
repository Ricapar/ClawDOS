/* The memory manager: see memui.h.
 *
 * It borrows the screen the way the run tool does (tools_save_screen), so the
 * chat and its scrollback come back untouched. While it has the screen, the
 * scrolled-chat state is parked and row 0 is the top of video memory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "inlines.h"
#include "screen.h"
#include "memory.h"
#include "tools.h"
#include "memui.h"

#define MAX_MEMS   64                 /* listed; more are counted but not shown */
#define TEXT_MAX   (MEM_MAX * 2)      /* a memory bigger than this is shown cut */
#define MAX_LINES  400                /* wrapped lines of one memory */
#define WRAP_W     78

#define LIST_TOP   2                  /* first list/text row */
#define LIST_ROWS  20                 /* rows 2..21 */
#define TEXT_ROWS  21                 /* rows 2..22 */
#define MSG_ROW    23
#define HINT_ROW   24

#define A_TEXT     SCR_ATTR(C_LIGHTGRAY, C_BLACK)
#define A_BRIGHT   SCR_ATTR(C_WHITE, C_BLACK)
#define A_DIM      SCR_ATTR(C_DARKGRAY, C_BLACK)
#define A_SEL      SCR_ATTR(C_BLACK, C_LIGHTGRAY)
#define A_ERR      SCR_ATTR(C_LIGHTRED, C_BLACK)
#define A_GOOD     SCR_ATTR(C_LIGHTGREEN, C_BLACK)

#define KEY_ENTER  13
#define KEY_BS     8
#define KEY_ESC    27
#define SCAN_F4    0x3E
#define SCAN_HOME  0x47
#define SCAN_UP    0x48
#define SCAN_PGUP  0x49
#define SCAN_END   0x4F
#define SCAN_DOWN  0x50
#define SCAN_PGDN  0x51
#define SCAN_DEL   0x53

static unsigned char accent;
static void (*idleFn)(void);

static MemEntry *mems;
static int count, shown;              /* found, and listed (at most MAX_MEMS) */
static int sel, first;                /* selected entry, first one on screen */

static char *text;                    /* the memory being viewed */
static int textLen, textCut;
static unsigned short *lineStart;
static unsigned char *lineLen;
static int lines, topLine;

/* --- drawing --------------------------------------------------------------- */

static void blankRow(int row)
{
    scr_fill(row, 0, SCR_COLS, ' ', A_TEXT);
}

/* s at row, col; returns the column after it */
static int put(int row, int col, const char *s, unsigned char attr)
{
    scr_puts(row, col, s, attr);
    return col + (int)strlen(s);
}

static void title(const char *left, const char *right)
{
    blankRow(0);
    put(0, 1, left, accent);
    if (right)
        put(0, SCR_COLS - 1 - (int)strlen(right), right, A_DIM);
    scr_fill(1, 0, SCR_COLS, 0xC4, A_DIM);
}

static void message(const char *s, unsigned char attr)
{
    blankRow(MSG_ROW);
    if (s)
        put(MSG_ROW, 1, s, attr);
}

static void hint(const char *s)
{
    blankRow(HINT_ROW);
    put(HINT_ROW, 1, s, A_DIM);
}

static unsigned waitKey(void)
{
    while (!biosIsKeyReady())
        idleFn();
    return biosKeyRead();
}

/* --- the list ---------------------------------------------------------------- */

static void rescan(void)
{
    count = mem_scan(mems, MAX_MEMS);
    shown = count < MAX_MEMS ? count : MAX_MEMS;
    if (sel >= shown)
        sel = shown - 1;
    if (sel < 0)
        sel = 0;
}

static void listRow(int i)
{
    int row = LIST_TOP + i - first;
    unsigned char attr = i == sel ? A_SEL : A_TEXT;
    char line[SCR_COLS + 1];

    if (i >= shown) {
        blankRow(row);
        return;
    }
    sprintf(line, " %-8s %6lu  %-60.60s", mems[i].name, mems[i].size, mems[i].summary);
    blankRow(row);
    scr_puts(row, 1, line, attr);
}

static void listMessage(void)
{
    char buf[60];
    if (count > shown) {
        sprintf(buf, "Showing the first %d of %d memories.", shown, count);
        message(buf, A_DIM);
    } else {
        message(NULL, A_TEXT);
    }
}

static void drawList(void)
{
    char right[40];
    int i;

    sprintf(right, "C:\\MEMORY \xFA %d memor%s", count, count == 1 ? "y" : "ies");
    title("Memories", right);
    if (shown)                        /* column heads over the rule */
        put(1, 1, " Name       Size  Summary ", A_DIM);
    for (i = first; i < first + LIST_ROWS; i++)
        listRow(i);
    if (!shown)
        put(LIST_TOP + 1, 3, "No memories yet. Claude saves them with its memory_write tool.", A_TEXT);
    blankRow(LIST_TOP + LIST_ROWS);
    listMessage();
    hint(shown ? "\x18\x19 PgUp PgDn move \xFA Enter view \xFA Del delete \xFA Esc back to the chat"
               : "Esc back to the chat");
}

/* Move the selection; redraw just the two rows unless the list scrolls. */
static void moveTo(int to)
{
    int old = sel;

    if (to >= shown)
        to = shown - 1;
    if (to < 0)
        to = 0;
    sel = to;
    if (sel < first || sel >= first + LIST_ROWS) {
        first = sel < first ? sel : sel - LIST_ROWS + 1;
        for (int i = first; i < first + LIST_ROWS; i++)
            listRow(i);
        return;
    }
    listRow(old);
    listRow(sel);
}

/* Del: ask, then delete. Returns 1 if it went. */
static int confirmDelete(const char *name)
{
    char buf[60];
    unsigned key;
    unsigned char ch;

    sprintf(buf, "Delete %s? (y/n)", name);
    message(buf, A_ERR);
    key = waitKey();
    ch = (unsigned char)key;
    if (ch != 'y' && ch != 'Y') {
        sprintf(buf, "Kept %s.", name);
        message(buf, A_DIM);
        return 0;
    }
    if (mem_delete(name)) {
        sprintf(buf, "Couldn't delete %s (read-only disk?).", name);
        message(buf, A_ERR);
        return 0;
    }
    return 1;
}

/* --- viewing one ------------------------------------------------------------- */

static void wrapText(void)
{
    int s = 0;

    lines = 0;
    while (s < textLen && lines < MAX_LINES) {
        int n = 0, brk = -1;
        while (s + n < textLen && text[s + n] != '\n' && n < WRAP_W) {
            if (text[s + n] == ' ')
                brk = n;
            n++;
        }
        if (s + n < textLen && text[s + n] != '\n' && brk > 0)
            n = brk;                  /* break at the last space */
        lineStart[lines] = (unsigned short)s;
        lineLen[lines] = (unsigned char)n;
        lines++;
        s += n;
        if (s < textLen && (text[s] == '\n' || text[s] == ' '))
            s++;
    }
}

static void textRow(int i)
{
    unsigned short cells[SCR_COLS];
    int row = LIST_TOP + i - topLine, n = 0, c;

    blankRow(row);
    if (i >= lines)
        return;
    for (c = 0; c < lineLen[i]; c++) {
        unsigned char ch = (unsigned char)text[lineStart[i] + c];
        cells[n++] = (ch < 32 ? ' ' : ch) | (A_TEXT << 8);
    }
    if (n)
        scr_putn(row, 1, cells, n);
}

static void viewStatus(void)
{
    char buf[80];

    if (lines > TEXT_ROWS) {
        int last = topLine + TEXT_ROWS < lines ? topLine + TEXT_ROWS : lines;
        sprintf(buf, "Lines %d-%d of %d%s", topLine + 1, last, lines,
                textCut ? " \xFA cut: the file is longer than this shows" : "");
        message(buf, A_DIM);
    } else {
        message(textCut ? "Cut: the file is longer than this shows." : NULL, A_DIM);
    }
}

static void drawText(void)
{
    for (int i = topLine; i < topLine + TEXT_ROWS; i++)
        textRow(i);
    viewStatus();
}

static void scrollTo(int to)
{
    int most = lines > TEXT_ROWS ? lines - TEXT_ROWS : 0;
    if (to > most)
        to = most;
    if (to < 0)
        to = 0;
    if (to != topLine) {
        topLine = to;
        drawText();
    }
}

/* Enter: show the selected memory. Returns 1 if it was deleted. */
static int view(const MemEntry *m)
{
    char right[40];

    textLen = mem_load(m->name, text, TEXT_MAX + 1, &textCut);
    if (textLen < 0) {
        message("Couldn't read that memory.", A_ERR);
        return 0;
    }
    wrapText();
    topLine = 0;
    sprintf(right, "%lu bytes", m->size);
    {
        char left[24];
        sprintf(left, "Memory %s", m->name);
        title(left, right);
    }
    drawText();
    hint("\x18\x19 PgUp PgDn scroll \xFA Del delete \xFA Esc back to the list");
    for (;;) {
        unsigned key = waitKey();
        unsigned char ch = (unsigned char)key, scan = (unsigned char)(key >> 8);

        if (ch == KEY_ESC || ch == KEY_BS || ch == KEY_ENTER)
            return 0;
        if (ch != 0 && ch != 0xE0)
            continue;
        switch (scan) {
        case SCAN_UP:   scrollTo(topLine - 1); break;
        case SCAN_DOWN: scrollTo(topLine + 1); break;
        case SCAN_PGUP: scrollTo(topLine - (TEXT_ROWS - 1)); break;
        case SCAN_PGDN: scrollTo(topLine + (TEXT_ROWS - 1)); break;
        case SCAN_HOME: scrollTo(0); break;
        case SCAN_END:  scrollTo(lines); break;
        case SCAN_F4:   return 0;
        case SCAN_DEL:
            if (confirmDelete(m->name))
                return 1;
            break;
        }
    }
}

/* --- the manager -------------------------------------------------------------- */

static int manage(void)
{
    int deleted = 0;
    char buf[60];

    rescan();
    drawList();
    for (;;) {
        unsigned key = waitKey();
        unsigned char ch = (unsigned char)key, scan = (unsigned char)(key >> 8);

        if (ch == KEY_ESC)
            return deleted;
        if (ch == KEY_ENTER && shown) {
            int gone = view(&mems[sel]);
            if (gone) {
                sprintf(buf, "Deleted %s.", mems[sel].name);
                deleted++;
                rescan();
            }
            drawList();
            if (gone)
                message(buf, A_GOOD);
            continue;
        }
        if (ch != 0 && ch != 0xE0)
            continue;
        switch (scan) {
        case SCAN_F4:   return deleted;
        case SCAN_UP:   moveTo(sel - 1); break;
        case SCAN_DOWN: moveTo(sel + 1); break;
        case SCAN_PGUP: moveTo(sel - (LIST_ROWS - 1)); break;
        case SCAN_PGDN: moveTo(sel + (LIST_ROWS - 1)); break;
        case SCAN_HOME: moveTo(0); break;
        case SCAN_END:  moveTo(shown - 1); break;
        case SCAN_DEL:
            if (shown && confirmDelete(mems[sel].name)) {
                sprintf(buf, "Deleted %s.", mems[sel].name);
                deleted++;
                rescan();
                if (first > 0 && first + LIST_ROWS > shown)
                    first = shown > LIST_ROWS ? shown - LIST_ROWS : 0;
                drawList();
                message(buf, A_GOOD);
            }
            break;
        }
    }
}

int memui_run(unsigned char accentAttr, void (*idle)(void))
{
    static const ScrState home = { 0, 0, 0 };
    unsigned short __far *save;
    ScrState chatState;
    int deleted = 0;

    accent = accentAttr;
    idleFn = idle;
    mems = (MemEntry *)malloc(MAX_MEMS * sizeof(MemEntry));
    text = (char *)malloc(TEXT_MAX + 1);
    lineStart = (unsigned short *)malloc(MAX_LINES * sizeof(unsigned short));
    lineLen = (unsigned char *)malloc(MAX_LINES);
    save = mems && text && lineStart && lineLen ? tools_save_screen() : NULL;
    if (save != NULL) {
        scr_state_get(&chatState);
        scr_state_set(&home);         /* row 0 is the top of video memory now */
        scr_cursor_visible(0);
        sel = first = 0;
        deleted = manage();
        scr_cursor_visible(1);
        scr_state_set(&chatState);
        tools_restore_screen(save);
    }
    free(lineLen);
    free(lineStart);
    free(text);
    free(mems);
    return save != NULL ? deleted : -1;
}
