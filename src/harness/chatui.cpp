/* The chat list: see chatui.h.
 *
 * It borrows the screen the way the memory manager does (memui.cpp): the
 * chat is saved with tools_save_screen, row 0 is the top of video memory
 * while the list has it, and the chat comes back untouched. The entries live
 * on the heap only while the list is open.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

#include "types.h"
#include "inlines.h"
#include "screen.h"
#include "tools.h"
#include "chatfile.h"
#include "chatui.h"
#include "relaunch.h"

#define MAX_CHATS  100                /* listed, newest first; older ones are counted only */
#define SUMMARY    41                 /* characters of the first message shown */
#define HEAD_READ  160                /* bytes read of each file for it */

#define LIST_TOP   2                  /* first list row */
#define LIST_ROWS  20                 /* rows 2..21 */
#define MSG_ROW    23
#define HINT_ROW   24

#define A_TEXT     SCR_ATTR(C_LIGHTGRAY, C_BLACK)
#define A_DIM      SCR_ATTR(C_DARKGRAY, C_BLACK)
#define A_SEL      SCR_ATTR(C_BLACK, C_LIGHTGRAY)
#define A_ERR      SCR_ATTR(C_LIGHTRED, C_BLACK)
#define A_GOOD     SCR_ATTR(C_LIGHTGREEN, C_BLACK)

#define KEY_ENTER  13
#define KEY_ESC    27
#define SCAN_F6    0x40
#define SCAN_HOME  0x47
#define SCAN_UP    0x48
#define SCAN_PGUP  0x49
#define SCAN_END   0x4F
#define SCAN_DOWN  0x50
#define SCAN_PGDN  0x51
#define SCAN_DEL   0x53

struct ChatEntry {
    char name[9];                     /* MMDDnnn, without .JSN */
    unsigned date, time;              /* last written, as DOS keeps them */
    unsigned long size;
    char summary[SUMMARY + 1];        /* the start of the first message */
};

static unsigned char accent;
static void (*idleFn)(void);
static void (*logFn)(const char *line);
static ChatEntry *chats;
static int found, shown;              /* chats with something in them, and listed */
static int sel, first;                /* selected entry, first one on screen */
static char current[9];               /* the open conversation's name ("" none) */

/* --- drawing --------------------------------------------------------------- */

static void blankRow(int row)
{
    scr_fill(row, 0, SCR_COLS, ' ', A_TEXT);
}

static void message(const char *s, unsigned char attr)
{
    blankRow(MSG_ROW);
    if (s)
        scr_puts(MSG_ROW, 1, s, attr);
}

static unsigned waitKey(void)
{
    while (!biosIsKeyReady())
        idleFn();
    return biosKeyRead();
}

static void listRow(int i)
{
    int row = LIST_TOP + i - first;
    const ChatEntry *e = &chats[i];
    char line[SCR_COLS + 1], size[12];

    blankRow(row);
    if (i >= shown)
        return;
    if (e->size < 1024)
        sprintf(size, "%lu B", e->size);
    else
        sprintf(size, "%lu KB", (e->size + 512) / 1024);
    sprintf(line, "%c%-8s  %04u-%02u-%02u %02u:%02u  %6s  %s", strcmp(e->name, current) ? ' ' : '*', e->name,
            (e->date >> 9) + 1980, (e->date >> 5) & 15, e->date & 31, e->time >> 11, (e->time >> 5) & 63,
            size, e->summary);
    scr_puts(row, 1, line, i == sel ? A_SEL : A_TEXT);
}

static void drawList(void)
{
    char right[48];
    int i;

    blankRow(0);
    scr_puts(0, 1, "Chats", accent);
    sprintf(right, CHATS_DIR " \xFA %d chat%s", found, found == 1 ? "" : "s");
    scr_puts(0, SCR_COLS - 1 - (int)strlen(right), right, A_DIM);
    scr_fill(1, 0, SCR_COLS, 0xC4, A_DIM);
    if (shown)                        /* column heads over the rule */
        scr_puts(1, 1, " Name      Last written      Size  First message ", A_DIM);
    for (i = first; i < first + LIST_ROWS; i++)
        listRow(i);
    if (!shown)
        scr_puts(LIST_TOP + 1, 3, "No earlier chats yet.", A_TEXT);
    blankRow(LIST_TOP + LIST_ROWS);
    if (found > shown) {
        sprintf(right, "Showing the newest %d of %d chats.", shown, found);
        message(right, A_DIM);
    } else {
        message(NULL, A_DIM);
        for (i = 0; i < shown; i++)
            if (!strcmp(chats[i].name, current))
                message("* is the chat you are in.", A_DIM);
    }
    blankRow(HINT_ROW);
    scr_puts(HINT_ROW, 1, shown ? "\x18\x19 PgUp PgDn move \xFA Enter open \xFA Del delete \xFA Esc back to the chat"
                                : "Esc back to the chat", A_DIM);
}

