/* The agent loop, see chat.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <direct.h>
#include <io.h>

#include "chat.h"
#include "apireq.h"
#include "json.h"
#include "history.h"
#include "httpp.h"
#include "ticks.h"
#include "memory.h"
#include "tools.h"
#include "utf8.h"
#include "convo.h"
#include "dosfs.h"

#define SSE_MAX        16384u
#define TOOL_INPUT_MAX 16384u
#define RX_MAX         2048u
#define READ_TIMEOUT   300          /* seconds without a byte: the link is dead */
#define MAX_ROUNDS     25           /* tool rounds per user message */
#define PROMPT_PATH    "C:\\CLAWDOS\\PROMPT.TXT"
#define TOOLS_PATH     "C:\\CLAWDOS\\TOOLS.JSN"
#define CHATS_DIR      "C:\\CLAWDOS\\CHATS"
#define CFG_PATH       "C:\\CLAWDOS\\CLAWDOS.CFG"
#define ROUND_CAP_NOTE "Tool limit reached; answer now without tools."

Chat::Chat() : connections(0), cfg(NULL), prov(NULL), ui(NULL), sseBuf(NULL), toolInput(NULL),
               rxBuf(NULL), err(""), reasked(0), streamRc(0), textShown(0), reqStart(0), firstText(0),
               upTicks(0), reqLen(0)
{
    addr[0] = path[0] = prePath[0] = desc[0] = 0;
    preLen = -1;
    ctxTokens = 0;
    turnsDone = 0;
    convo_init(&convo);
}

/* What init and resume share: the provider, the buffers. */
int8_t Chat::setup(Cfg *c, const char *providerId, const char *hostOverride, ChatUi *u)
{
    const char *missing;

    cfg = c;
    ui = u;
    prov = cfg_provider(cfg, providerId && providerId[0] ? providerId : cfg->provider);
    if (prov == NULL) {
        sprintf(errBuf, "No provider [%.20s] in CLAWDOS.CFG.", providerId && providerId[0] ? providerId : cfg->provider);
        err = errBuf;
        return -1;
    }
    if (hostOverride && hostOverride[0])
        strncpy(prov->tunnel, hostOverride, sizeof prov->tunnel - 1);
    if ((missing = cfg_missing(prov)) != NULL && strcmp(missing, "key")) {
        sprintf(errBuf, "Provider [%s] has no %s line in CLAWDOS.CFG.", prov->id, missing);
        err = errBuf;
        return -1;
    }
    if (!sseBuf)
        sseBuf = (char *)malloc(SSE_MAX);
    if (!toolInput)
        toolInput = (char *)malloc(TOOL_INPUT_MAX);
    if (!rxBuf)
        rxBuf = (char *)malloc(RX_MAX);
    if (!sseBuf || !toolInput || !rxBuf) {
        err = "Out of memory for the API buffers.";
        return -1;
    }
    http.idle = ui->idle;
    return 0;
}

int8_t Chat::init(Cfg *c, const char *providerId, const char *hostOverride, ChatUi *u)
{
    if (setup(c, providerId, hostOverride, u))
        return -1;
    if (newConversation())
        return -1;
    if (connect())
        return -1;
    if (!prov->key[0]) {
        sprintf(errBuf, "No API key for [%s]: add a key line to " CFG_PATH ".", prov->id);
        warn(errBuf);
    }
    return 0;
}

int8_t Chat::resume(Cfg *c, const char *providerId, const char *model, const char *hostOverride,
                    ChatUi *u, const char *chatPath, const Convo *state)
{
    if (setup(c, providerId, hostOverride, u))
        return -1;
    if (model && model[0]) {
        strncpy(prov->model, model, sizeof prov->model - 1);
        prov->model[sizeof prov->model - 1] = 0;
    }
    strncpy(path, chatPath, sizeof path - 1);
    path[sizeof path - 1] = 0;
    if (hist_open(path)) {            /* appends: the file is as the run left it */
        sprintf(errBuf, "Cannot reopen %s", path);
        err = errBuf;
        return -1;
    }
    convo = *state;
    setPrefixPath();                  /* the prefix the conversation started with */
    return connect();
}

