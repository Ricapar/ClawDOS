/* CLAUDE.EXE - chat with Claude from an IBM PC XT.
 *
 * Connects to an XTBridge (bridge/xtbridge.py) over TCP, sends what you
 * type, and streams the reply onto a word-wrapped, hardware-scrolled CGA
 * screen.
 *
 *   CLAUDE [host[:port]] [-nosnow] [-m message]
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

#define VERSION   "0.1"
#define INPUT_MAX 900

#define A_TITLE  SCR_ATTR(C_WHITE, C_BLUE)
#define A_INFO   SCR_ATTR(C_CYAN, C_BLACK)
#define A_DIM    SCR_ATTR(C_DARKGRAY, C_BLACK)
#define A_YOU    SCR_ATTR(C_YELLOW, C_BLACK)
#define A_USER   SCR_ATTR(C_WHITE, C_BLACK)
#define A_CLAUDE SCR_ATTR(C_LIGHTRED, C_BLACK)
#define A_REPLY  SCR_ATTR(C_LIGHTGRAY, C_BLACK)
#define A_ERR    SCR_ATTR(C_YELLOW, C_RED)

#define KEY_ESC   0x011B
#define KEY_ENTER 13
#define KEY_BS    8
#define KEY_CTRLN 14
#define SCAN_F1   0x3B
#define SCAN_F2   0x3C
#define SCAN_F10  0x44
#define SCAN_ALTX 0x2D
#define SCAN_PGUP 0x49
#define SCAN_PGDN 0x51

enum State { IDLE, WAITING, STREAMING };

static XTBridge bridge;
static State state = IDLE;
static char input[INPUT_MAX + 1];
static int inputLen;
static int spinRow, spinCol;
static int quitting;

static volatile unsigned long __far *biosTicks = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);

static int keyReady(void) { return biosIsKeyReady(); }
static unsigned keyRead(void) { return biosKeyRead(); }

static void say(unsigned char attr, const char *text)
{
    con_ensure_blank_line();
    con_attr(attr);
    con_write(text);
    con_newline();
}

static void showError(const char *text)
{
    con_ensure_blank_line();
    con_attr(A_ERR);
    con_putc(' ');
    con_write(text);
    con_putc(' ');
    con_attr(A_REPLY);
    con_newline();
}

static void titleBar(void)
{
    static const char right[] = "Claude-XT v" VERSION " ";
    int i;

    con_attr(A_TITLE);
    con_write(" Claude for the IBM PC XT");
    for (i = con_col(); i < SCR_COLS - (int)sizeof(right) + 1; i++)
        con_putc(' ');
    con_write(right);   /* exactly fills the row, so the console moves to the next */
    con_attr(A_REPLY);
}

static void helpLine(void)
{
    say(A_DIM, "Enter sends.  Esc stops a reply.  PgUp/PgDn scroll.  F2 new chat.  F10 quits.");
}

static void prompt(void)
{
    con_ensure_blank_line();
    con_newline();
    con_attr(A_YOU);
    con_write("You: ");
    con_attr(A_USER);
    con_mark();
    inputLen = 0;
    state = IDLE;
    scr_cursor_visible(1);
}

