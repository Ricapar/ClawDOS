/* HARNESS.EXE - chat with Claude from an IBM PC 5160.
 *
 * Talks the Messages API itself (chat.cpp) through a TLS tunnel, sends what
 * you type, and streams the reply onto a word-wrapped, hardware-scrolled CGA
 * screen. The look follows Claude Code: a welcome panel with a mascot, "> "
 * for your messages, a bullet for Claude's, a spinner while it works, and an
 * input box pinned to the bottom of the screen.
 *
 *   CLAWDOS [host[:port]] [-p provider] [-unsafe] [-q] [-m message] [-i text]
 *           [-memories]
 *
 * Claude can also use tools on this machine (tools.cpp); in safe mode (F3)
 * anything that would change files, memory or ports asks first.
 *
 * Settings, providers and the API key come from C:\CLAWDOS\CLAWDOS.CFG; a host
 * on the command line replaces the provider's tunnel address (the tests point
 * it at the mock API).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include "types.h"
#include "inlines.h"
#include "net.h"
#include "cfg.h"
#include "chat.h"
#include "screen.h"
#include "console.h"
#include "tools.h"
#include "toolapi.h"
#include "ticks.h"
#include "relaunch.h"
#include "ui.h"
#include "memui.h"

#ifndef BUILD_STAMP                 /* yyyymmdd.hhmm, from the build (scripts/build.py) */
#define BUILD_STAMP "dev"
#endif
#define VERSION   BUILD_STAMP
#define CFG_PATH  "C:\\CLAWDOS\\CLAWDOS.CFG"
#define LOG_PATH  "C:\\CLAWDOS\\CLAWDOS.LOG"
#define INPUT_MAX 900
#define HIST_N    8

/* Colours: "blue chrome". The conversation is on black; the parts that are
   ClawDOS's own (the welcome panel, the input box, the F-key bar) are blue
   or Norton-style bars, with yellow for the mascot and headings. On blue, dark
   gray and light blue all but vanish, so dim text there is cyan. */
#define A_MASCOT  SCR_ATTR(C_YELLOW, C_BLACK)       /* spinner */
#define A_ACCENT  SCR_ATTR(C_YELLOW, C_BLACK)
#define A_TEXT    SCR_ATTR(C_LIGHTGRAY, C_BLACK)

#define A_P_TEXT  SCR_ATTR(C_LIGHTGRAY, C_BLUE)     /* welcome panel */
#define A_P_FRAME SCR_ATTR(C_LIGHTCYAN, C_BLUE)
#define A_P_MASCOT SCR_ATTR(C_YELLOW, C_BLUE)
#define A_P_HEAD  SCR_ATTR(C_YELLOW, C_BLUE)
#define A_P_TITLE SCR_ATTR(C_WHITE, C_BLUE)
#define A_P_DIM   SCR_ATTR(C_CYAN, C_BLUE)

#define A_BOX     SCR_ATTR(C_WHITE, C_BLUE)         /* input box: frame, prompt, text */
#define A_BOX_HINT SCR_ATTR(C_LIGHTGRAY, C_BLUE)

#define A_KEY_NUM SCR_ATTR(C_WHITE, C_BLACK)        /* F-key bar: "1" */
#define A_KEY     SCR_ATTR(C_BLACK, C_CYAN)         /* "Help  " */
#define A_KEY_OFF SCR_ATTR(C_WHITE, C_RED)          /* "Unsafe" */
#define A_KEY_DIM SCR_ATTR(C_CYAN, C_BLACK)         /* the tunnel's address */

#define A_BRIGHT  SCR_ATTR(C_WHITE, C_BLACK)
#define A_DIM     SCR_ATTR(C_DARKGRAY, C_BLACK)
#define A_ERR     SCR_ATTR(C_LIGHTRED, C_BLACK)
#define A_GOOD    SCR_ATTR(C_LIGHTGREEN, C_BLACK)

/* Code page 437 glyphs */
#define G_H       0xC4    /* box drawing: horizontal */
#define G_V       0xB3    /* vertical */
#define G_TL      0xDA
#define G_TR      0xBF
#define G_BL      0xC0
#define G_BR      0xD9
#define G_TDOWN   0xC2
#define G_TUP     0xC1
#define G_BULLET  0x07
#define G_DOT     0xFA    /* middle dot */
#define G_UP      0x18
#define G_DOWN    0x19

/* Input box: "| > text... |" */
#define BOX_TEXT  4                   /* first text column */
#define BOX_W     (SCR_COLS - 6)      /* text columns per line */
#define BOX_LINES 4                   /* most lines the box grows to */

#define KEY_ENTER 13
#define KEY_BS    8
#define KEY_ESC   27
#define KEY_CTRLN 14
#define KEY_CTRLU 21
#define SCAN_F1   0x3B
#define SCAN_F2   0x3C
#define SCAN_F3   0x3D
#define SCAN_F4   0x3E
#define SCAN_F10  0x44
#define SCAN_ALTX 0x2D
#define SCAN_HOME 0x47
#define SCAN_UP   0x48
#define SCAN_PGUP 0x49
#define SCAN_LEFT 0x4B
#define SCAN_RIGHT 0x4D
#define SCAN_END  0x4F
#define SCAN_DOWN 0x50
#define SCAN_PGDN 0x51
#define SCAN_DEL  0x53

enum State { IDLE, WAITING, STREAMING };

static Chat chat;
static Cfg *cfg;
static State state = IDLE;
static int quitting;
static int exitCode;
static int queued;                    /* Enter pressed while Claude was busy */
static int quitAfterReply;            /* -q: leave once the -m message is answered */
static int quickFlag;                 /* -q as given, for the relaunch state */
static int resumeMode;                /* -resume: back from a run under CLAWDOS.EXE */

