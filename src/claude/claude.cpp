/* CLAUDE.EXE - chat with Claude from an IBM PC XT.
 *
 * Connects to an XTBridge (bridge/xtbridge.py) over TCP, sends what you
 * type, and streams the reply onto a word-wrapped, hardware-scrolled CGA
 * screen. The look follows Claude Code: a welcome panel with Clawd, "> "
 * for your messages, a bullet for Claude's, a spinner while it works, and an
 * input box pinned to the bottom of the screen.
 *
 *   CLAUDE [host[:port]] [-nosnow] [-noscroll] [-watch] [-unsafe] [-q] [-m message] [-i text]
 *
 * Claude can also use tools on this machine (tools.cpp); in safe mode (F3)
 * anything that would change files, memory or ports asks first.
 *
 * Without a host, uses CLAUDE_BRIDGE from the mTCP config file (MTCPCFG), and
 * CLAUDE_TOKEN if the bridge requires one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>

#include "types.h"
#include "inlines.h"
#include "xtbridge.h"
#include "screen.h"
#include "console.h"
#include "tools.h"
#include "toolapi.h"
#include "ui.h"

#define VERSION   "0.2"
#define INPUT_MAX 900
#define HIST_N    8

/* Colours. CGA has no orange; brown is the nearest, and on an IBM 5153
   monitor it is a warm dark orange. */
#define A_CLAWD   SCR_ATTR(C_BROWN, C_BLACK)
#define A_ACCENT  SCR_ATTR(C_LIGHTRED, C_BLACK)
#define A_TITLE   SCR_ATTR(C_WHITE, C_BLACK)
#define A_TEXT    SCR_ATTR(C_LIGHTGRAY, C_BLACK)
#define A_BRIGHT  SCR_ATTR(C_WHITE, C_BLACK)
#define A_DIM     SCR_ATTR(C_DARKGRAY, C_BLACK)
#define A_BOX     SCR_ATTR(C_DARKGRAY, C_BLACK)
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

static XTBridge bridge;
static State state = IDLE;
static int quitting;
static int queued;                    /* Enter pressed while Claude was busy */
static int quitAfterReply;            /* -q: leave once the -m message is answered */
static const char *toolVerb;          /* spinner verb while a tool runs */
static char host[80];
static char backend[80];
static char token[64];

/* Link liveness: the bridge answers PING at any time, even mid-reply. */
#define PING_IDLE   (60 * 18)         /* ticks of silence before a PING when idle */
#define PING_BUSY   (20 * 18)         /* ...and while waiting for Claude */
#define PONG_WAIT   (15 * 18)         /* no answer by then: the link is dead */
#define RETRY_WAIT  (10 * 18)         /* between reconnect attempts */
static int linkUp;
static unsigned long lastRx;          /* ticks when the bridge last sent a line */
static int pinged;
static unsigned long pingedAt;
static unsigned long retryAt;
static char offlineNote[100];

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

static volatile unsigned long __far *biosTicks = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);


/* --- building rows ------------------------------------------------------- */

struct Row {
    unsigned short c[SCR_COLS];
};

static void rowBlank(Row &r)
{
    for (int i = 0; i < SCR_COLS; i++)
        r.c[i] = ' ' | (A_TEXT << 8);
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
        unsigned long now = *biosTicks;
        unsigned secs = (unsigned)((now - busySince) * 10 / 182);
        int col;

        r.c[1] = spinGlyphs[(unsigned)(now >> 1) & 7] | (A_CLAWD << 8);
        col = rowText(r, 3, toolVerb ? toolVerb : verbs[verbIndex], A_ACCENT);
        col = rowText(r, col, "... ", A_ACCENT);
        sprintf(buf, "(%us %c esc to interrupt%s)", secs, G_DOT,
                queued ? " \xFA 1 message queued" : "");
        rowText(r, col, buf, A_DIM);
    } else if (statusNote) {
        rowText(r, 1, statusNote, A_DIM);
    }
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

    rowBlank(r);
    r.c[0] = G_V | (A_BOX << 8);
    r.c[SCR_COLS - 1] = G_V | (A_BOX << 8);
    if (line == 0)
        r.c[2] = '>' | (A_BRIGHT << 8);
    if (inputLen == 0 && i == 0) {
        rowText(r, BOX_TEXT, state == IDLE ? "Try \"what is an 8088?\"  (F1 for help)" : "",
                A_DIM);
    } else if (line < lineCount) {
        int first = lineStart[line];
        int n = lineEnd(line) - first;
        for (int k = 0; k < n; k++)
            r.c[BOX_TEXT + k] = (unsigned char)input[first + k] | (A_BRIGHT << 8);
    }
    scr_putn(footerTop() + 2 + i, 0, r.c, SCR_COLS);
}