void Chat::close(void)
{
    http.close();
    hist_close();
}

const char *Chat::address(void)
{
    return prov ? prov->tunnel : "";
}

const char *Chat::describe(void)
{
    sprintf(desc, "%.16s: %.48s", prov->id, prov->model);
    return desc;
}

int8_t Chat::connect(void)
{
    char line[40];
    if (http.open(prov->tunnel)) {
        sprintf(errBuf, "%.60s (%.48s)", http.errorText(), prov->tunnel);
        err = errBuf;
        sprintf(line, "connect failed, local port %u", http.localPort());
        ui->log(line);
        return -1;
    }
    connections++;
    sprintf(line, "connection #%d, local port %u", connections, http.localPort());
    ui->log(line);
    return 0;
}

int8_t Chat::switchProvider(const char *id, const char *model)
{
    Provider *p = cfg_provider(cfg, id);
    const char *missing;
    if (p == NULL) {
        sprintf(errBuf, "No provider called %.20s; /model lists them.", id);
        err = errBuf;
        return -1;
    }
    if ((missing = cfg_missing(p)) != NULL && strcmp(missing, "key")) {
        sprintf(errBuf, "Provider %s has no %s line in CLAWDOS.CFG.", p->id, missing);
        err = errBuf;
        return -1;
    }
    if (model && model[0]) {
        strncpy(p->model, model, sizeof p->model - 1);
        p->model[sizeof p->model - 1] = 0;
    }
    if (p != prov)
        http.close();
    prov = p;
    return 0;
}

/* --- the conversation file ----------------------------------------------------- */

int8_t Chat::newConversation(void)
{
    struct dosdate_t d;
    int n;

    hist_close();
    ctxTokens = 0;
    turnsDone = 0;
    mkdir(CHATS_DIR);
    _dos_getdate(&d);
    for (n = 1; n < 1000; n++) {
        sprintf(path, "%s\\%02u%02u%03d.JSN", CHATS_DIR, d.month, d.day, n);
        if (access(path, 0) != 0)
            break;
    }
    if (hist_open(path)) {
        sprintf(errBuf, "Cannot create %s", path);
        err = errBuf;
        return -1;
    }
    convo_init(&convo);
    setPrefixPath();
    remove(prePath);                  /* one left by a chat file deleted since */
    return 0;
}

/* CHATS\MMDDnnn.PRE beside CHATS\MMDDnnn.JSN. */
void Chat::setPrefixPath(void)
{
    char *dot;
    strcpy(prePath, path);
    if ((dot = strrchr(prePath, '.')) != NULL)
        strcpy(dot, ".PRE");
    preLen = -1;
}

/* The conversation's prefix file: rendered the first time a request needs it
   (or when it has gone missing) from PROMPT.TXT, TOOLS.JSN and the memories
   as they are then, and the same bytes on every request after that, through
   /model, F5 and a relaunch. Edits to those apply to the next conversation. */
int8_t Chat::preparePrefix(void)
{
    ApiPrefix x;
    ToolCtx t;
    char line[60];

    if ((preLen = fs_size(prePath)) > 0)
        return 0;
    tout_reset(&t);
    x.promptPath = PROMPT_PATH;
    x.toolsPath = TOOLS_PATH;
    x.name = prov->name[0] ? prov->name : cfg->name;
    x.memories = tool_memory_list(&t) == TS_OK ? t.out : "";
    x.cache = prov->cache;
    if ((preLen = api_prefix_file(prePath, &x)) < 0)
        return -1;
    sprintf(line, "prefix %.30s: %ld bytes", prePath, preLen);
    ui->log(line);
    return 0;
}

/* An error note: on the screen and in CLAWDOS.LOG (the boot tests read the log). */
void Chat::warn(const char *text)
{
    ui->note(text, 1);
    ui->log(text);
}

/* --- streaming callbacks ------------------------------------------------------- */

void Chat::onText(void *ctx, const char *cp437, unsigned n)
{
    Chat *c = (Chat *)ctx;
    if (!c->textShown) {
        c->textShown = 1;
        c->firstText = TICKS_NOW;
    }
    c->ui->text(cp437, n);
}