/* RELAUNCH_STATE: what a relaunched CLAWDOS needs to carry the turn on. */
struct Resume {
    char chat[40], tool[64], provider[16], model[48], host[80];
    int round, merge, mergeAtStart, assistant, safe, quick, quit;
    unsigned long ticks;              /* TICKS_NOW when CLAWDOS left */
};
static Resume resumeState;
static const char *toolVerb;          /* spinner verb while a tool runs */
static int stopRequested;             /* Esc during a turn */
static unsigned long lastSpin;
static char host[80];                 /* command-line address, if any */

static char input[INPUT_MAX + 1];
static int inputLen, inputPos;
static int boxLines = 1;              /* text lines in the input box */
static int boxScroll;                 /* first input line shown */
static int lineStart[INPUT_MAX / 2];  /* input word-wrapped into lines */
static int lineCount;

static char history[HIST_N][INPUT_MAX + 1];
static int histCount, histPos;

static const char *statusNote;        /* shown in the status row when idle */
static unsigned long busySince;
static int verbIndex;



/* --- building rows ------------------------------------------------------- */

struct Row {
    unsigned short c[SCR_COLS];
};

static void rowBlank(Row &r, unsigned char attr = A_TEXT)
{
    for (int i = 0; i < SCR_COLS; i++)
        r.c[i] = ' ' | (attr << 8);
}

static void rowFill(Row &r, int col, int n, unsigned char ch, unsigned char attr)
{
    while (n-- > 0 && col < SCR_COLS)
        r.c[col++] = ch | (attr << 8);
}

/* Put text at col (clipped to `width` chars); returns the column after it. */
static int rowText(Row &r, int col, const char *s, unsigned char attr, int width = SCR_COLS)
{
    while (*s && col < SCR_COLS && width-- > 0)
        r.c[col++] = (unsigned char)*s++ | (attr << 8);
    return col;
}

static void rowCenter(Row &r, int col, int width, const char *s, unsigned char attr)
{
    int len = strlen(s);
    if (len > width)
        len = width;
    rowText(r, col + (width - len) / 2, s, attr, len);
}

/* A box line: left corner/edge, fill, right corner/edge. */
static void rowFrame(Row &r, unsigned char left, unsigned char fill, unsigned char right,
                     unsigned char attr)
{
    r.c[0] = left | (attr << 8);
    rowFill(r, 1, SCR_COLS - 2, fill, attr);
    r.c[SCR_COLS - 1] = right | (attr << 8);
}


/* --- footer: status row, input box, hint row ------------------------------ */

static int footerRows(void) { return boxLines + 4; }
static int footerTop(void)  { return SCR_ROWS - footerRows(); }

static const char *const verbs[] = {
    "Thinking", "Pondering", "Clauding", "Computing", "Percolating", "Ruminating",
    "Musing", "Cogitating", "Noodling", "Spooling", "Buffering", "Paging",
    "Multiplexing", "Interleaving", "Seeking", "Defragmenting"
};
static const unsigned char spinGlyphs[] = { 0xFA, 0xF9, '+', '*', 0x0F, '*', '+', 0xF9 };

static void drawStatus(void)
{
    Row r;
    char buf[48];

    rowBlank(r);
    if (state != IDLE) {
        unsigned long now = TICKS_NOW;
        unsigned long elapsed = ticks_elapsed(now, busySince);
        int col, digits, end;

        r.c[1] = spinGlyphs[(unsigned)(now >> 1) & 7] | (A_MASCOT << 8);
        col = rowText(r, 3, toolVerb ? toolVerb : verbs[verbIndex], A_ACCENT);
        col = rowText(r, col, "... ", A_ACCENT);
        digits = sprintf(buf, "(%u", TICKS_TO_SECS(elapsed));
        sprintf(buf + digits, "s %c esc to interrupt%s)", G_DOT, queued ? " \xFA 1 message queued" : "");
        end = rowText(r, col, buf, A_DIM);
        scr_putn(footerTop(), 0, r.c, SCR_COLS);
        /* From here the timer interrupt moves the spinner and the seconds,
           even while we're busy with no time to call this. */
        scr_ticker(footerTop(), 1, spinGlyphs, A_MASCOT, col + 1, A_DIM, elapsed, now,
                   &r.c[col + digits], end - (col + digits));
        return;
    }
    scr_ticker_off();
    if (statusNote)
        rowText(r, 1, statusNote, A_DIM);
    scr_putn(footerTop(), 0, r.c, SCR_COLS);
}

/* Word-wrap the input into lineStart[]. A full last line gets an empty one
   after it, for the cursor. */
static void wrapInput(void)
{
    int start = 0;

    lineCount = 0;
    lineStart[lineCount++] = 0;
    while (inputLen - start >= BOX_W) {
        int k = start + BOX_W;
        while (k > start && input[k - 1] != ' ')
            k--;
        if (k == start)
            k = start + BOX_W;            /* one long word: break it */
        lineStart[lineCount++] = start = k;
    }
}

static int lineEnd(int line)
{
    return line + 1 < lineCount ? lineStart[line + 1] : inputLen;
}

static int cursorLine(void)
{
    int line = lineCount - 1;
    while (line > 0 && lineStart[line] > inputPos)
        line--;
    return line;
}

static void placeCursor(void)
{
    int line = cursorLine();
    scr_cursor(footerTop() + 2 + line - boxScroll, BOX_TEXT + inputPos - lineStart[line]);
}

