/* Reading a conversation file (CHATS\MMDDnnn.JSN, see history.h) without
 * holding it in memory: the start of its first message for the chat list,
 * where its last few exchanges begin, and those messages as the transcript
 * draws them, together with what the file ends with (which decides what the
 * next message must do to keep the roles alternating).
 *
 * Messages are separated by ",\r\n" and each begins {"role":"...". Raw CR and
 * LF never occur inside a JSON string, so ",\r\n{\"role\":\"" marks a
 * message's start, and ",\r\n{\"role\":\"user\",\"content\":\"" one the user
 * typed: convo.c writes typed text as a plain string, tool results (and text
 * that joins them) as a list of blocks.
 *
 * The file is read through a ChatRead callback (hist_read for the open
 * conversation), a piece at a time, so a 200 KB chat costs a few KB of heap
 * while it is read and nothing after.
 */
#ifndef CLAWDOS_CHATFILE_H
#define CLAWDOS_CHATFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#define CHATFILE_SHOW      3          /* typed messages (exchanges) the transcript shows */
#define CHATFILE_SHOW_MAX  49152L     /* bytes at most read to show them */
#define CHATFILE_TEXT_MAX  1000       /* characters shown of one text block (about 13 rows) */

/* Up to n bytes of the file from pos into buf: how many (0 at the end), or -1. */
typedef int (*ChatRead)(void *ctx, long pos, char *buf, unsigned n);

typedef struct ChatScan {
    int exchanges;                    /* messages the user typed */
    long last;                        /* where the last message starts; -1 none */
    long from;                        /* where the part to show starts: the CHATFILE_SHOW-th last
                                         typed message, or later to keep within CHATFILE_SHOW_MAX */
} ChatScan;

/* The whole file (size bytes) once through. 0, or -1 (a read failed, no memory). */
int chatfile_scan(ChatRead rd, void *ctx, long size, ChatScan *s);

/* What a shown message holds, in the order it comes. Text is CP437 for the
   screen; cut says it was longer than CHATFILE_TEXT_MAX. A tool call's input
   is its raw JSON object, perhaps cut short (its first members are whole). */
typedef struct ChatShow {
    void *ctx;
    void (*user)(void *ctx, const char *cp437, int cut);     /* text the user typed */
    void (*reply)(void *ctx, const char *cp437, int cut);    /* a text block of a reply */
    void (*tool)(void *ctx, const char *name, const char *input, unsigned len);
} ChatShow;

/* What the last message is, and so what the next one must do. */
enum {
    CHAT_END_NONE,                    /* no message */
    CHAT_END_REPLY,                   /* a reply without a tool call: the next text is a new message */
    CHAT_END_CALL,                    /* a reply whose tool call (toolId) never got its result */
    CHAT_END_RESULTS,                 /* a tool results message (perhaps with text): the next text joins it */
    CHAT_END_USER                     /* a typed message never answered: the next text joins it */
};

typedef struct ChatEnd {
    int kind;
    char toolId[64];
} ChatEnd;

/* The messages from offset `from` (a message's start) to `to` (the end of
   one), through show (NULL: nothing shown) and what the last of them is into
   end (NULL: not wanted). 0, or -1 (a read failed, no memory). */
int chatfile_read(ChatRead rd, void *ctx, long from, long to, const ChatShow *show, ChatEnd *end);

/* The start of the file's first message as one line of CP437 (line breaks
   as spaces) into out (max bytes, NUL included), from its first n bytes in
   raw, which are decoded in place. Its length; 0 when the file does not
   begin with a typed message. */
unsigned chatfile_first(char *raw, unsigned n, char *out, unsigned max);

#ifdef __cplusplus
}
#endif

#endif
