/* DOSUNIT: the request's settings snapshot (F5 mid-body), /model's re-choice
 * of a conversation's fixed settings, and a reopened chat holding an image
 * result. In a module of its own, for room in dosunit.c's code segment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include "sink.h"
#include "cfg.h"
#include "history.h"
#include "apireq.h"
#include "dosfs.h"
#include "chatfile.h"
#include "apistream.h"

void check(const char *name, int ok, const char *detail);              /* dosunit.c */
void check_str(const char *name, const char *got, const char *want);
#define CHECK(name, cond) check((name), (cond), "")
#define CHECK_STR(name, got, want) check_str((name), (got), (want))

#define U1 "{\"role\":\"user\",\"content\":\"a\"}"

static void writeFile(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

/* The bytes of a small file, NUL-terminated ("" if it can't be read). */
static const char *fileText(const char *path)
{
    static char text[300];
    FILE *f = fopen(path, "rb");
    unsigned n = 0;

    if (f) {
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
    }
    text[n] = 0;
    return text;
}

/* A chat file in memory, read `step` bytes at a time. */
typedef struct MemFile {
    const char *s;
    long len;
    unsigned step;
} MemFile;

static int memRead(void *ctx, long pos, char *buf, unsigned n)
{
    MemFile *m = (MemFile *)ctx;
    if (pos >= m->len)
        return 0;
    if (n > m->step)
        n = m->step;
    if ((long)n > m->len - pos)
        n = (unsigned)(m->len - pos);
    memcpy(buf, m->s + pos, n);
    return (int)n;
}

static char *shownLog;                /* "U:...|T:name:input|" (2000) */

static void logShown(const char *kind, const char *text)
{
    unsigned n = strlen(shownLog);
    if (n + strlen(text) + 8 < 2000)
        sprintf(shownLog + n, "%s:%s|", kind, text);
}

static void shownUser(void *ctx, const char *t, int cut) { (void)ctx; (void)cut; logShown("U", t); }
static void shownReply(void *ctx, const char *t, int cut) { (void)ctx; (void)cut; logShown("R", t); }

static void shownTool(void *ctx, const char *name, const char *input, unsigned len)
{
    unsigned n = strlen(shownLog);
    (void)ctx;
    if (n + len + 40 < 2000)
        sprintf(shownLog + n, "T:%s:%.*s|", name, (int)len, input);
}

static const ChatShow showLog = { NULL, shownUser, shownReply, shownTool };

#define CF_TYPED  "{\"role\":\"user\",\"content\":\"Hi\"}"
#define CF_SEP    ",\r\n"

/* F5 while a request is written: the sink below is the socket, whose flushes
   run the idle callback and so handle keys. The provider it changes is the
   live one; the request was built from api_req_settings' copy, so the body
   still matches the length and the fingerprint counted before it began. */
static Provider *f5Live;
static int f5Put(Sink *s, const char *p, unsigned n)
{
    char *buf = (char *)s->ctx;
    unsigned have = strlen(buf);
    if (have + n < 400) {
        memcpy(buf + have, p, n);
        buf[have + n] = 0;
    }
    if (s->count > 20)                    /* past "model", before "output_config" */
        strcpy(f5Live->effort, "medium");
    return 0;
}

void testApiReqSnapshot(void)
{
    static char body[400];
    Provider *live = (Provider *)malloc(sizeof(Provider));
    ApiReq *rq = (ApiReq *)malloc(sizeof(ApiReq));
    ApiPrefix x;
    ApiFixed fx;
    Sink s;
    unsigned long length, fp;

    writeFile("C:\\PTEST.TXT", "Hi.");
    writeFile("C:\\TTEST.JSN", "[{\"name\":\"t\"}]");
    memset(live, 0, sizeof *live);
    strcpy(live->id, "anthropic"); strcpy(live->host, "h"); strcpy(live->path, "/v1/messages");
    strcpy(live->model, "m"); strcpy(live->effort, "low"); live->maxTokens = 100; live->threads = 1;
    api_fixed_from(&fx, live);
    x.promptPath = "C:\\PTEST.TXT"; x.toolsPath = "C:\\TTEST.JSN"; x.name = "C"; x.memories = ""; x.skills = "";
    x.cache = 0; x.ttl1h = 0;
    api_req_settings(rq, live);
    rq->fixed = &fx; rq->prefixPath = "C:\\PTEST.PRE"; rq->prefixLen = api_prefix_file("C:\\PTEST.PRE", &x);
    rq->noTools = 0; rq->thread = API_TH_CREATE; rq->prevId = ""; rq->histFrom = 0;
    remove("C:\\HISTTEST.JSN");
    hist_open("C:\\HISTTEST.JSN");
    hist_puts(U1);
    hist_message_done();
    length = api_length(rq);
    fp = api_fingerprint(&rq->p, 0);
    f5Live = live;
    body[0] = 0;
    s.put = f5Put; s.ctx = body; s.count = 0;
    CHECK("F5 mid-body: written", api_body(&s, rq) == 0 && !strcmp(live->effort, "medium"));
    CHECK("F5 mid-body: the length sent still holds", s.count == length && strlen(body) == length);
    CHECK("F5 mid-body: the effort counted is the one sent", strstr(body, "\"effort\":\"low\"") != NULL
          && strstr(body, "medium") == NULL);
    CHECK("F5 mid-body: the fingerprint is the request's", api_fingerprint(&rq->p, 0) == fp
          && api_fingerprint(live, 0) != fp);
    api_req_settings(rq, live);
    CHECK("F5: the next message takes it", api_length(rq) == length + 3 && api_fingerprint(&rq->p, 0) != fp);
    hist_close();
    remove("C:\\HISTTEST.JSN"); remove("C:\\PTEST.TXT"); remove("C:\\TTEST.JSN"); remove("C:\\PTEST.PRE");
    free(rq);
    free(live);
}

/* /model to another provider section: the fixed settings follow it, and the
   prefix file's marker is rewritten to the new cache setting with the prompt
   and tools left byte for byte as rendered. */
void testFixedSwitch(void)
{
    static char want[300];
    Provider *p = (Provider *)malloc(sizeof(Provider));
    ApiPrefix x;
    ApiFixed fx;
    Sink buf;
    int rc;

    writeFile("C:\\PTEST.TXT", "Say \"hi\" \\ }],\"tools\": {name}.");
    writeFile("C:\\TTEST.JSN", "[{\"name\":\"t\",\"cache_control\":{\"ttl\":\"1h\"}}]");
    x.promptPath = "C:\\PTEST.TXT"; x.toolsPath = "C:\\TTEST.JSN"; x.name = "Q"; x.memories = "";
    x.skills = ""; x.cache = 1; x.ttl1h = 1;
    api_prefix_file("C:\\PTEST.PRE", &x);
    memset(p, 0, sizeof *p);              /* a host with none of the betas and no cache */
    rc = api_fixed_switch(&fx, p, "C:\\PTEST.PRE");
    x.cache = 0;
    sink_buffer(&buf, want, sizeof want);
    api_prefix(&buf, &x);
    CHECK("switch: settings from the new provider", !fx.progress && !fx.dropThinking && !fx.threads && !fx.cache);
    CHECK("switch: the marker dropped, the rest as rendered", rc == 1 && !strcmp(fileText("C:\\PTEST.PRE"), want)
          && fs_size("C:\\PTEST.PR$") == -1);
    p->cache = 1;
    rc = api_fixed_switch(&fx, p, "C:\\PTEST.PRE");
    x.cache = 1; x.ttl1h = 0;
    sink_buffer(&buf, want, sizeof want);
    api_prefix(&buf, &x);
    CHECK("switch: a five-minute marker put in", rc == 1 && fx.cache && !fx.ttl1h
          && !strcmp(fileText("C:\\PTEST.PRE"), want));
    p->progress = p->dropThinking = p->threads = 1;
    rc = api_fixed_switch(&fx, p, "C:\\PTEST.PRE");
    CHECK("switch: the same marker leaves the file", rc == 0 && fx.progress && fx.dropThinking && fx.threads
          && !strcmp(fileText("C:\\PTEST.PRE"), want));
    p->cacheTtl1h = 1;
    mkdir("C:\\PTEST.PR$");               /* the rewrite's temporary file cannot be made */
    rc = api_fixed_switch(&fx, p, "C:\\PTEST.PRE");
    CHECK("switch: an unwritable marker keeps the file's", rc == -1 && fx.cache && !fx.ttl1h && fx.threads
          && !strcmp(fileText("C:\\PTEST.PRE"), want));
    rmdir("C:\\PTEST.PR$");
    remove("C:\\PTEST.PRE");
    rc = api_fixed_switch(&fx, p, "C:\\PTEST.PRE");
    CHECK("switch: no prefix yet", rc == 0 && fx.cache && fx.ttl1h && fs_size("C:\\PTEST.PRE") == -1);
    remove("C:\\PTEST.TXT");
    remove("C:\\TTEST.JSN");
    free(p);
}

/* A reopened chat with what Tasks 10 and 11 put in a history: a thinking
   signature of several kilobytes, and a tool result whose content is the
   text-and-image list with 22 KB of base64 (nested at depths where the
   reader keeps no strings), the round cap's note and typed text joined to it.
   Shown as typed is only the typed text. */
void testChatFileImage(void)
{
    char *big = (char *)malloc(30000);
    unsigned n;
    MemFile m;
    ChatScan sc;
    ChatEnd end;
    int ok;

    shownLog = (char *)malloc(2000);
    if (!big || !shownLog) {
        CHECK("chatfile image: memory", 0);
        return;
    }
    strcpy(big, CF_TYPED CF_SEP "{\"role\":\"assistant\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"t\",\"signature\":\"");
    n = strlen(big);
    memset(big + n, 'S', 6000);
    strcpy(big + n + 6000, "\"},{\"type\":\"tool_use\",\"id\":\"toolu_9\",\"name\":\"run\",\"input\":{\"command\":\"Q\"}}]}" CF_SEP
           "{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"toolu_9\",\"content\":["
           "{\"type\":\"text\",\"text\":\"exit code 0\"},{\"type\":\"image\",\"source\":{\"type\":\"base64\","
           "\"media_type\":\"image/png\",\"data\":\"");
    n = strlen(big);
    memset(big + n, 'A', 22000);
    strcpy(big + n + 22000, "\"}}]},{\"type\":\"text\",\"text\":\"" CHAT_ROUND_CAP_NOTE "\"},"
           "{\"type\":\"text\",\"text\":\"next\"}]}");
    m.s = big;
    m.len = strlen(big);
    m.step = 2048;
    shownLog[0] = 0;
    ok = chatfile_scan(memRead, &m, m.len, &sc) == 0 && sc.exchanges == 1 && sc.from == 0
         && chatfile_read(memRead, &m, sc.from, m.len, &showLog, &end) == 0 && end.kind == CHAT_END_RESULTS;
    CHECK("chatfile image: scanned and read to a results end", ok);
    CHECK_STR("chatfile image: shown", shownLog, "U:Hi|T:run:{\"command\":\"Q\"}|U:next|");
    free(big);
    free(shownLog);
}

/* A tool call whose input max_tokens cuts mid-string (fine-grained input
   streaming sends it in pieces): the message is stored with "input":{} so it
   stays valid JSON, and the call is marked cut. An input that closes, braces
   inside its strings included, is kept as it came. */
static char cutHist[400];
static int cutPut(void *c, const char *p, unsigned n)
{
    unsigned have = strlen(cutHist);
    (void)c;
    if (have + n >= sizeof cutHist)
        return -1;
    memcpy(cutHist + have, p, n);
    cutHist[have + n] = 0;
    return 0;
}
static long cutPos(void *c) { (void)c; return (long)strlen(cutHist); }
static int cutCut(void *c, long pos) { (void)c; cutHist[pos] = 0; return 0; }

static int cutRun(ApiStream *a, char *in, const char *const *pieces, const char *stop)
{
    static char ev[200];
    int i, rc = 0;

    cutHist[0] = 0;
    apistream_init(a, in, 200);
    a->hist = cutPut; a->histPos = cutPos; a->histCut = cutCut;
    strcpy(ev, "{\"message\":{\"id\":\"msg_1\",\"model\":\"m\"}}");
    rc |= apistream_event(a, "message_start", ev, strlen(ev));
    sprintf(ev, "{\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"write\",\"input\":{}}}");
    rc |= apistream_event(a, "content_block_start", ev, strlen(ev));
    for (i = 0; pieces[i]; i++) {
        sprintf(ev, "{\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"%s\"}}", pieces[i]);
        rc |= apistream_event(a, "content_block_delta", ev, strlen(ev));
    }
    rc |= apistream_event(a, "content_block_stop", "{\"index\":0}", 11);
    sprintf(ev, "{\"delta\":{\"stop_reason\":\"%s\"},\"usage\":{\"output_tokens\":5}}", stop);
    rc |= apistream_event(a, "message_delta", ev, strlen(ev));
    return rc == 0 && apistream_event(a, "message_stop", "{}", 2) == 1;
}

void testToolInputCut(void)
{
    static const char *const cut[] = { "{\\\"path\\\":\\\"C:\\\\\\\\A.TXT\\\",", "\\\"text\\\":\\\"half a li", NULL };
    static const char *const whole[] = { "{\\\"text\\\":\\\"}{[\\\\\\\"\\\"", ",\\\"n\\\":[1,{\\\"a\\\":2}]}", NULL };
    static const char *const nested[] = { "{\\\"a\\\":{\\\"b\\\":1}", NULL };
    ApiStream *a = (ApiStream *)malloc(sizeof(ApiStream));
    char *in = (char *)malloc(200);

    if (!a || !in) {
        CHECK("tool input cut: memory", 0);
        return;
    }
    CHECK("tool input cut: the stream ends", cutRun(a, in, cut, "max_tokens"));
    CHECK_STR("tool input cut: stored with no input", cutHist,
              "{\"role\":\"assistant\",\"content\":[{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"write\",\"input\":{}}]}");
    CHECK("tool input cut: marked, no input kept", a->toolInputCut && a->hasTool && a->toolInputLen == 0
          && !strcmp(a->stopReason, "max_tokens"));
    CHECK("tool input whole: the stream ends", cutRun(a, in, whole, "tool_use"));
    CHECK_STR("tool input whole: kept as it came", cutHist,
              "{\"role\":\"assistant\",\"content\":[{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"write\","
              "\"input\":{\"text\":\"}{[\\\"\",\"n\":[1,{\"a\":2}]}}]}");
    CHECK("tool input whole: not marked", !a->toolInputCut && a->toolInputLen > 0);
    CHECK("tool input cut after an inner object", cutRun(a, in, nested, "max_tokens") && a->toolInputCut
          && strstr(cutHist, "\"input\":{}}]}") != NULL);
    free(in);
    free(a);
}

/* hist_cut_back: a cut inside the message being written, in the write-behind
   buffer and on disk, never before the message's start. */
void testHistCutBack(void)
{
    char *big = (char *)malloc(3001);
    long at;

    if (!big) {
        CHECK("hist cut back: memory", 0);
        return;
    }
    remove("C:\\HISTTEST.JSN");
    hist_open("C:\\HISTTEST.JSN");
    hist_puts(U1);
    hist_message_done();
    hist_puts("{\"role\":\"assistant\",\"content\":[{\"input\":");
    at = hist_size();
    hist_puts("{\"a\":\"hal");
    CHECK("hist cut back: in the buffer", hist_cut_back(at) == 0 && hist_size() == at);
    memset(big, 'x', 3000); big[3000] = 0;
    hist_puts(big);                       /* past the 2 KB buffer: part of it on disk */
    CHECK("hist cut back: on disk", hist_cut_back(at) == 0 && hist_size() == at);
    CHECK("hist cut back: not before the message", hist_cut_back(10) == -1);
    hist_puts("{}}]}");
    CHECK("hist cut back: the message ends whole", hist_message_done() == 0
          && hist_size() == (long)strlen(U1) + 3 + 45);
    hist_close();
    /* the file itself, read back whole after the close (the directory entry's
       size changes only then): without the truncate, the 3,000 x's the cut
       reached on disk would follow the message */
    CHECK("hist cut back: the file's length", fs_size("C:\\HISTTEST.JSN") == (long)strlen(U1) + 3 + 45);
    CHECK_STR("hist cut back: the file as cut", fileText("C:\\HISTTEST.JSN"),
              U1 ",\r\n{\"role\":\"assistant\",\"content\":[{\"input\":{}}]}");
    remove("C:\\HISTTEST.JSN");
    free(big);
}