static void drawInputLine(int i)
{
    Row r;
    int line = boxScroll + i;

    rowBlank(r, A_BOX);
    r.c[0] = G_V | (A_BOX << 8);
    r.c[SCR_COLS - 1] = G_V | (A_BOX << 8);
    if (line == 0)
        r.c[2] = '>' | (A_BOX << 8);
    if (inputLen == 0 && i == 0) {
        rowText(r, BOX_TEXT, state == IDLE ? "What would you like to do?  (F1 for help)" : "",
                A_BOX_HINT);
    } else if (line < lineCount) {
        int first = lineStart[line];
        int n = lineEnd(line) - first;
        for (int k = 0; k < n; k++)
            r.c[BOX_TEXT + k] = (unsigned char)input[first + k] | (A_BOX << 8);
    }
    scr_putn(footerTop() + 2 + i, 0, r.c, SCR_COLS);
}

/* One Norton-style F-key: the number, then its label on a bar. */
static int rowKey(Row &r, int col, const char *num, const char *label, unsigned char attr)
{
    col = rowText(r, col, num, A_KEY_NUM);
    rowFill(r, col, 6, ' ', attr);
    rowText(r, col, label, attr, 6);
    return col + 6 + 1;
}

static void drawHint(void)
{
    Row r;
    int col;

    rowBlank(r);
    col = rowKey(r, 0, "1", "Help", A_KEY);
    col = rowKey(r, col, "2", "New", A_KEY);
    col = rowKey(r, col, "3", tools_safe() ? "Safe" : "Unsafe", tools_safe() ? A_KEY : A_KEY_OFF);
    col = rowKey(r, col, "4", "Memory", A_KEY);
    col = rowKey(r, col, "10", "Quit", A_KEY);
    {   /* the tunnel's address, right-aligned; a long one is cut to the room left */
        const char *where = chat.address();
        int len = strlen(where), room = SCR_COLS - 5 - col;
        if (len > room)
            len = room;
        col = SCR_COLS - 2 - len - 2;
        r.c[col] = G_BULLET | ((chat.connected() ? A_GOOD : A_ERR) << 8);
        rowText(r, col + 2, where, A_KEY_DIM, len);
    }
    scr_putn(SCR_ROWS - 1, 0, r.c, SCR_COLS);
}

static void drawFooter(void)
{
    Row r;
    int top = footerTop();

    drawStatus();
    rowFrame(r, G_TL, G_H, G_TR, A_BOX);
    scr_putn(top + 1, 0, r.c, SCR_COLS);
    for (int i = 0; i < boxLines; i++)
        drawInputLine(i);
    rowFrame(r, G_BL, G_H, G_BR, A_BOX);
    scr_putn(top + 2 + boxLines, 0, r.c, SCR_COLS);
    drawHint();
    placeCursor();
}

/* Re-wrap the input, fit the box to it, keep the cursor's line in view, and
   redraw the input from line `from` on (or all of the footer if its size
   changed). Rewrapping can pull a word back up a line, hence the -1. */
static void layoutInput(int from)
{
    int need, lines, curLine, scroll = boxScroll;

    wrapInput();
    need = lineCount;
    lines = need < BOX_LINES ? need : BOX_LINES;
    curLine = cursorLine();
    from--;

    if (curLine < scroll)
        scroll = curLine;
    if (curLine >= scroll + lines)
        scroll = curLine - lines + 1;
    if (scroll > need - lines)
        scroll = need - lines;

    if (lines != boxLines) {
        if (lines > boxLines)
            con_make_room(lines + 4);     /* scroll text out from under the box */
        boxLines = lines;
        boxScroll = scroll;
        scr_footer(footerRows(), A_TEXT);
        drawFooter();
        return;
    }
    if (scroll != boxScroll) {
        boxScroll = scroll;
        from = 0;
    }
    for (int i = from - boxScroll < 0 ? 0 : from - boxScroll; i < boxLines; i++)
        drawInputLine(i);
    placeCursor();
}

static void setInput(const char *text)
{
    strncpy(input, text, INPUT_MAX);
    input[INPUT_MAX] = 0;
    inputLen = inputPos = strlen(input);
    layoutInput(0);
}


/* --- transcript ---------------------------------------------------------- */

static void blankLine(void)
{
    con_ensure_blank_line();
    con_newline();
}

/* "  L text" under the message it belongs to. */
static void note(const char *text, unsigned char attr)
{
    con_margins(0, SCR_COLS - 1);
    con_ensure_blank_line();
    con_attr(A_DIM);
    con_write("  \xC0 ");
    con_margins(4, SCR_COLS - 1);
    con_attr(attr);
    con_write(text);
    con_newline();
}

static void dimLines(const char *const *lines)
{
    con_margins(2, SCR_COLS - 1);
    con_attr(A_DIM);
    for (; *lines; lines++) {
        con_write(*lines);
        con_newline();
    }
}

static void showHelp(void)
{
    static const char *const help[] = {
        "Enter sends \xFA Esc stops a reply (or clears what you typed)",
        "\x18/\x19 recall earlier messages \xFA \x1B/\x1A Home End Del edit \xFA Ctrl-U clears",
        "PgUp/PgDn scroll back \xFA F2 or /new starts over \xFA F10 or /quit leaves",
        "/model lists the AI providers in CLAWDOS.CFG \xFA /model name switches",
        "F3 toggles safe mode (asks before tools change files, memory or ports)",
        "F4 or /memories looks through Claude's memories and deletes them",
        0
    };
    blankLine();
    dimLines(help);
}

static void systemLine(char *buf)
{
    union REGS r;
    int86(0x12, &r, &r);              /* conventional memory in KB */
    sprintf(buf, "%uK RAM \xFA %s \xFA ", r.x.ax, scr_has_hwscroll() ? "CGA" : "EGA/VGA");
    Net::localAddress(buf + strlen(buf));
}