static void redrawInput(void)
{
    int i;
    con_rewind();
    for (i = 0; i < inputLen; i++)
        con_putc(input[i]);
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

static void newChat(void)
{
    bridge.send("NEW");
    con_init();
    titleBar();
    say(A_INFO, "New conversation.");
    helpLine();
    prompt();
}

static void submit(void)
{
    input[inputLen] = 0;
    if (inputLen == 0)
        return;
    if (!stricmp(input, "/quit") || !stricmp(input, "/exit")) {
        quitting = 1;
        return;
    }
    if (!stricmp(input, "/new")) {
        newChat();
        return;
    }
    if (!stricmp(input, "/help")) {
        con_newline();
        helpLine();
        say(A_DIM, "Commands: /new  /quit  /help");
        prompt();
        return;
    }
    if (bridge.send("MSG", input)) {
        showError(bridge.errorText());
        prompt();
        return;
    }
    scr_cursor_visible(0);
    con_newline();
    con_newline();
    con_attr(A_CLAUDE);
    con_write("Claude: ");
    con_attr(A_REPLY);
    spinRow = con_row();
    spinCol = con_col();
    state = WAITING;
}

static void endReply(const char *how)
{
    if (state == WAITING)
        scr_putc(spinRow, spinCol, ' ', A_REPLY);
    if (how && *how) {
        con_attr(A_DIM);
        con_write(" [");
        con_write(how);
        con_write("]");
    }
    prompt();
}

static void handleLine(const char *verb, const char *payload)
{
    if (scr_viewing())
        scr_view(0);
    if (!strcmp(verb, "TXT") || !strcmp(verb, "NL")) {
        if (state == WAITING) {
            scr_putc(spinRow, spinCol, ' ', A_REPLY);
            state = STREAMING;
        }
        if (verb[0] == 'T')
            con_write(payload);
        else
            con_newline();
    } else if (!strcmp(verb, "END")) {
        endReply(payload);
    } else if (!strcmp(verb, "ERR")) {
        if (state == WAITING)
            scr_putc(spinRow, spinCol, ' ', A_REPLY);
        showError(payload);
        prompt();
    } else if (!strcmp(verb, "OK") && state == IDLE) {
        /* reply to NEW */
    }
}

static void handleKey(unsigned key)
{
    unsigned char ch = (unsigned char)(key & 0xFF);
    unsigned char scan = (unsigned char)(key >> 8);

    if (ch == 0) {
        switch (scan) {
        case SCAN_F10:
        case SCAN_ALTX: quitting = 1; break;
        case SCAN_F1:
            if (state == IDLE) {
                con_newline();
                helpLine();
                prompt();
            }
            break;
        case SCAN_F2: if (state == IDLE) newChat(); break;
        case SCAN_PGUP: scr_view(scr_viewing() + 20); break;
        case SCAN_PGDN: scr_view(scr_viewing() - 20); break;
        }
        return;
    }
    if (key == KEY_ESC) {
        if (state != IDLE)
            bridge.send("STOP");
        return;
    }
    if (state != IDLE)
        return;
    if (scr_viewing())
        scr_view(0);
    if (ch == KEY_ENTER) {
        submit();
    } else if (ch == KEY_BS) {
        if (inputLen > 0) {
            inputLen--;
            redrawInput();
        }
    } else if (ch == KEY_CTRLN) {
        newChat();
    } else if (ch >= 32 && inputLen < INPUT_MAX) {
        input[inputLen++] = ch;
        con_putc(ch);
    }
}

static void waitKeyAndExit(int rc)
{
    say(A_DIM, "Press any key to exit.");
    keyRead();
    bridge.close();
    XTBridge::stopStack();
    scr_done();
    exit(rc);
}

int main(int argc, char *argv[])
{
    char host[80] = "";
    char token[64] = "";
    int nosnow = 0;
    const char *firstMessage = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "-nosnow") || !stricmp(argv[i], "/nosnow")) {
            nosnow = 1;
        } else if ((!stricmp(argv[i], "-m") || !stricmp(argv[i], "/m")) && i + 1 < argc) {
            firstMessage = argv[++i];
        } else if (argv[i][0] == '-' || argv[i][0] == '/') {
            printf("CLAUDE " VERSION " - chat with Claude via an XTBridge\n\n"
                   "Usage: CLAUDE [host[:port]] [-nosnow] [-m message]\n\n"
                   "Without a host, CLAUDE_BRIDGE from the mTCP config file is used.\n"
                   "-nosnow  skip CGA retrace waits (faster on clones and EGA/VGA)\n"
                   "-m       send this message as soon as we're connected\n");
            return 1;
        } else {
            strncpy(host, argv[i], sizeof(host) - 1);
        }
    }

    if (XTBridge::startStack())
        return 1;
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
    con_init();
    titleBar();
    con_attr(A_INFO);
    con_write("Connecting to bridge at ");
    con_write(host);
    con_write(" ...");

    if (bridge.connect(host, XTBRIDGE_PORT)) {
        showError(bridge.errorText());
        waitKeyAndExit(1);
    }
    if (waitLine(10) <= 0 || strcmp(bridge.verb(), "HELLO")) {
        showError("The bridge didn't say hello.");
        waitKeyAndExit(1);
    }
    if (strstr(bridge.payload(), "AUTH")) {
        bridge.send("AUTH", token);
        if (waitLine(10) <= 0 || strcmp(bridge.verb(), "OK")) {
            showError("The bridge rejected our CLAUDE_TOKEN.");
            waitKeyAndExit(1);
        }
    }
    bridge.send("INFO");
    con_write(" connected.");
    if (waitLine(10) > 0 && !strcmp(bridge.verb(), "INFO")) {
        con_newline();
        con_write("Talking to ");
        con_write(bridge.payload());
        con_putc('.');
    }
    con_newline();
    helpLine();
    prompt();
    if (firstMessage) {
        for (i = 0; firstMessage[i] && inputLen < INPUT_MAX; i++) {
            input[inputLen++] = firstMessage[i];
            con_putc(firstMessage[i]);
        }
        submit();
    }

    unsigned long lastSpin = *biosTicks;
    int spin = 0;
    static const char spinner[] = "|/-\\";

    while (!quitting) {
        int rc = bridge.poll();
        if (rc > 0) {
            handleLine(bridge.verb(), bridge.payload());
            continue;
        }
        if (rc < 0) {
            showError(bridge.errorText());
            waitKeyAndExit(1);
        }
        if (XTBridge::breakPressed)
            break;
        if (keyReady())
            handleKey(keyRead());
        if (state == WAITING && *biosTicks != lastSpin) {
            lastSpin = *biosTicks;
            if ((lastSpin & 1) == 0)
                scr_putc(spinRow, spinCol, spinner[spin++ & 3], A_CLAUDE);
        }
    }

    bridge.send("QUIT");
    bridge.close();
    XTBridge::stopStack();
    scr_done();
    printf("Thanks for chatting!\n");
    return 0;
}