void Chat::onThinking(void *ctx)
{
    Chat *c = (Chat *)ctx;
    c->ui->thinking();
}

int Chat::onHist(void *ctx, const char *p, unsigned n)
{
    (void)ctx;
    return hist_write(p, n);
}

void Chat::onEvent(void *ctx, const char *event, const char *data, unsigned n)
{
    Chat *c = (Chat *)ctx;
    if (c->streamRc == 0)
        c->streamRc = apistream_event(&c->as, event, data, n);
}

/* An event too long for the SSE buffer: a thinking signature (they grow with
   how long the model thought) is streamed into the history; anything else is
   refused and reported as "event too long". */
int Chat::onBig(void *ctx, int phase, const char *event, const char *data, unsigned n)
{
    Chat *c = (Chat *)ctx;
    int rc;
    if (c->streamRc != 0)
        return phase == SSE_BIG_BEGIN ? 1 : 0;
    rc = apistream_big(&c->as, phase, event, data, n);
    if (rc < 0)
        c->streamRc = -1;
    return rc > 0 ? 1 : 0;
}

/* --- waiting on the network ---------------------------------------------------- */

void Chat::pause(int secs)
{
    unsigned long start = TICKS_NOW;
    while (ticks_elapsed(TICKS_NOW, start) < SECS_TO_TICKS(secs)) {
        http.service();
        ui->idle();
        if (ui->escPressed())
            return;
    }
}

int Chat::waitRead(char *buf, unsigned max)
{
    unsigned long start = TICKS_NOW;
    for (;;) {
        int n = http.read(buf, max, 1);
        ui->idle();                       /* keys and the spinner, whether or not bytes came */
        if (ui->escPressed())
            return -3;
        if (n != -2)
            return n;
        if (ticks_elapsed(TICKS_NOW, start) >= SECS_TO_TICKS(READ_TIMEOUT))
            return -2;
    }
}

int8_t Chat::waitHead(HttpHead *h)
{
    unsigned long start = TICKS_NOW;
    for (;;) {
        int8_t rc = http.readHead(h, 1);
        ui->idle();
        if (ui->escPressed())
            return 1;
        if (rc != -2)
            return rc;
        if (ticks_elapsed(TICKS_NOW, start) >= SECS_TO_TICKS(READ_TIMEOUT))
            return -2;
    }
}

/* Finish reading a body we no longer need, so the connection can be reused. */
void Chat::drain(void)
{
    int n, guard = 0;
    while (!http.bodyDone() && guard++ < 64) {
        n = http.read(rxBuf, RX_MAX, 2);
        if (n <= 0) {
            if (n != 0)
                http.close();
            break;
        }
    }
}

/* The error message of a non-200 response (error.message), or the status. */
void Chat::readErrorBody(const HttpHead *h, char *msg, unsigned max)
{
    static char *body;
    unsigned len = 0;
    const char *v;
    unsigned vlen;
    int n;

    if (!body)
        body = (char *)malloc(1600);
    sprintf(msg, "API error %d", h->status);
    if (!body)
        return;
    while (len < 1500 && (n = waitRead(body + len, 1500 - len)) > 0)
        len += n;
    drain();
    if (len && json_find(body, len, "error.message", &v, &vlen) == JSON_STRING) {
        unsigned k = json_unescape(v, vlen, body, 1500);
        char *cp = (char *)malloc(max);
        if (cp) {
            unsigned c = utf8_to_cp437(body, k, cp, max - 1);
            cp[c] = 0;
            sprintf(msg, "API error %d: %.*s", h->status, (int)(max - 20), cp);
            free(cp);
        }
    }
}

/* up= and len= are the upload of the attempt that got this reply: the 8088's
   share of ttft, which also holds the API's wait (scripts/bench.py). */