/* The welcome panel, on blue: the mascot on the left, tips on the right. */
static void welcome(void)
{
    /* The mascot, CORE, in half blocks (docs/research/mascots.md has the
       others): a one-eyed ISA card, 11x8 "pixels", edge-connector fingers
       along the bottom. Odd width so the four legs sit symmetrically. */
    static const char *const mascot[4] = {
        "\xDB\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDB",
        "\xDB \xDB\xDF\xDB\xDB\xDB\xDF\xDB \xDB",
        "\xDB \xDF\xDF\xDF\xDF\xDF\xDF\xDF \xDB",
        "\xDF\xDF\xDB \xDB \xDB \xDB\xDF\xDF",
    };
    enum { L = 1, LW = 38, MID = 39, R = 42, RW = SCR_COLS - 2 - 42 };
    char sys[48];
    Row r;
    int i;

    systemLine(sys);
    for (i = 0; i < 12; i++) {
        rowBlank(r, A_P_TEXT);
        if (i == 0) {
            int col;
            rowFrame(r, G_TL, G_H, G_TR, A_P_FRAME);
            col = rowText(r, 4, " ClawDOS ", A_P_TITLE);
            rowText(r, col - 1, " v" VERSION " ", A_P_DIM);
            r.c[MID] = G_TDOWN | (A_P_FRAME << 8);
        } else if (i == 11) {
            rowFrame(r, G_BL, G_H, G_BR, A_P_FRAME);
            r.c[MID] = G_TUP | (A_P_FRAME << 8);
        } else {
            r.c[0] = r.c[MID] = r.c[SCR_COLS - 1] = G_V | (A_P_FRAME << 8);
        }
        switch (i) {
        case 2: rowCenter(r, L, LW, "Welcome to ClawDOS!", A_P_TITLE); break;
        case 4: case 5: case 6: case 7: rowCenter(r, L, LW, mascot[i - 4], A_P_MASCOT); break;
        case 9: rowCenter(r, L, LW, chat.describe(), A_P_DIM); break;
        case 10: rowCenter(r, L, LW, chat.address(), A_P_DIM); break;
        }
        switch (i) {
        case 2: rowText(r, R, "Tips for getting started", A_P_HEAD); break;
        case 3: rowText(r, R, "Type below and press Enter to chat", A_P_TEXT, RW); break;
        case 4: rowText(r, R, "Esc stops \xFA F2 new chat \xFA F3 safe", A_P_TEXT, RW); break;
        case 5: rowText(r, R, "F1 help \xFA F4 memories \xFA PgUp/PgDn", A_P_TEXT, RW); break;
        case 6:
            rowFill(r, MID + 1, SCR_COLS - MID - 2, G_H, A_P_FRAME);
            r.c[MID] = 0xC3 | (A_P_FRAME << 8);              /* tee joints */
            r.c[SCR_COLS - 1] = 0xB4 | (A_P_FRAME << 8);
            break;
        case 7: rowText(r, R, "This machine", A_P_HEAD); break;
        case 8: rowText(r, R, sys, A_P_TEXT, RW); break;
        case 9: rowText(r, R, "ClawDOS \xFA FreeDOS \xFA mTCP", A_P_TEXT, RW); break;
        }
        con_line(r.c);
    }
}

static void echoUser(const char *text)
{
    blankLine();
    con_margins(0, SCR_COLS - 1);
    con_attr(A_DIM);
    con_write("> ");
    con_margins(2, SCR_COLS - 1);
    con_attr(A_TEXT);
    con_write(text);
    con_newline();
}

static void startReply(void)
{
    blankLine();
    con_margins(0, SCR_COLS - 1);
    con_attr(A_BRIGHT);
    con_putc(G_BULLET);
    con_putc(' ');
    con_margins(2, SCR_COLS - 1);
    con_attr(A_TEXT);
}


/* --- conversation ---------------------------------------------------------- */

static void setState(State s)
{
    if (s != IDLE && state == IDLE) {
        busySince = TICKS_NOW;
        verbIndex = (int)(busySince % (sizeof(verbs) / sizeof(verbs[0])));
    }
    state = s;
    drawStatus();
    if (inputLen == 0)
        drawInputLine(0);             /* placeholder only shows when idle */
    placeCursor();
}

static void newChat(void)
{
    if (chat.newConversation())
        note(chat.errorText(), A_ERR);
    con_init();
    welcome();
    blankLine();
    note("New conversation.", A_DIM);
    drawFooter();
}

/* F4 or /memories: the memory manager (memui.cpp) has the screen until Esc.
   The conversation keeps the list of memories it started with. */
static void memIdle(void)
{
    _asm { sti
           hlt }
    Net::service();
}

static void memories(void)
{
    int deleted;

    if (state != IDLE)
        return;
    deleted = memui_run(A_ACCENT, memIdle);
    if (deleted < 0)
        note("Not enough memory to open the memory manager.", A_ERR);
    else if (deleted > 0)
        note("Deleted memories take effect in a new chat (F2).", A_DIM);
}

static void submit(void);
static void handleKey(unsigned key);
static void modelCommand(const char *arg);
static void relaunchExit(void);

static void finished(void)
{
    setState(IDLE);
    if (quitAfterReply && !queued)
        quitting = 1;
    if (queued) {
        queued = 0;
        submit();
    }
}