/* The boot tests' look at the list: its rows as video memory holds them. */
static void logRows(void)
{
    unsigned short cells[SCR_COLS];
    char line[SCR_COLS + 12];
    int row, n;

    for (row = 0; row < LIST_TOP + (shown < LIST_ROWS ? shown : LIST_ROWS); row++) {
        scr_getn(row, 0, cells, SCR_COLS);
        strcpy(line, "chat list: ");
        for (n = 0; n < SCR_COLS; n++)
            line[11 + n] = (char)(cells[n] & 0xFF);
        for (n = 11 + SCR_COLS; n > 11 && line[n - 1] == ' '; n--)
            ;
        line[n] = 0;
        logFn(line);
    }
}

/* --- the entries -------------------------------------------------------------- */

static int newer(const ChatEntry *a, const ChatEntry *b)
{
    if (a->date != b->date)
        return a->date > b->date;
    if (a->time != b->time)
        return a->time > b->time;
    return strcmp(a->name, b->name) > 0;
}

static void summarize(ChatEntry *e)
{
    char path[40], head[HEAD_READ];
    FILE *f;
    unsigned n = 0;

    sprintf(path, CHATS_DIR "\\%s.JSN", e->name);
    if ((f = fopen(path, "rb")) != NULL) {
        n = fread(head, 1, sizeof head, f);
        fclose(f);
    }
    if (!chatfile_first(head, n, e->summary, sizeof e->summary))
        strcpy(e->summary, "(no typed message)");
}

