/* The agent loop: one conversation with the Messages API over the TLS tunnel.
 * A turn sends the user's text, streams the reply onto the screen through the
 * ChatUi callbacks, runs the tool calls the model makes (one per round) and
 * keeps the history file valid whatever happens: Esc, a lost link, an API error.
 */
#ifndef CLAWDOS_CHAT_H
#define CLAWDOS_CHAT_H

#include "cfg.h"
#include "toolapi.h"
#include "http.h"
#include "apistream.h"
#include "sse.h"
#include "convo.h"

struct ChatUi {
    void (*text)(const char *cp437, unsigned n);   /* visible reply text */
    void (*thinking)(void);                        /* the model is thinking (a delta arrived) */
    void (*note)(const char *text, int error);     /* a line in the transcript */
    int  (*tool)(const char *name, const char *input, unsigned len, ToolCtx *t);  /* tools_call */
    void (*idle)(void);                            /* called while waiting: keys, spinner */
    int  (*escPressed)(void);                      /* 1: stop the turn */
    void (*log)(const char *line);                 /* a line for CLAWDOS.LOG */
};

class Chat {
  public:
    Chat();

    /* Pick the provider (providerId, else cfg->provider), use hostOverride as
       its address if given, allocate buffers, start a conversation and
       connect. 0 ok; otherwise errorText() says why. */
    int8_t init(Cfg *cfg, const char *providerId, const char *hostOverride, ChatUi *ui);
    void   close(void);

    int8_t newConversation(void);          /* a new CHATS file; memories re-read */
    /* Send the user's text and run the whole turn, tool rounds included.
       0 done, 1 stopped by Esc (answered() says whether anything was shown),
       -1 failed (a note has been shown). */
    int8_t turn(const char *cp437Text);
    int    answered(void) { return convo.assistantInTurn > 0; }

    int8_t switchProvider(const char *id, const char *model);   /* /model */
    const char *describe(void);            /* "anthropic: claude-opus-5-5" */
    const char *address(void);             /* the tunnel address in use */
    int    connected(void) { return http.isOpen(); }
    const char *chatPath(void) { return path; }
    const char *errorText(void) { return err; }
    Cfg *config(void) { return cfg; }
    Provider *provider(void) { return prov; }

    int connections;                       /* TCP connections made so far */

  private:
    Cfg *cfg;
    Provider *prov;
    ChatUi *ui;
    Http http;
    Sse sse;
    ApiStream as;
    char *sseBuf;
    char *toolInput;
    char *memories;
    char *rxBuf;
    char addr[48];
    char path[40];
    char desc[80];
    char errBuf[160];
    const char *err;
    Convo convo;                           /* the messages and the rules that keep the file valid */
    int reasked;                           /* re-sent after a lost link this turn */
    int clockSet;
    int streamRc;
    unsigned long reqStart, firstText;

    int8_t connect(void);
    int8_t request(int noTools);           /* 0 ok (as filled), 1 stopped, 2 ask again, -1 failed */
    int8_t stream(void);
    int    waitRead(char *buf, unsigned max);     /* >0, 0 end, -1 lost, -2 timeout, -3 Esc */
    int8_t waitHead(HttpHead *h);                 /* 0, -1, -2, -3 malformed, 1 Esc */
    void   drain(void);
    void   readErrorBody(const HttpHead *h, char *msg, unsigned max);
    void   setClock(const char *date);
    void   logRound(int round);
    void   loadMemories(void);
    void   pause(int secs);
    void   warn(const char *text);

    static void onText(void *ctx, const char *cp437, unsigned n);
    static void onThinking(void *ctx);
    static int  onHist(void *ctx, const char *p, unsigned n);
    static void onEvent(void *ctx, const char *event, const char *data, unsigned n);
};

#endif