static void submit(void)
{
    if (inputLen == 0)
        return;
    if (state != IDLE) {
        queued = 1;
        drawStatus();
        return;
    }
    input[inputLen] = 0;
    if (!stricmp(input, "/quit")) {
        quitting = 1;
        return;
    }
    if (histCount == 0 || strcmp(history[0], input)) {
        memmove(history[1], history[0], (HIST_N - 1) * sizeof(history[0]));
        strcpy(history[0], input);
        if (histCount < HIST_N)
            histCount++;
    }
    histPos = -1;
    if (!stricmp(input, "/new")) {
        setInput("");
        newChat();
        return;
    }
    if (!stricmp(input, "/memories")) {
        setInput("");
        memories();
        return;
    }
    if (!stricmp(input, "/help")) {
        setInput("");
        showHelp();
        return;
    }
    if (!strnicmp(input, "/model", 6) && (input[6] == 0 || input[6] == ' ')) {
        modelCommand(input + 6);
        setInput("");
        return;
    }
    echoUser(input);
    {
        static char text[INPUT_MAX + 1];
        int rc;
        strcpy(text, input);
        setInput("");
        setState(WAITING);
        stopRequested = 0;
        rc = chat.turn(text);
        if (rc == 2)
            relaunchExit();           /* does not return */
        if (rc == 1) {
            note("Interrupted by user", A_ERR);
            if (!chat.answered())
                setInput(text);       /* nothing came back: keep the text for another try */
        } else if (rc < 0) {
            if (quitAfterReply)
                exitCode = 1;
            if (!chat.answered())
                setInput(text);
        }
    }
    finished();
}

/* /model lists the providers in CLAWDOS.CFG, /model NAME[:MODEL] or /model MODEL switches. */
static void modelCommand(const char *arg)
{
    char buf[100];
    int i;

    while (*arg == ' ')
        arg++;
    if (!*arg) {
        for (i = 0; i < cfg->nproviders; i++) {
            const Provider *p = &cfg->providers[i];
            sprintf(buf, "  %-12.12s %-40.40s%s%s", p->id, p->model, p == chat.provider() ? "  (current)" : "",
                    cfg_missing(p) ? "  (incomplete)" : "");
            note(buf, A_DIM);
        }
        note("/model NAME, /model NAME:MODEL or /model MODEL switches.", A_DIM);
        return;
    }
    {
        char id[40], *colon;
        const char *model = NULL;
        strncpy(id, arg, sizeof id - 1);
        id[sizeof id - 1] = 0;
        if ((colon = strchr(id, ':')) != NULL) {
            *colon = 0;
            model = colon + 1;
        } else if (!cfg_provider(cfg, id)) {   /* a model name for the current provider */
            model = arg;
            strcpy(id, chat.provider()->id);
        }
        if (chat.switchProvider(id, model)) {
            note(chat.errorText(), A_ERR);
            return;
        }
        sprintf(buf, "Now %s", chat.describe());
        note(buf, A_DIM);
    }
}

/* --- what the agent loop needs from the screen (chat.h) ------------------- */

static void uiText(const char *text, unsigned n)
{
    static char buf[520];

    if (scr_viewing())
        scr_view(0);
    if (state == WAITING) {
        startReply();
        setState(STREAMING);
    }
    while (n) {
        unsigned k = n > 512 ? 512 : n;
        memcpy(buf, text, k);
        buf[k] = 0;
        con_write(buf);
        text += k;
        n -= k;
    }
}

static void uiThinking(void)
{
}

static void uiNote(const char *text, int error)
{
    note(text, error ? A_ERR : A_DIM);
}

/* The spinner and seconds move by themselves (scr_ticker); this only redraws
   the status row if the ticker isn't running, e.g. after the screen was handed
   to a program and back. */
static void keepStatus(void)
{
    if (state != IDLE && !scr_ticker_on() && ticks_elapsed(TICKS_NOW, lastSpin) >= 2) {
        lastSpin = TICKS_NOW;
        drawStatus();
    }
}

/* Called while the loop waits on the network: keys and the spinner. */
static void uiIdle(void)
{
    while (biosIsKeyReady())
        handleKey(biosKeyRead());
    keepStatus();
}

/* While a tool runs (tool_esc calls this): keep the connection alive and the
   spinner moving. Keys are left to tool_esc, which is looking for Esc. */
static void toolIdle(void)
{
    Net::service();
    keepStatus();
}

static int uiEscPressed(void)
{
    return stopRequested || quitting || Net::breakPressed;
}

static void uiLog(const char *line)
{
    FILE *f = fopen(LOG_PATH, "a");
    struct dosdate_t d;
    struct dostime_t t;

    if (f == NULL)
        return;
    _dos_getdate(&d);
    _dos_gettime(&t);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u %s\n", d.year, d.month, d.day, t.hour, t.minute, t.second, line);
    fclose(f);
}

static ChatUi chatUi = { uiText, uiThinking, uiNote, tools_call, uiIdle, uiEscPressed, uiLog };


/* --- tool calls (ui.h) ----------------------------------------------------- */

void ui_tool_header(const char *title, const char *arg)
{
    char buf[SCR_COLS];

    if (scr_viewing())
        scr_view(0);
    if (state == STREAMING)
        state = WAITING;              /* text after the tool starts a new bullet */
    if (*arg)
        sprintf(buf, "%.20s(%.60s", title, arg);
    else
        sprintf(buf, "%.20s", title);
    if (strlen(buf) > SCR_COLS - 6)
        strcpy(buf + SCR_COLS - 9, "...");
    blankLine();
    con_margins(0, SCR_COLS - 1);
    con_attr(A_GOOD);
    con_putc(G_BULLET);
    con_putc(' ');
    con_margins(2, SCR_COLS - 1);
    con_attr(A_BRIGHT);
    con_write(buf);
    if (*arg)
        con_write(")");
    con_newline();
}