/* Every CHATS\*.JSN with something in it, newest first. */
static void scan(void)
{
    struct find_t f;
    ChatEntry e;
    int i;

    found = shown = 0;
    if (_dos_findfirst(CHATS_DIR "\\*.JSN", _A_NORMAL | _A_RDONLY | _A_ARCH, &f) == 0) {
        do {
            if (f.size == 0)          /* a new chat nothing was said in */
                continue;
            found++;
            memset(&e, 0, sizeof e);
            strncpy(e.name, f.name, sizeof e.name - 1);
            if (strchr(e.name, '.'))
                *strchr(e.name, '.') = 0;
            e.date = f.wr_date;
            e.time = f.wr_time;
            e.size = f.size;
            for (i = shown; i > 0 && newer(&e, &chats[i - 1]); i--)
                ;
            if (i >= MAX_CHATS)
                continue;
            if (shown < MAX_CHATS)
                shown++;
            memmove(&chats[i + 1], &chats[i], (shown - 1 - i) * sizeof e);
            chats[i] = e;
        } while (_dos_findnext(&f) == 0);
    }
    for (i = 0; i < shown; i++)       /* only for the ones kept */
        summarize(&chats[i]);
    if (sel >= shown)
        sel = shown - 1;
    if (sel < 0)
        sel = 0;
    if (first > 0 && first + LIST_ROWS > shown)
        first = shown > LIST_ROWS ? shown - LIST_ROWS : 0;
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

/* Whether RESUME.TXT (a full_memory run waiting for CLAWDOS -resume) names
   the chat at path: deleting it then would leave the resume a lone result to
   write into a new, empty file. */
static int resumeNames(const char *path)
{
    FILE *f = fopen(RELAUNCH_STATE, "r");
    char line[48];
    int yes = 0;

    if (!f)
        return 0;
    if (fgets(line, sizeof line, f) && !strncmp(line, "chat ", 5)) {   /* writeState's first line */
        line[strcspn(line, "\r\n")] = 0;
        yes = !stricmp(line + 5, path);
    }
    fclose(f);
    return yes;
}

/* Del: ask, then delete the chat and its prefix file. 1 if it went. */
static int confirmDelete(const char *name)
{
    char buf[72], path[40];
    unsigned char ch;

    sprintf(path, CHATS_DIR "\\%s.JSN", name);
    if (!strcmp(name, current)) {
        message("That is the chat you are in: it stays.", A_ERR);
        return 0;
    }
    if (resumeNames(path)) {
        sprintf(buf, "A full_memory run of %s waits for CLAWDOS -resume: it stays.", name);
        message(buf, A_ERR);
        return 0;
    }
    sprintf(buf, "Delete chat %s? (y/n)", name);
    message(buf, A_ERR);
    ch = (unsigned char)waitKey();
    if (ch != 'y' && ch != 'Y') {
        sprintf(buf, "Kept %s.", name);
        message(buf, A_DIM);
        return 0;
    }
    _dos_setfileattr(path, _A_NORMAL);
    if (remove(path)) {
        sprintf(buf, "Couldn't delete %s (read-only disk?).", name);
        message(buf, A_ERR);
        return 0;
    }
    strcpy(strrchr(path, '.'), ".PRE");
    remove(path);                     /* none if it never got a reply */
    sprintf(buf, "chats: deleted %s", name);
    logFn(buf);
    return 1;
}

/* --- the list ------------------------------------------------------------------ */

static int pick(int logScreen, char *chosen)
{
    char buf[48];

    scan();
    drawList();
    if (logScreen)
        logRows();
    for (;;) {
        unsigned key = waitKey();
        unsigned char ch = (unsigned char)key, scan_ = (unsigned char)(key >> 8);

        if (ch == KEY_ESC)
            return 0;
        if (ch == KEY_ENTER && shown) {
            if (!strcmp(chats[sel].name, current))
                return 0;             /* the one we are in: back to it */
            sprintf(chosen, CHATS_DIR "\\%s.JSN", chats[sel].name);
            return 1;
        }
        if (ch != 0 && ch != 0xE0)
            continue;
        switch (scan_) {
        case SCAN_F6:   return 0;
        case SCAN_UP:   moveTo(sel - 1); break;
        case SCAN_DOWN: moveTo(sel + 1); break;
        case SCAN_PGUP: moveTo(sel - (LIST_ROWS - 1)); break;
        case SCAN_PGDN: moveTo(sel + (LIST_ROWS - 1)); break;
        case SCAN_HOME: moveTo(0); break;
        case SCAN_END:  moveTo(shown - 1); break;
        case SCAN_DEL:
            if (shown && confirmDelete(chats[sel].name)) {
                sprintf(buf, "Deleted %s.", chats[sel].name);
                memmove(&chats[sel], &chats[sel + 1], (shown - sel - 1) * sizeof chats[0]);
                shown--;              /* the one entry goes; the rest need no rereading */
                found--;
                if (sel >= shown && sel > 0)
                    sel--;
                if (first > 0 && first + LIST_ROWS > shown)
                    first = shown > LIST_ROWS ? shown - LIST_ROWS : 0;
                drawList();
                message(buf, A_GOOD);
            }
            break;
        }
    }
}

int chatui_run(unsigned char accentAttr, void (*idle)(void), void (*log)(const char *line), int logScreen,
               const char *currentPath, char *chosen)
{
    static const ScrState home = { 0, 0, 0 };
    unsigned short __far *save = NULL;
    ScrState chatState;
    const char *base = strrchr(currentPath, '\\');
    int rc = -1;

    accent = accentAttr;
    idleFn = idle;
    logFn = log;
    strncpy(current, base ? base + 1 : currentPath, sizeof current - 1);
    current[sizeof current - 1] = 0;
    if (strchr(current, '.'))
        *strchr(current, '.') = 0;
    if ((chats = (ChatEntry *)malloc(MAX_CHATS * sizeof(ChatEntry))) != NULL)
        save = tools_save_screen();
    if (save != NULL) {
        scr_state_get(&chatState);
        scr_state_set(&home);         /* row 0 is the top of video memory now */
        scr_cursor_visible(0);
        sel = first = 0;
        rc = pick(logScreen, chosen);
        scr_cursor_visible(1);
        scr_state_set(&chatState);
        tools_restore_screen(save);
    }
    free(chats);
    return rc;
}
