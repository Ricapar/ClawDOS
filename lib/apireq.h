/* Writing a Messages API request: the head, and the body streamed out of the
 * conversation's prefix file and the history file.
 *
 * The prefix ("system" and "tools") is rendered once per conversation into a
 * file beside the chat (CHATS\MMDDnnn.PRE), from PROMPT.TXT with {name},
 * {memories} and {skills} filled in and TOOLS.JSN, and every request of the
 * conversation sends those bytes unchanged: thinking blocks are bound to the
 * exact prefix that produced them, and the prompt cache wants it too. A
 * request is
 *
 *   {<fields>,<prefix>,"messages":[<history>]}
 *
 * whose length is known before a byte is written: the fields are a few short
 * strings, the prefix and the history are files of known size.
 *
 * A conversation also keeps, from its first request to its last, the settings
 * a message thread treats as fixed (ApiFixed): the thinking configuration and
 * the cache markers, and with them the anthropic-beta values they need.
 *
 * With message threads (docs/API.md, "Threads") the first request of a thread
 * is that body plus "thread":{"type":"create"}, and the API keeps the
 * conversation under the response's id. A later one continues it with only
 * the messages added since that response, and no prefix:
 *
 *   {<fields>,"thread":{"type":"continue","previous_message_id":ID},"messages":[<history from an offset>]}
 */
#ifndef CLAWDOS_APIREQ_H
#define CLAWDOS_APIREQ_H

#include "sink.h"
#include "cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The settings a conversation is held to whatever /model or a relaunch does,
   taken from the provider when the conversation's prefix is first rendered:
   thinking blocks and the prompt cache are bound to them, and a message
   thread refuses a request whose fixed fields differ. */
typedef struct ApiFixed {
    int progress;                /* "thinking":{"display":"updates"}: the model's progress notes */
    int dropThinking;            /* "block_binding":{"prefix_mismatch_behavior":"drop_block"} */
    int cache;                   /* the two cache_control markers */
    int ttl1h;                   /* ...with "ttl":"1h"; else five minutes */
    int threads;                 /* message threads (beta); 0 also once the API said it cannot */
} ApiFixed;

void api_fixed_from(ApiFixed *f, const Provider *p);

/* What a conversation's prefix is made from. */
typedef struct ApiPrefix {
    const char *promptPath;      /* PROMPT.TXT: CP437 text with {name}, {memories} and {skills} */
    const char *toolsPath;       /* TOOLS.JSN: the tools array, copied verbatim */
    const char *name;            /* for {name} */
    const char *memories;        /* for {memories}; "" shows as "(none)" */
    const char *skills;          /* for {skills}; "" (or NULL) shows as "(none)" */
    int cache;                   /* the system block's cache_control marker */
    int ttl1h;                   /* its ttl: one hour, else five minutes */
} ApiPrefix;

/* "system":[...],"tools":[...] into the sink. 0 ok, -1 a file could not be
   read, -2 the sink failed. */
int  api_prefix(Sink *s, const ApiPrefix *x);
/* The same into the file at path (written beside it first, then renamed, so
   the file is whole or absent). Its size; -1 PROMPT.TXT or TOOLS.JSN could not
   be read (or no memory to read them), -2 the file could not be written. */
long api_prefix_file(const char *path, const ApiPrefix *x);

/* The cache marker a prefix file's system block was written with (a chat
   reopened later keeps it, so the request's top-level marker agrees): into
   *cache and *ttl1h. 0, or -1 if the file cannot be read or is not a prefix. */
int  api_prefix_cache(const char *path, int *cache, int *ttl1h);

/* /model to another provider section: the conversation's fixed settings are
   chosen again from p (a host without the betas would refuse the old ones on
   every request), and the prefix file's cache marker is rewritten to match
   them; its prompt and tools stay as they were rendered (the API's prompt
   cache and thinking blocks are bound to those, not to the marker). 0 done
   (no prefix yet, or its marker already right), 1 the marker rewritten, -1
   it could not be: f keeps the file's marker, so the two markers agree. */
int  api_fixed_switch(ApiFixed *f, const Provider *p, const char *prefixPath);

enum { API_TH_NONE, API_TH_CREATE, API_TH_CONTINUE };

typedef struct ApiReq {
    /* The provider's settings as api_req_settings took them: every byte of a
       request (its length, body, head and thread fingerprint) comes from this
       copy, never from the provider itself, which F5 changes at any key press,
       including those handled while the body is being written. */
    Provider p;
    const ApiFixed *fixed;       /* the conversation's thinking and cache settings */
    const char *prefixPath;      /* the conversation's prefix file */
    long prefixLen;              /* its size */
    int noTools;                 /* the last round: tool_choice none */
    int thread;                  /* API_TH_NONE, _CREATE (the whole conversation) or _CONTINUE */
    const char *prevId;          /* _CONTINUE: the response it continues from */
    long histFrom;               /* _CONTINUE: where the messages since that response start in the history */
} ApiReq;

/* Take the provider's settings for the requests to come (the harness does so
   when a turn begins, so a change made during the turn applies from the next
   message). */
void api_req_settings(ApiReq *r, const Provider *p);

/* A message thread the next request may continue (docs/API.md, "Threads"):
   the response it continues from, where that response ended in the history
   file, and api_fingerprint() of the request that got it. */
typedef struct ApiThread {
    char id[48];                 /* "" none: the next request creates one */
    long at;                     /* hist_size() once the response was written */
    unsigned long fp;
} ApiThread;

/* The fields a thread holds fixed that can still change within a
   conversation (ApiFixed holds the others): the provider and its betas line,
   the model (/model), the effort (F5) and tool_choice (noTools, the round
   cap), hashed. A collision only costs a refused continue, which replays. */
unsigned long api_fingerprint(const Provider *p, int noTools);
/* What the next request is: API_TH_NONE with threads off; API_TH_CONTINUE
   when t holds a thread made with the same fingerprint, nothing at or before
   its offset has been cut from the history since (hist_low) and something
   follows it; else API_TH_CREATE, and t is forgotten. */
int  api_thread_kind(ApiThread *t, const ApiFixed *f, unsigned long fp);
/* After a reply is in the history: keep it as the thread to continue only if
   the request was in a thread (sent != API_TH_NONE), the id is one to send
   back, it stopped at end_turn or tool_use (the API stores no refusal, no
   reply cut by max_tokens) and its tool input was not cut (inputCut: the
   history holds {} where the API keeps what it sent); otherwise t is forgotten. */
void api_thread_keep(ApiThread *t, int sent, const char *id, const char *stopReason, int inputCut,
                     unsigned long fp);
/* t read back after a relaunch: 0 and kept if it fits the reopened history. */
int  api_thread_set(ApiThread *t, const ApiThread *from);
long api_thread_from(const ApiThread *t);    /* where a continue's messages start in the history */

/* The body's length: what api_body will write, from the sizes alone. */
unsigned long api_length(const ApiReq *r);
/* The JSON body into the sink. 0 ok, -1 the prefix or the history could not
   be read (or the prefix was not the size counted), -2 the sink failed. */
int api_body(Sink *s, const ApiReq *r);
/* "POST path HTTP/1.1\r\n" + headers + "\r\n" for a body of `length` bytes.
   anthropic-beta carries the provider's betas line and the values f needs
   (none repeated; with threads on, every request of the conversation carries
   the threads value); there is none when both are empty. */
int api_head(Sink *s, const Provider *p, const ApiFixed *f, unsigned long length);

#ifdef __cplusplus
}
#endif

#endif
