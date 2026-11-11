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
#include "history.h"
#include "apireq.h"
#include "chatfile.h"

struct ChatUi {
    void (*text)(const char *cp437, unsigned n);   /* visible reply text */
    void (*progress)(int phase, const char *cp437, unsigned n);  /* thinking text (apistream.h THINK_*) */
    void (*note)(const char *text, int error);     /* a line in the transcript */
    int  (*tool)(const char *name, const char *input, unsigned len, ToolCtx *t);  /* tools_call */
    void (*idle)(void);                            /* called while waiting: keys, spinner */
    int  (*escPressed)(void);                      /* 1: stop the turn */
    void (*log)(const char *line);                 /* a line for CLAWDOS.LOG */
    void (*usage)(void);                           /* contextTokens()/turns() changed */
};

class Chat {
  public:
    Chat();

    /* Pick the provider (providerId, else cfg->provider), use hostOverride as
       its address if given, allocate buffers, start a conversation (unless
       fresh is 0: openConversation follows) and connect. 0 ok; otherwise
       errorText() says why. */
    int8_t init(Cfg *cfg, const char *providerId, const char *hostOverride, ChatUi *ui, int fresh = 1);
    void   close(void);

    int8_t newConversation(void);          /* a new CHATS file, and a new prefix (memories and skills as they are) */
    /* An earlier CHATS file becomes the conversation (the one we were in is
       left as it is, or deleted if nothing was said in it). scan says where
       its tail to show starts (chatfile.h), endKind what it ended with
       (CHAT_END_*). 0, or -1 with errorText() (the conversation is then the
       one we were in). */
    int8_t openConversation(const char *chatPath, ChatScan *scan, int *endKind);
    long   openedCut(void) { return repairCut; }   /* bytes the reopen cut off a damaged end */
    /* Send the user's text and run the whole turn, tool rounds included.
       0 done, 1 stopped by Esc (answered() says whether anything was shown),
       -1 failed (a note has been shown), 2 a run waits for the launcher
       (relaunch.h): pendingRound()/pendingToolId() and state() are what a
       relaunched CLAWDOS needs to continueTurn(). */
    int8_t turn(const char *cp437Text);
    int    answered(void) { return convo.assistantInTurn > 0; }

    /* Like init, but back into an existing conversation after a relaunch: the
       history file, the convo state, where the turn began in the file (turn,
       NULL if not known) and the provider/model in use. */
    int8_t resume(Cfg *cfg, const char *providerId, const char *model, const char *hostOverride,
                  ChatUi *ui, const char *chatPath, const Convo *state, const HistTurn *turn);
    int    pendingRound(void) { return pendRound; }
    const char *pendingToolId(void) { return pendTool; }
    const Convo *state(void) { return &convo; }
    /* Carry the turn on with the launcher's result for the pending call: the
       tool_result message, then the remaining rounds, with the provider's
       settings as they are now, except the effort the turn began with
       (effort; "" for the provider's: an F5 during the turn applies from
       the next message). Returns as turn(). */
    int8_t continueTurn(int round, const char *toolId, int status, ToolCtx *t, const char *effort = "");
    const char *turnEffort(void) { return req.p.effort; }   /* for the relaunch state */

    /* The conversation's thinking and cache settings (apireq.h): NULL until
       its first request chose them; setFixed gives a resumed conversation
       the ones it started with. */
    const ApiFixed *fixed(void) { return fixedSet ? &fix : NULL; }
    void   setFixed(const ApiFixed *f) { fix = *f; fixedSet = 1; }
    /* The message thread a relaunch carries over (RELAUNCH_STATE); setThread
       ignores one that does not fit the reopened history. */
    const ApiThread *thread(void) { return th.id[0] ? &th : NULL; }
    void   setThread(const ApiThread *t);