void Chat::logRound(int round)
{
    char line[256];
    unsigned long now = TICKS_NOW;
    sprintf(line, "api %s %s round=%d ttft=%lu total=%lu up=%lu len=%lu stop=%s in=%ld out=%ld cr=%ld cw=%ld ev=%u",
            prov->id, as.model[0] ? as.model : prov->model, round,
            textShown ? ticks_elapsed(firstText, reqStart) * 55 : 0UL, ticks_elapsed(now, reqStart) * 55,
            upTicks * 55, reqLen, as.stopReason[0] ? as.stopReason : "?",
            as.inTok, as.outTok, as.cacheRead, as.cacheWrite, sse.maxLen);
    ui->log(line);
}

/* --- one API round ---------------------------------------------------------------- */

int8_t Chat::stream(void)
{
    int n;

    sse_init(&sse, sseBuf, SSE_MAX);
    sse.big = onBig;
    apistream_init(&as, toolInput, TOOL_INPUT_MAX);
    as.text = onText;
    as.thinking = onThinking;
    as.hist = onHist;
    as.ctx = this;
    streamRc = 0;
    textShown = 0;
    firstText = 0;
    for (;;) {
        n = waitRead(rxBuf, RX_MAX);
        if (n > 0) {
            if (sse_feed(&sse, rxBuf, (unsigned)n, onEvent, this) < 0 && streamRc == 0) {
                streamRc = -1;
                strcpy(as.error, "event too long");
            }
            if (streamRc == 1) {
                drain();
                return 0;
            }
            if (streamRc < 0) {
                hist_rollback_message();
                drain();
                /* An overload reported inside the stream, as it is once
                   streaming has begun: with nothing shown yet it is retried
                   like an HTTP 529. */
                if (!textShown && (!strcmp(as.errorType, "overloaded_error") || !strcmp(as.errorType, "api_error")))
                    return 3;
                warn(as.error);
                return -1;
            }
            continue;
        }
        hist_rollback_message();
        http.close();
        if (n == -3)
            return 1;
        if (!reasked) {                  /* lost mid-reply: ask once more */
            reasked = 1;
            warn("[link lost, asking again]");
            return 2;
        }
        ui->note(n == -2 ? "The API stopped answering." : "The link was lost again; giving up on this reply.", 1);
        return -1;
    }
}

int8_t Chat::request(int noTools)
{
    ApiReq r;
    HttpHead h;
    char msg[160];
    int attempt = 0;
    int fresh, wait;

    if (preparePrefix()) {
        warn("Cannot read " PROMPT_PATH " or " TOOLS_PATH ".");
        return -1;
    }
    r.p = prov;
    r.prefixPath = prePath;
    r.prefixLen = preLen;
    r.noTools = noTools;

    for (;;) {
        unsigned long length = api_length(&r);
        int8_t rc;
        int body = 0;

        fresh = 0;
        if (http.isOpen() && !http.alive())
            http.close();                 /* an idle keep-alive the far end gave up on */
        if (!http.isOpen()) {
            if (connect()) {
                warn(err);
                return -1;
            }
            fresh = 1;
        }
        reqStart = TICKS_NOW;
        rc = http.sendHead(prov, length);
        if (rc == 0 && (body = api_body(http.sink(), &r)) != 0)
            rc = -1;
        if (body == -1) {                 /* a file short of its size: no use retrying */
            http.close();
            warn("Cannot read the conversation's files in " CHATS_DIR ".");
            return -1;
        }
        if (rc == 0 && http.flush())
            rc = -1;
        if (rc == 0) {                    /* the last body byte is with the socket */
            upTicks = ticks_elapsed(TICKS_NOW, reqStart);
            reqLen = length;
            rc = waitHead(&h);
        }
        if (rc == 1) {
            http.close();
            return 1;
        }
        if (rc != 0) {
            int stale = !fresh && !http.headStarted();
            http.close();
            if (stale)                    /* an idle keep-alive connection the far end had dropped */
                continue;
            if (++attempt > 2) {
                warn(rc == -2 ? "No answer from the API." : rc == -3 ? "The API sent a bad response."
                                                                       : "The connection failed.");
                return -1;
            }
            ui->note(attempt == 1 ? "No reply; trying again in 2 s" : "Still nothing; trying again in 8 s", 1);
            pause(attempt == 1 ? 2 : 8);
            if (ui->escPressed())
                return 1;
            continue;
        }
        if (h.status == 200) {
            rc = stream();
            if (rc == 2)
                continue;                 /* re-ask after a lost link */
            if (rc != 3)
                return rc;
            sprintf(msg, "%.120s", as.error);
            wait = 0;
        } else {
            readErrorBody(&h, msg, sizeof msg);
            if (h.close)
                http.close();
            if (h.status != 429 && h.status < 500) {
                warn(msg);
                return -1;
            }
            wait = h.retryAfter;
        }
        if (!wait)                        /* 429, 5xx or an overload in the stream */
            wait = attempt == 0 ? 2 : 8;
        if (++attempt > 2) {
            warn(msg);
            return -1;
        }
        msg[sizeof msg - 28] = 0;         /* room for the rest, any int included */
        sprintf(msg + strlen(msg), "; trying again in %d s", wait);
        warn(msg);
        pause(wait);
        if (ui->escPressed())
            return 1;
    }
}