void ui_tool_detail(const char *text)
{
    con_margins(0, SCR_COLS - 1);
    con_attr(A_DIM);
    con_write("    ");
    con_margins(4, SCR_COLS - 1);
    con_write(text);
    con_newline();
}

void ui_tool_note(const char *text, int error)
{
    note(text, error ? A_ERR : A_DIM);
}

int ui_ask(const char *question)
{
    Row r;
    int key;

    rowBlank(r);
    rowText(r, 1, "? ", A_ACCENT);
    rowText(r, 3, question, A_BRIGHT);
    scr_putn(footerTop(), 0, r.c, SCR_COLS);
    while (biosIsKeyReady())          /* typed before the question appeared */
        biosKeyRead();
    do {
        key = biosKeyRead() & 0xFF;
        if (key == 'Y')
            key = 'y';
        if (key == 'N')
            key = 'n';
    } while (key != 'y' && key != 'n' && key != KEY_ESC);
    drawStatus();
    return key;
}

void ui_tool_busy(const char *verb)
{
    toolVerb = verb;
    drawStatus();
}

void ui_tool_done(void)
{
    /* Ctrl-Break during a tool (say, to stop a program under run) shouldn't
       also end the chat. */
    Net::breakPressed = 0;
    toolVerb = NULL;
    drawStatus();
    placeCursor();
}

void ui_redraw(void)
{
    scr_footer(footerRows(), A_TEXT);
    drawFooter();
}

void ui_local_address(char *buf)
{
    Net::localAddress(buf);
}


/* --- keyboard -------------------------------------------------------------- */

static void recall(int pos)
{
    histPos = pos;
    setInput(pos < 0 ? "" : history[pos]);
}

static void handleKey(unsigned key)
{
    unsigned char ch = (unsigned char)(key & 0xFF);
    unsigned char scan = (unsigned char)(key >> 8);
    int lineBefore = cursorLine();

    if (scan == SCAN_PGUP && (ch == 0 || ch == 0xE0)) {
        scr_view(scr_viewing() + 10);
        return;
    }
    if (scan == SCAN_PGDN && (ch == 0 || ch == 0xE0)) {
        scr_view(scr_viewing() - 10);
        return;
    }
    if (scr_viewing())
        scr_view(0);

    if (ch == 0 || ch == 0xE0) {
        switch (scan) {
        case SCAN_F10:
        case SCAN_ALTX: quitting = 1; break;
        case SCAN_F1:   showHelp(); break;
        case SCAN_F2:   if (state == IDLE) newChat(); break;
        case SCAN_F4:   memories(); return;
        case SCAN_F3:
            tools_toggle_safe();
            note(tools_safe() ? "Safe mode on: changes need your OK."
                              : "Safe mode off: tools run without asking.",
                 tools_safe() ? A_GOOD : A_ERR);
            drawHint();
            placeCursor();
            return;
        case SCAN_LEFT:  if (inputPos > 0) inputPos--; break;
        case SCAN_RIGHT: if (inputPos < inputLen) inputPos++; break;
        case SCAN_HOME:  inputPos = 0; break;
        case SCAN_END:   inputPos = inputLen; break;
        case SCAN_UP:    if (histPos + 1 < histCount) recall(histPos + 1); return;
        case SCAN_DOWN:  if (histPos >= 0) recall(histPos - 1); return;
        case SCAN_DEL:
            if (inputPos < inputLen) {
                memmove(input + inputPos, input + inputPos + 1, inputLen - inputPos);
                inputLen--;
            }
            break;
        default: return;
        }
        layoutInput(lineBefore);
        return;
    }

    switch (ch) {
    case KEY_ESC:
        if (state != IDLE)
            stopRequested = 1;        /* the loop sees it at its next wait */
        else
            setInput("");
        return;
    case KEY_ENTER:
        submit();
        return;
    case KEY_CTRLN:
        if (state == IDLE)
            newChat();
        return;
    case KEY_CTRLU:
        setInput("");
        return;
    case KEY_BS:
        if (inputPos == 0)
            return;
        memmove(input + inputPos - 1, input + inputPos, inputLen - inputPos + 1);
        inputPos--;
        inputLen--;
        break;
    default:
        if (ch < 32 || inputLen >= INPUT_MAX)
            return;
        memmove(input + inputPos + 1, input + inputPos, inputLen - inputPos + 1);
        input[inputPos++] = ch;
        inputLen++;
        break;
    }
    input[inputLen] = 0;
    layoutInput(lineBefore);
}


/* --- main ------------------------------------------------------------------ */

static void fatal(const char *text)
{
    char line[200];
    sprintf(line, "fatal: %.180s", text);
    uiLog(line);
    blankLine();
    con_margins(0, SCR_COLS - 1);
    con_attr(A_ERR);
    con_putc(G_BULLET);
    con_putc(' ');
    con_margins(2, SCR_COLS - 1);
    con_write(text);
    con_newline();
    statusNote = "Press any key to exit.";
    setState(IDLE);
    if (!quitAfterReply)              /* unattended runs have nobody to press it */
        biosKeyRead();
    chat.close();
    Net::stopStack();
    scr_done();
    exit(1);
}

/* --- relaunch around a run (relaunch.h) --------------------------------------- */

static int writeState(void)
{
    FILE *f = fopen(RELAUNCH_STATE, "w");
    const Convo *c = chat.state();

    if (!f)
        return -1;
    fprintf(f, "chat %s\ntool %s\nround %d\nmerge %d\nmergeat %d\nassistant %d\n",
            chat.chatPath(), chat.pendingToolId(), chat.pendingRound(),
            c->mergeNext, c->mergeAtStart, c->assistantInTurn);
    fprintf(f, "safe %d\nquick %d\nquit %d\nhost %s\nprovider %s\nmodel %s\nticks %lu\n",
            tools_safe(), quickFlag, quitAfterReply, host,
            chat.provider()->id, chat.provider()->model, TICKS_NOW);
    return fclose(f) ? -1 : 0;
}