static void drawHint(void)
{
    Row r;
    int col;

    rowBlank(r);
    col = rowText(r, 2, "F1 help \xFA F2 new chat \xFA F3 ", A_DIM);
    col = rowText(r, col, tools_safe() ? "safe:on" : "safe:off", tools_safe() ? A_GOOD : A_ERR);
    rowText(r, col, " \xFA PgUp/PgDn \xFA F10 quit", A_DIM);
    col = SCR_COLS - 2 - strlen(host) - 2;
    r.c[col] = G_BULLET | ((bridge.isConnected() ? A_GOOD : A_ERR) << 8);
    rowText(r, col + 2, host, A_DIM);
    scr_putn(SCR_ROWS - 1, 0, r.c, SCR_COLS);
}

static void drawFooter(void)
{
    Row r;
    int top = footerTop();

    drawStatus();
    rowBlank(r);
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
        "/model lists the bridge's AI providers \xFA /model name switches",
        "F3 toggles safe mode (asks before tools change files, memory or ports)",
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
    XTBridge::localAddress(buf + strlen(buf));
}

/* Claude Code's welcome panel: Clawd on the left, tips on the right. */
static void welcome(void)
{
    /* Clawd in half blocks: 12x8 "pixels", eyes are the holes. */
    static const char *const clawd[4] = {
        " \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB ",
        "\xDC\xDB\xDB \xDB\xDB\xDB\xDB \xDB\xDB\xDC",
        "\xDF\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDF",
        " \xDF \xDF    \xDF \xDF ",
    };
    enum { L = 1, LW = 38, MID = 39, R = 42, RW = SCR_COLS - 2 - 42 };
    char sys[48];
    Row r;
    int i;

    systemLine(sys);
    for (i = 0; i < 12; i++) {
        rowBlank(r);
        if (i == 0) {
            int col;
            rowFrame(r, G_TL, G_H, G_TR, A_CLAWD);
            col = rowText(r, 4, " Claude for the IBM PC XT ", A_TITLE);
            rowText(r, col - 1, " v" VERSION " ", A_DIM);
            r.c[MID] = G_TDOWN | (A_CLAWD << 8);
        } else if (i == 11) {
            rowFrame(r, G_BL, G_H, G_BR, A_CLAWD);
            r.c[MID] = G_TUP | (A_CLAWD << 8);
        } else {
            r.c[0] = r.c[MID] = r.c[SCR_COLS - 1] = G_V | (A_CLAWD << 8);
        }
        switch (i) {
        case 2: rowCenter(r, L, LW, "Welcome to Claude!", A_TITLE); break;
        case 4: case 5: case 6: case 7: rowCenter(r, L, LW, clawd[i - 4], A_CLAWD); break;
        case 9: rowCenter(r, L, LW, backend, A_DIM); break;
        case 10: rowCenter(r, L, LW, host, A_DIM); break;
        }
        switch (i) {
        case 2: rowText(r, R, "Tips for getting started", A_ACCENT); break;
        case 3: rowText(r, R, "Type below and press Enter to chat", A_TEXT, RW); break;
        case 4: rowText(r, R, "Esc stops \xFA F2 new chat \xFA F3 safe", A_TEXT, RW); break;
        case 5: rowText(r, R, "PgUp/PgDn scroll back \xFA F1 help", A_TEXT, RW); break;
        case 6:
            rowFill(r, MID + 1, SCR_COLS - MID - 2, G_H, A_CLAWD);
            r.c[MID] = 0xC3 | (A_CLAWD << 8);              /* tee joints */
            r.c[SCR_COLS - 1] = 0xB4 | (A_CLAWD << 8);
            break;
        case 7: rowText(r, R, "This machine", A_ACCENT); break;
        case 8: rowText(r, R, sys, A_TEXT, RW); break;
        case 9: rowText(r, R, "Claude-XT \xFA FreeDOS \xFA mTCP", A_TEXT, RW); break;
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
        busySince = *biosTicks;
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
    bridge.send("NEW");
    con_init();
    welcome();
    blankLine();
    note("New conversation.", A_DIM);
    drawFooter();
}

static void submit(void);

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
    if (!stricmp(input, "/quit") || !stricmp(input, "/exit")) {
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
    if (!linkUp) {
        note("Not connected to the bridge; your message is still in the box.", A_ERR);
        return;
    }
    if (!stricmp(input, "/new") || !stricmp(input, "/clear")) {
        setInput("");
        newChat();
        return;
    }
    if (!stricmp(input, "/help")) {
        setInput("");
        showHelp();
        return;
    }
    echoUser(input);
    if (bridge.send("MSG", input)) {
        note(bridge.errorText(), A_ERR);
        return;                       /* keep the text for another try */
    }
    setState(WAITING);
    setInput("");
}

static void handleLine(const char *verb, const char *payload)
{
    if (tools_line(verb, payload))
        return;
    if (!strcmp(verb, "TXT") || !strcmp(verb, "NL")) {
        if (scr_viewing())
            scr_view(0);
        if (state == WAITING) {
            startReply();
            setState(STREAMING);
        }
        if (verb[0] == 'T')
            con_write(payload);
        else
            con_newline();
    } else if (!strcmp(verb, "END")) {
        tools_stopping(0);
        if (*payload)
            note("Interrupted by user", A_ERR);
        finished();
    } else if (!strcmp(verb, "ERR")) {
        tools_stopping(0);
        note(payload, A_ERR);
        finished();
    }
}


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
    XTBridge::breakPressed = 0;
    toolVerb = NULL;
    drawStatus();
    placeCursor();
}

void ui_redraw(void)
{
    scr_footer(footerRows(), A_TEXT);
    drawFooter();
}

int ui_send(const char *verb, const char *payload)
{
    return bridge.send(verb, payload);
}

void ui_local_address(char *buf)
{
    XTBridge::localAddress(buf);
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
        if (state != IDLE) {
            bridge.send("STOP");
            tools_stopping(1);        /* calls already on their way won't run */
        } else
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
    bridge.close();
    XTBridge::stopStack();
    scr_done();
    exit(1);
}

/* Wait up to `secs` for the next protocol line. */
static int waitLine(int secs)
{
    unsigned long start = *biosTicks;
    while (*biosTicks - start < (unsigned long)secs * 18) {
        int rc = bridge.poll();
        if (rc != 0)
            return rc;
        if (XTBridge::breakPressed)
            return -1;
    }
    return 0;
}

/* Connect and say hello. Returns NULL, or what went wrong (link closed). */
static const char *connectBridge(void)
{
    if (bridge.connect(host, XTBRIDGE_PORT))
        return bridge.errorText();
    if (waitLine(10) <= 0 || strcmp(bridge.verb(), "HELLO")) {
        bridge.close();
        return "The bridge didn't say hello.";
    }
    if (strstr(bridge.payload(), "AUTH")) {
        bridge.send("AUTH", token);
        if (waitLine(10) <= 0 || strcmp(bridge.verb(), "OK")) {
            bridge.close();
            return "The bridge rejected our CLAUDE_TOKEN.";
        }
    }
    bridge.send("INFO");
    if (waitLine(10) > 0 && !strcmp(bridge.verb(), "INFO"))
        strncpy(backend, bridge.payload(), sizeof(backend) - 1);
    linkUp = 1;
    lastRx = *biosTicks;
    pinged = 0;
    return NULL;
}

/* The link is gone: say so, drop whatever was in flight, and retry later
   instead of leaving the chat. */
static void linkLost(const char *why)
{
    bridge.close();
    linkUp = 0;
    tools_reset();
    toolVerb = NULL;
    queued = 0;
    if (state != IDLE)
        note("The reply was cut off.", A_ERR);
    note(why, A_ERR);
    setState(IDLE);
    if (quitAfterReply) {             /* unattended: one reconnect, then fail */
        const char *err = connectBridge();
        fatal(err ? err : "Lost the bridge mid-reply (reconnected, then gave up).");
    }
    retryAt = *biosTicks + 5 * 18;
    sprintf(offlineNote, "Bridge offline; reconnecting to %s ...", host);
    statusNote = offlineNote;
    drawStatus();
    drawHint();
}

static void reconnect(void)
{
    const char *err;

    sprintf(offlineNote, "Reconnecting to %s ...", host);
    statusNote = offlineNote;
    drawStatus();
    err = connectBridge();
    if (err) {
        sprintf(offlineNote, "Bridge offline (%.40s) - retrying in 10s. F10 quits.", err);
        drawStatus();
        retryAt = *biosTicks + RETRY_WAIT;
        return;
    }
    statusNote = NULL;
    note("Reconnected to the bridge. It doesn't remember the earlier conversation.", A_DIM);
    drawFooter();
}

/* -watch: catch other programs writing to video memory. Every half second
   part of VRAM is compared with what we wrote; a difference is reported to
   the bridge log (with the BIOS video state and interrupt vectors, which show
   whether someone went through INT 10h or hooked something) and repaired. */
#define WATCH_CELLS 2048              /* compared per check: all 16 KB in 2 s */
static const unsigned char watchVecs[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                           0x10, 0x13, 0x16, 0x1C, 0x21, 0x28, 0x2F, 0x60 };
static int watching;
static unsigned long watchAt, watchReports;
static unsigned long startVecs[sizeof(watchVecs)];
static unsigned char startBios[0x1E];        /* 0040:0049-0066, the BIOS video area */
static unsigned char __far *biosVideo = (unsigned char __far *)MK_FP(0x40, 0x49);

static unsigned long vector(unsigned char n)
{
    return *(unsigned long __far *)MK_FP(0, n * 4);
}

static void watchStart(void)
{
    unsigned short __far *buf = (unsigned short __far *)malloc(SCR_SAVE_BYTES);

    if (buf == NULL)
        return;
    for (unsigned i = 0; i < sizeof(watchVecs); i++)
        startVecs[i] = vector(watchVecs[i]);
    _fmemcpy(startBios, biosVideo, sizeof(startBios));
    scr_watch(buf);
    watching = 1;
}

static void watchLog(const char *text)
{
    if (linkUp)
        bridge.send("LOG", text);
}

static void watchCheck(unsigned cells)
{
    ScrDiff d;
    char buf[160];
    int n, len;
    unsigned long now = *biosTicks;

    n = scr_check(cells, &d);
    if (n == 0)
        return;
    watchReports++;
    sprintf(buf, "VRAM watch #%lu: %d cells changed by someone else; first run %04X-%04X (cell 0 = B800:0000)."
                 " Live row 0 is cell %04X; state %d; %lus since the last bridge line.",
            watchReports, n, d.first, d.last, scr_top(), (int)state, (now - lastRx) * 10 / 182);
    watchLog(buf);
    len = sprintf(buf, "VRAM found   ");
    for (int i = 0; i < 8; i++)
        len += sprintf(buf + len, " %04X", d.found[i]);
    watchLog(buf);
    len = sprintf(buf, "VRAM expected");
    for (int i = 0; i < 8; i++)
        len += sprintf(buf + len, " %04X", d.expected[i]);
    watchLog(buf);
    len = sprintf(buf, "BIOS video 0040:0049..0066 changed at:");
    for (unsigned i = 0; i < sizeof(startBios); i++)
        if (biosVideo[i] != startBios[i])
            len += sprintf(buf + len, " %02X:%02X->%02X", 0x49 + i, startBios[i], biosVideo[i]);
    if (len == 38)
        strcpy(buf + len, " nothing");
    watchLog(buf);
    len = sprintf(buf, "Vectors changed since start:");
    for (unsigned i = 0; i < sizeof(watchVecs); i++) {
        unsigned long v = vector(watchVecs[i]);
        if (v != startVecs[i] && len < 120)
            len += sprintf(buf + len, " %02X %04X:%04X->%04X:%04X", watchVecs[i],
                           (unsigned)(startVecs[i] >> 16), (unsigned)startVecs[i],
                           (unsigned)(v >> 16), (unsigned)v);
    }
    if (len == 28)
        strcpy(buf + len, " none");
    watchLog(buf);
    scr_repair();
    sprintf(buf, "Something else wrote to screen memory (cells %04X-%04X); repaired. Details are in the bridge log.",
            d.first, d.last);
    note(buf, A_ERR);
    drawFooter();
}

/* Ping a quiet bridge; give up on one that doesn't answer. */
static void checkLink(void)
{
    unsigned long now = *biosTicks;

    if (pinged) {
        if (now - pingedAt > PONG_WAIT)
            linkLost("The bridge stopped answering.");
    } else if (now - lastRx > (state == IDLE ? PING_IDLE : PING_BUSY)) {
        if (bridge.send("PING"))
            linkLost(bridge.errorText());
        else
            pinged = 1, pingedAt = now;
    }
}

int main(int argc, char *argv[])
{
    char note_[120];
    int nosnow = 0, noscroll = 0, watch = 0, unsafe = 0, quick = 0;
    const char *firstMessage = NULL;
    const char *prefill = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "-nosnow") || !stricmp(argv[i], "/nosnow")) {
            nosnow = 1;
        } else if (!stricmp(argv[i], "-noscroll") || !stricmp(argv[i], "/noscroll")) {
            noscroll = 1;
        } else if (!stricmp(argv[i], "-watch") || !stricmp(argv[i], "/watch")) {
            watch = 1;
        } else if (!stricmp(argv[i], "-unsafe") || !stricmp(argv[i], "/unsafe")) {
            unsafe = 1;
        } else if (!stricmp(argv[i], "-q") || !stricmp(argv[i], "/q")) {
            quick = 1;
        } else if ((!stricmp(argv[i], "-m") || !stricmp(argv[i], "/m")) && i + 1 < argc) {
            firstMessage = argv[++i];
        } else if ((!stricmp(argv[i], "-i") || !stricmp(argv[i], "/i")) && i + 1 < argc) {
            prefill = argv[++i];
        } else if (argv[i][0] == '-' || argv[i][0] == '/') {
            printf("CLAUDE " VERSION " - chat with Claude via an XTBridge\n\n"
                   "Usage: CLAUDE [host[:port]] [-nosnow] [-noscroll] [-watch] [-unsafe] [-q]\n"
                   "              [-m message] [-i text]\n\n"
                   "Without a host, CLAUDE_BRIDGE from the mTCP config file is used.\n"
                   "-nosnow  skip CGA retrace waits (faster on clones and EGA/VGA)\n"
                   "-noscroll  scroll by copying text, not by moving the CGA start address\n"
                   "-watch   report other programs' writes to video memory to the bridge log\n"
                   "-unsafe  start with safe mode off: tools run without asking\n"
                   "-q       quit once the -m message is answered (unattended tests)\n"
                   "-m       send this message as soon as we're connected\n"
                   "-i       start with this text in the input box\n");
            return 1;
        } else {
            strncpy(host, argv[i], sizeof(host) - 1);
        }
    }

    tools_init(!unsafe, quick);
    quitAfterReply = quick && firstMessage != NULL;
    if (XTBridge::startStack())
        return 1;
    tool_idle = XTBridge::service;    /* keep ACKing the bridge during long tools */
    if (!host[0] && XTBridge::configValue("CLAUDE_BRIDGE", host, sizeof(host)) != 0) {
        fprintf(stderr, "No bridge given. Use CLAUDE host[:port], or add a line like\n"
                        "  CLAUDE_BRIDGE 192.168.1.10:8023\n"
                        "to your mTCP config file (%s).\n", getenv("MTCPCFG"));
        XTBridge::stopStack();
        return 1;
    }
    XTBridge::configValue("CLAUDE_TOKEN", token, sizeof(token));

    scr_init();
    if (nosnow)
        scr_set_snowcheck(0);
    if (noscroll)
        scr_set_hwscroll(0);
    if (watch)
        watchStart();
    con_track_cursor(0);            /* the cursor lives in the input box */
    con_init();
    scr_footer(footerRows(), A_TEXT);
    sprintf(note_, "Connecting to the bridge at %s ...", host);
    statusNote = note_;
    drawFooter();

    const char *err = connectBridge();
    if (err)
        fatal(err);

    welcome();
    statusNote = NULL;
    drawFooter();
    if (firstMessage)
        setInput(firstMessage), submit();
    if (prefill)
        setInput(prefill);

    unsigned long lastSpin = *biosTicks;

    while (!quitting) {
        if (linkUp) {
            int rc = bridge.poll();
            if (rc > 0) {
                pinged = 0;
                handleLine(bridge.verb(), bridge.payload());
                lastRx = *biosTicks;      /* after it: a tool may have run a while */
                continue;
            }
            if (rc < 0)
                linkLost(bridge.errorText());
            else
                checkLink();
        } else if ((long)(*biosTicks - retryAt) >= 0) {
            reconnect();
        }
        if (watching && *biosTicks - watchAt >= 9) {
            watchAt = *biosTicks;
            watchCheck(WATCH_CELLS);
        }
        if (XTBridge::breakPressed)
            break;
        if (biosIsKeyReady())
            handleKey(biosKeyRead());
        if (state != IDLE && *biosTicks - lastSpin >= 2) {
            lastSpin = *biosTicks;
            drawStatus();
        }
    }

    if (watching)                     /* a last full look, for -q runs */
        watchCheck(8192);
    if (linkUp)
        bridge.send("QUIT");
    bridge.close();
    XTBridge::stopStack();
    scr_done();
    printf("Thanks for chatting!\n");
    return 0;
}