/* --- a turn ---------------------------------------------------------------------- */

int8_t Chat::turn(const char *text)
{
    convo_turn_begin(&convo);
    reasked = 0;
    if (convo_user_text(&convo, text)) {
        warn("Cannot write the conversation file.");
        convo_rollback_turn(&convo);
        return -1;
    }
    return rounds(0);
}

/* The tool_result for a finished call: 1 stopped, 0 the turn is over, -2 next round. */
int8_t Chat::finishTool(int round, const char *toolId, int status, ToolCtx *t)
{
    convo_tool_result(&convo, toolId, tools_result_text(status, t), status != TS_OK,
                      round + 1 == MAX_ROUNDS ? ROUND_CAP_NOTE : NULL);
    if (status == TS_STOPPED)
        return 1;
    if (round >= MAX_ROUNDS)          /* the model kept calling tools after the cap */
        return 0;
    return -2;
}

int8_t Chat::continueTurn(int round, const char *toolId, int status, ToolCtx *t)
{
    int8_t rc;

    reasked = 0;
    rc = finishTool(round, toolId, status, t);
    return rc == -2 ? rounds(round + 1) : rc;
}

int8_t Chat::rounds(int round)
{
    for (; ; round++) {
        ToolCtx t;
        int status;
        int8_t rc = request(round >= MAX_ROUNDS);
        if (rc == 0 && convo_assistant_done(&convo)) {
            warn("Cannot write the conversation file.");
            hist_rollback_message();
            rc = -1;
        }
        if (rc != 0) {
            convo_settle(&convo);
            return rc;
        }
        if (convo.assistantInTurn == 1)
            turnsDone++;              /* the turn's first reply */
        if (as.inTok || as.cacheRead)
            ctxTokens = as.inTok + as.cacheRead + as.cacheWrite + as.outTok;
        logRound(round);
        if (ui->usage)
            ui->usage();
        if (!strcmp(as.stopReason, "refusal")) {
            warn("The model declined to answer that.");
            if (convo.assistantInTurn == 1)
                turnsDone--;          /* the whole turn goes */
            convo_rollback_turn(&convo);
            return -1;
        }
        if (!strcmp(as.stopReason, "max_tokens"))
            warn("[reply cut off: too long]");
        if (strcmp(as.stopReason, "tool_use")) {
            if (as.hasTool)               /* a call cut off before it was complete */
                convo_tool_result(&convo, as.toolId, "Error: reply cut off before the call was complete", 1, NULL);
            return 0;
        }
        if (!as.hasTool)
            return 0;
        tout_reset(&t);
        if (as.toolInputOverflow) {
            warn("Tool call arguments too long; not run.");
            tout_puts(&t, "arguments too long");
            status = TS_ERROR;
        } else {
            status = ui->tool(as.toolName, as.toolInput, as.toolInputLen, &t);
        }
        if (status == TS_RELAUNCH) {      /* the launcher runs it; a new CLAWDOS continues */
            pendRound = round;
            strcpy(pendTool, as.toolId);
            return 2;
        }
        rc = finishTool(round, as.toolId, status, &t);
        if (rc != -2)
            return rc;
    }
}