    /* /model: within the provider section only the model changes; to another
       section the conversation's fixed settings are chosen again from it,
       its prefix's cache marker follows (api_fixed_switch) and the thread is
       dropped. Only between turns. */
    int8_t switchProvider(const char *id, const char *model);
    const char *describe(void);            /* "anthropic: claude-opus-5-5" */
    const char *address(void);             /* the tunnel address in use */
    int    connected(void) { return http.isOpen(); }
    const char *chatPath(void) { return path; }
    const char *errorText(void) { return err; }
    Cfg *config(void) { return cfg; }
    Provider *provider(void) { return prov; }

    /* The conversation's size after the last round (input, cache and output
       tokens) and the turns that got a reply; 0 in a new conversation. */
    long contextTokens(void) { return ctxTokens; }
    int  turns(void) { return turnsDone; }
    void setUsage(long tokens, int turnCount) { ctxTokens = tokens; turnsDone = turnCount; }

    int connections;                       /* TCP connections made so far */

  private:
    Cfg *cfg;
    Provider *prov;
    ChatUi *ui;
    Http http;
    Sse sse;
    ApiStream as;                          /* its buffers are lib/runmem.h's, lent to run programs */
    void useBuffers(void);
    ApiReq req;                            /* the turn's request: the provider's settings as the turn began */
    char path[40];
    char prePath[40];                      /* the conversation's prefix file (apireq.h) */
    long preLen;                           /* its size; -1 until a request looks */
    ApiFixed fix;                          /* chosen with the prefix, kept to the end */
    int fixedSet;
    ApiThread th;                          /* what the next request may continue */
    unsigned long sentFp;                  /* fingerprint() of the attempt that got the reply */
    int thSent;                            /* its API_TH_* (logRound, api_thread_keep) */
    long repairCut;                        /* openConversation: bytes hist_open cut off the file's end */
    char desc[80];
    char errBuf[160];
    const char *err;
    Convo convo;                           /* the messages and the rules that keep the file valid */
    int reasked;                           /* re-sent after a lost link this turn */
    int streamRc;
    int textShown;                         /* this round's reply has put text on the screen */
    unsigned long reqStart, firstText;
    unsigned long upTicks, reqLen;         /* the request's upload time and body length (logRound) */
    int pendRound;                         /* the call the launcher is running (turn() returned 2) */
    long ctxTokens;
    int turnsDone;
    char pendTool[64];

    int8_t setup(Cfg *cfg, const char *providerId, const char *hostOverride, ChatUi *ui);
    int8_t rounds(int round);              /* the request/tool loop from this round on */
    int8_t finishTool(int round, const char *toolId, int status, ToolCtx *t);   /* -2: next round */
    int8_t toolResult(const char *toolId, const char *text, int isError, const char *note, const char *image = NULL);
    void   dropTurn(void);                 /* the whole turn rolled back, its count with it */
    int8_t connect(void);
    int8_t request(int noTools);           /* 0 ok (as filled), 1 stopped, -1 failed */
    int8_t stream(void);                   /* as request, or 3: an overload before any text, retry it */
    int    waitRead(char *buf, unsigned max);     /* >0, 0 end, -1 lost, -2 timeout, -3 Esc */
    int8_t waitHead(HttpHead *h);                 /* 0, -1, -2, -3 malformed, 1 Esc */
    void   drain(void);
    /* msg: "API error N: error.message"; code: error.details.error_code or "" */
    void   readErrorBody(const HttpHead *h, char *msg, unsigned max, char *code, unsigned codeMax);
    unsigned long fingerprint(int noTools);   /* the fixed fields a request is sent with, hashed */
    void   replayThread(ApiReq *r, const char *code);
    void   logRound(int round);
    void   setPrefixPath(void);
    int8_t preparePrefix(void);            /* 0: prePath holds preLen bytes, 1 Esc, -1 unreadable, -2 unwritable */
    void   pause(int secs);
    void   warn(const char *text);

    static void onText(void *ctx, const char *cp437, unsigned n);
    static void onThinking(void *ctx, int phase, const char *cp437, unsigned n);
    static int  onHist(void *ctx, const char *p, unsigned n);
    static void onEvent(void *ctx, const char *event, const char *data, unsigned n);
    static int onBig(void *ctx, int phase, const char *event, const char *data, unsigned n);
};

#endif