#define STATE_STR(key, field) if (!strcmp(line, key)) { strncpy(r->field, v, sizeof r->field - 1); continue; }
#define STATE_INT(key, field) if (!strcmp(line, key)) { r->field = atoi(v); continue; }

static int readState(Resume *r)
{
    FILE *f = fopen(RELAUNCH_STATE, "r");
    char line[200];

    if (!f)
        return -1;
    memset(r, 0, sizeof *r);
    while (fgets(line, sizeof line, f)) {
        char *v = strchr(line, ' ');
        if (!v)
            continue;
        *v++ = 0;
        v[strcspn(v, "\r\n")] = 0;
        STATE_STR("chat", chat) STATE_STR("tool", tool) STATE_STR("provider", provider)
        STATE_STR("model", model) STATE_STR("host", host)
        STATE_INT("round", round) STATE_INT("merge", merge) STATE_INT("mergeat", mergeAtStart)
        STATE_INT("assistant", assistant) STATE_INT("safe", safe) STATE_INT("quick", quick)
        STATE_INT("quit", quit)
        if (!strcmp(line, "ticks"))
            r->ticks = strtoul(v, NULL, 10);
    }
    fclose(f);
    return r->chat[0] && r->tool[0] ? 0 : -1;
}

/* The chat screen, the state behind it and the Up/Down input history, to a
   file: the in-process run keeps them in memory, which a relaunch loses. scr_suspend also leaves a plain DOS
   screen for the program. */
static int saveScreenFile(void)
{
    unsigned short __far *buf = (unsigned short __far *)malloc(SCR_SAVE_BYTES);
    ScrState ss;
    ConState cs;
    int fd, rc = -1;

    if (!buf)
        return -1;
    scr_state_get(&ss);
    con_state_get(&cs);
    scr_suspend(buf);
    fd = open(RELAUNCH_SCREEN, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
    if (fd >= 0) {
        rc = 0;
        if (write(fd, buf, SCR_SAVE_BYTES) != (int)SCR_SAVE_BYTES
            || write(fd, &ss, sizeof ss) != sizeof ss || write(fd, &cs, sizeof cs) != sizeof cs
            || write(fd, &histCount, sizeof histCount) != sizeof histCount
            || write(fd, history, histCount * sizeof history[0]) != (int)(histCount * sizeof history[0]))
            rc = -1;
        close(fd);
    }
    free(buf);
    return rc;
}

static int restoreScreenFile(void)
{
    unsigned short __far *buf = (unsigned short __far *)malloc(SCR_SAVE_BYTES);
    ScrState ss;
    ConState cs;
    int fd, n, rc = -1;

    if (!buf)
        return -1;
    fd = open(RELAUNCH_SCREEN, O_RDONLY | O_BINARY);
    if (fd >= 0) {
        if (read(fd, buf, SCR_SAVE_BYTES) == (int)SCR_SAVE_BYTES
            && read(fd, &ss, sizeof ss) == sizeof ss && read(fd, &cs, sizeof cs) == sizeof cs) {
            scr_state_set(&ss);
            con_state_set(&cs);
            scr_resume(buf);
            rc = 0;
            /* the Up/Down input history follows */
            if (read(fd, &n, sizeof n) == sizeof n && n > 0 && n <= HIST_N
                && read(fd, history, n * sizeof history[0]) == (int)(n * sizeof history[0])) {
                histCount = n;
                histPos = -1;
            }
        }
        close(fd);
        remove(RELAUNCH_SCREEN);
    }
    free(buf);
    return rc;
}

/* A run goes to the launcher: save what a relaunched CLAWDOS needs, give the
   memory back and leave with the exit code CLAWDOS.EXE watches for. */
static void relaunchExit(void)
{
    if (writeState())
        fatal("Cannot write " RELAUNCH_STATE);
    saveScreenFile();
    chat.close();
    /* mTCP's endStack prints "End: heap is corrupted!" whenever Open Watcom's
       _fheapchk() is unhappy, and in this build it is unhappy with any far heap
       at all (DOSUNIT: one malloc is enough). A normal exit resets the screen
       over it; here the program's screen would start with it. */
    freopen("NUL", "w", stderr);
    scr_ticker_off();                 /* before mTCP puts back the INT 1Ch it hooked under ours */
    Net::stopStack();
    exit(RELAUNCH_EXIT);
}

static void secsText(char *out, unsigned long ticks)
{
    unsigned long t = ticks * 100 / 182;    /* tenths of a second */
    sprintf(out, "%lu.%lu", t / 10, t % 10);
}

/* Back from a relaunch: the launcher's result under the Run header, then the
   rest of the turn. */
static void resumeTurn(void)
{
    ToolCtx t;
    int status = TS_ERROR;
    unsigned long progTicks = 0;
    char line[90], a[16], b[16];
    int8_t rc;

    tout_reset(&t);
    if (relaunch_read_out(&t, &status, &progTicks)) {
        tout_puts(&t, "the launcher left no result");
        strcpy(t.summary, "no result from the launcher");
        status = TS_ERROR;
    }
    setState(WAITING);
    tools_show_result(status, &t);
    secsText(a, progTicks);
    secsText(b, ticks_elapsed(TICKS_NOW, resumeState.ticks));
    sprintf(line, "relaunch: program %s s, CLAWDOS away %s s", a, b);
    uiLog(line);
    stopRequested = 0;
    rc = chat.continueTurn(resumeState.round, resumeState.tool, status, &t);
    if (rc == 2)
        relaunchExit();
    if (rc == 1)
        note("Interrupted by user", A_ERR);
    else if (rc < 0 && quitAfterReply)
        exitCode = 1;
    finished();
}

int main(int argc, char *argv[])
{
    char note_[120];
    int unsafe = 0, quick = 0;
    const char *firstMessage = NULL;
    const char *prefill = NULL;
    const char *providerArg = NULL;
    int i, bad, openMemories = 0;

    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "-unsafe")) {
            unsafe = 1;
        } else if (!stricmp(argv[i], "-q")) {
            quick = 1;
        } else if (!stricmp(argv[i], "-m") && i + 1 < argc) {
            firstMessage = argv[++i];
        } else if (!stricmp(argv[i], "-i") && i + 1 < argc) {
            prefill = argv[++i];
        } else if (!stricmp(argv[i], "-p") && i + 1 < argc) {
            providerArg = argv[++i];
        } else if (!stricmp(argv[i], "-launched")) {
            tools_set_launched(1);    /* CLAWDOS.EXE is our parent: runs go to it */
        } else if (!stricmp(argv[i], "-memories")) {
            openMemories = 1;         /* open the memory manager once connected (screenshots) */
        } else if (!stricmp(argv[i], "-resume")) {
            resumeMode = 1;
        } else if (argv[i][0] == '-' || argv[i][0] == '/') {
            printf("ClawDOS v" VERSION " - chat with an AI from DOS\n\n"
                   "Usage: CLAWDOS [host[:port]] [-p provider] [-unsafe] [-q]\n"
                   "       (CLAWDOS.EXE launches this program, HARNESS.EXE, and takes over\n"
                   "       while a run tool program needs the memory; HARNESS alone keeps\n"
                   "       programs inside its own memory)\n"
                   "              [-m message] [-i text] [-memories]\n\n"
                   "Settings come from " CFG_PATH ". A host replaces the\n"
                   "provider's tunnel address (plain HTTP, e.g. the mock API in tests).\n"
                   "-p       use this [provider] section instead of the configured one\n"
                   "-unsafe  start with safe mode off: tools run without asking\n"
                   "-q       quit once the -m message is answered (unattended tests)\n"
                   "-m       send this message as soon as we're connected\n"
                   "-i       start with this text in the input box\n"
                   "-memories  open the memory manager (F4) once connected\n");
            return 1;
        } else {
            strncpy(host, argv[i], sizeof(host) - 1);
        }
    }

    if (resumeMode) {                 /* the flags come from the CLAWDOS that left */
        if (readState(&resumeState)) {
            fprintf(stderr, "CLAWDOS -resume: nothing to resume (%s).\n", RELAUNCH_STATE);
            return 1;
        }
        unsafe = !resumeState.safe;
        quick = resumeState.quick;
        quitAfterReply = resumeState.quit;
        providerArg = resumeState.provider;
        strcpy(host, resumeState.host);
    } else {
        quitAfterReply = quick && firstMessage != NULL;
    }
    tools_init(!unsafe, quick);
    quickFlag = quick;
    cfg = (Cfg *)malloc(sizeof(Cfg));
    if (cfg == NULL) {
        fprintf(stderr, "Out of memory.\n");
        return 1;
    }
    cfg_defaults(cfg);
    bad = cfg_load(cfg, CFG_PATH);
    if (bad == -1) {
        fprintf(stderr, "Cannot read %s.\nIt holds the tunnel address, the provider and the API key.\n", CFG_PATH);
        return 1;
    }
    if (bad > 0) {
        fprintf(stderr, "%s line %d: unknown setting or too many providers.\n", CFG_PATH, bad);
        return 1;
    }
    if (Net::startStack())
        return 1;
    tool_idle = toolIdle;              /* keep the connection alive and the spinner moving during tools */

    scr_init();
    con_track_cursor(0);            /* the cursor lives in the input box */
    con_init();
    wrapInput();                    /* lineCount 0 put the cursor a row up, on the box's edge */
    scr_footer(footerRows(), A_TEXT);
    if (resumeMode)
        restoreScreenFile();          /* the chat as it was when the run began */
    sprintf(note_, "Connecting to %.60s ...", host[0] ? host : "the tunnel");
    statusNote = note_;
    drawFooter();

    if (resumeMode) {
        Convo c;
        c.mergeNext = resumeState.merge;
        c.mergeAtStart = resumeState.mergeAtStart;
        c.assistantInTurn = resumeState.assistant;
        if (chat.resume(cfg, providerArg, resumeState.model, host[0] ? host : NULL, &chatUi,
                        resumeState.chat, &c))
            fatal(chat.errorText());
        remove(RELAUNCH_STATE);       /* resumed; until now CLAWDOS -resume could try again */
        statusNote = NULL;
        drawFooter();
        resumeTurn();
    } else {
        if (chat.init(cfg, providerArg, host[0] ? host : NULL, &chatUi))
            fatal(chat.errorText());
        welcome();
        statusNote = NULL;
        drawFooter();
        if (firstMessage)
            setInput(firstMessage), submit();
        if (prefill)
            setInput(prefill);
        if (openMemories)
            memories();
    }

    while (!quitting) {
        if (Net::breakPressed)
            break;
        if (biosIsKeyReady())
            handleKey(biosKeyRead());
        else                          /* rest until a key, a packet or the timer: polling */
            _asm { sti                /* the card nonstop made the PC whine audibly */
                   hlt }
        Net::service();
    }

    chat.close();
    Net::stopStack();
    scr_done();
    printf("Thanks for chatting!\n");
    return exitCode;
}
