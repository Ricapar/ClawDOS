/* Reading a conversation file, see chatfile.h. */
#include <stdlib.h>
#include <string.h>
#include "chatfile.h"
#include "json.h"
#include "utf8.h"

#define SCAN_BUF   2048               /* the scan's reads */
#define READ_BUF   1024               /* chatfile_read's reads */
#define INPUT_RAW  512                /* of a tool call's input: enough for the header's argument */
#define DEPTH_MAX  15                 /* nesting tracked; tool inputs (the deep part) are copied raw */

#define MSG_MARK   "{\"role\":\""                        /* every message starts so */
#define MSG_MARK_LEN  9
#define USER_REST  "user\",\"content\":\""               /* ...and a typed one goes on so */
#define USER_REST_LEN 17
#define LOOK       (3 + MSG_MARK_LEN + USER_REST_LEN)    /* ",\r\n" and both marks */

/* --- the scan -------------------------------------------------------------- */

/* What starts at q, which follows ",\r\n" if anything does: 0 no message, 1
   a message, 2 a typed message. The bytes come from buf (holding the file
   from bufPos, `have` of them) when they are all there, else a read. */
static int startAt(ChatRead rd, void *ctx, long q, const char *buf, long bufPos, unsigned have)
{
    char tmp[LOOK];
    const char *p;
    unsigned skip = q == 0 ? 3 : 0;   /* the file's start has no separator before it */
    long at = q - 3 + skip;

    if (q > 0 && q < 3)
        return 0;
    if (!skip && at >= bufPos && at + LOOK <= bufPos + have) {
        p = buf + (unsigned)(at - bufPos);
    } else {
        unsigned got = skip;
        int n = 0;
        memset(tmp, 0, sizeof tmp);
        while (got < LOOK && (n = rd(ctx, at + got - skip, tmp + got, LOOK - got)) > 0)
            got += n;
        if (n < 0)
            return -1;
        p = tmp;
    }
    if ((!skip && (p[0] != ',' || p[1] != '\r' || p[2] != '\n')) || memcmp(p + 3, MSG_MARK, MSG_MARK_LEN))
        return 0;
    return memcmp(p + 3 + MSG_MARK_LEN, USER_REST, USER_REST_LEN) ? 1 : 2;
}

int chatfile_scan(ChatRead rd, void *ctx, long size, ChatScan *s)
{
    char *buf = (char *)malloc(SCAN_BUF);
    long typed[CHATFILE_SHOW];
    long bufPos = 0, q = 0, recent = -1;
    unsigned have = 0, i = 0;
    int kind;

    s->exchanges = 0;
    s->last = -1;
    s->from = 0;
    if (!buf)
        return -1;
    /* The file's first byte, then the byte after each '\n' (the end of a
       separator, if it is one). */
    kind = size > 0 ? startAt(rd, ctx, 0, buf, 0, 0) : 0;
    for (;;) {
        char *nl;
        int n;

        if (kind < 0)
            break;
        if (kind > 0) {               /* a message at q, a typed one if 2 */
            s->last = q;
            if (recent < 0 && q >= size - CHATFILE_SHOW_MAX)
                recent = q;
            if (kind == 2)
                typed[s->exchanges++ % CHATFILE_SHOW] = q;
        }
        kind = 0;
        if (i >= have) {
            bufPos += have;
            have = i = 0;
            if (bufPos >= size)
                break;
            if ((n = rd(ctx, bufPos, buf, SCAN_BUF)) < 0) {
                kind = -1;
                break;
            }
            if (n == 0)
                break;
            have = (unsigned)n;
        }
        if ((nl = (char *)memchr(buf + i, '\n', have - i)) == NULL) {
            i = have;
            continue;
        }
        i = (unsigned)(nl - buf) + 1;
        q = bufPos + i;
        kind = startAt(rd, ctx, q, buf, bufPos, have);
    }
    free(buf);
    if (kind < 0)
        return -1;
    if (s->exchanges)
        s->from = typed[s->exchanges < CHATFILE_SHOW ? 0 : s->exchanges % CHATFILE_SHOW];
    if (s->from < size - CHATFILE_SHOW_MAX)
        s->from = recent >= 0 ? recent : (s->last > 0 ? s->last : 0);
    return 0;
}

/* --- reading messages ----------------------------------------------------- */

enum { T_NONE, T_KEY, T_ROLE, T_TYPE, T_NAME, T_ID, T_TEXT };

typedef struct Parse {
    const ChatShow *show;
    int depth, inStr, esc, expectKey, target;
    unsigned char list[DEPTH_MAX + 1];  /* the container at that depth is a list */
    char key[12], key1[12], key3[12];   /* the key being read; the last at depth 1 (a message), 3 (a block) */
    unsigned keyLen;
    char role[12], type[20], name[24], id[64];
    unsigned roleLen, typeLen, nameLen, idLen;
    int idCut;                          /* the block's id did not fit */
    int strContent;                     /* the message's content is a plain string */
    int call;                           /* the message has a tool call, its id in callId (2: cut) */
    char callId[64];
    int endKind;                        /* of the last message completed */
    unsigned textLen, inputLen;
    int textCut, inputCut;
    int inputDepth, inputStr, inputEsc; /* copying a tool call's input */
    char text[CHATFILE_TEXT_MAX];       /* a string being read, still escaped */
    char shown[CHATFILE_TEXT_MAX + 1];
    char input[INPUT_RAW];
    char buf[READ_BUF];
} Parse;

static void putc_(char *s, unsigned *len, unsigned max, char c)
{
    if (*len < max - 1)
        s[(*len)++] = c;
}

/* The escaped text cut at n bytes: back to before an escape the cut split. */
static unsigned escapeEnd(const char *s, unsigned n)
{
    unsigned i = n, back = 0;

    while (i > 0 && n - i < 6) {      /* the last backslash within an escape's reach */
        i--;
        if (s[i] == '\\')
            break;
    }
    if (i >= n || s[i] != '\\')
        return n;
    while (back < i && s[i - 1 - back] == '\\')
        back++;
    if (back & 1)                     /* an escaped backslash: it ended the escape */
        return n;
    if (i + 1 < n && s[i + 1] == 'u') {
        if (n - i < 6)
            return i;
        if (n - i == 6 && (s[i + 2] == 'd' || s[i + 2] == 'D')
            && strchr("89abAB", s[i + 3]) != NULL)
            return i;                 /* a high surrogate whose pair was cut off */
        return n;
    }
    return n - i >= 2 ? n : i;
}

/* UTF-8 cut at n bytes: back to the start of a character the cut split. */
static unsigned utf8End(const char *s, unsigned n)
{
    unsigned i = n, need;
    unsigned char c;

    while (i > 0 && n - i < 4 && ((unsigned char)s[i - 1] & 0xC0) == 0x80)
        i--;
    if (i == 0 || ((c = (unsigned char)s[i - 1]) & 0x80) == 0)
        return n;
    need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    return n - (i - 1) >= need ? n : i - 1;
}

/* Text (the escaped string in p->text) to whoever shows it. */
static void emitText(Parse *p, int user)
{
    unsigned n, m;
    if (!p->show || !p->textLen)
        return;
    if (p->textCut)                   /* cut again while that leaves a half pair at the end */
        while ((n = escapeEnd(p->text, p->textLen)) != p->textLen)
            p->textLen = n;
    n = json_unescape(p->text, p->textLen, p->text, sizeof p->text);   /* never longer: in place */
    if (p->textCut)
        n = utf8End(p->text, n);
    m = utf8_to_cp437(p->text, n, p->shown, CHATFILE_TEXT_MAX);
    p->shown[m] = 0;
    if (user && !strcmp(p->shown, CHAT_ROUND_CAP_NOTE))
        return;                       /* the harness's note beside the last result, not something typed */
    if (user ? p->show->user != NULL : p->show->reply != NULL)
        (user ? p->show->user : p->show->reply)(p->show->ctx, p->shown, p->textCut || m == CHATFILE_TEXT_MAX);
}

static int isUser(const Parse *p)
{
    return !strcmp(p->role, "user");
}

static void open_(Parse *p, int isList)
{
    p->depth++;
    p->list[p->depth <= DEPTH_MAX ? p->depth : DEPTH_MAX] = (unsigned char)isList;
    p->expectKey = !isList;
    if (p->depth == 1) {              /* a message */
        p->roleLen = 0;
        p->role[0] = p->key1[0] = 0;
        p->strContent = p->call = 0;
    } else if (p->depth == 3 && !isList) {   /* a block of the content list */
        p->typeLen = p->nameLen = p->idLen = p->textLen = p->inputLen = 0;
        p->idCut = 0;
        p->type[0] = p->name[0] = p->id[0] = p->key3[0] = 0;
        p->textCut = p->inputCut = 0;
    }
}

static void close_(Parse *p, ChatEnd *end)
{
    if (p->depth == 3 && !p->list[3]) {
        if (!strcmp(p->type, "text")) {
            emitText(p, isUser(p));
        } else if (!strcmp(p->type, "tool_use")) {
            p->call = p->idCut ? 2 : 1;
            strcpy(p->callId, p->id);
            if (p->show && p->show->tool)
                p->show->tool(p->show->ctx, p->name, p->input, p->inputLen);
        }
    } else if (p->depth == 1) {
        if (isUser(p))
            p->endKind = p->strContent ? CHAT_END_USER : CHAT_END_RESULTS;
        else
            p->endKind = p->call == 2 ? CHAT_END_BADCALL : p->call ? CHAT_END_CALL : CHAT_END_REPLY;
        if (end) {
            end->kind = p->endKind;
            strcpy(end->toolId, p->endKind == CHAT_END_CALL ? p->callId : "");
        }
    }
    if (p->depth > 0)
        p->depth--;
    p->expectKey = 0;                 /* a ',' comes first */
}

static void startString(Parse *p)
{
    const char *k = p->depth == 1 ? p->key1 : p->depth == 3 ? p->key3 : "";

    p->target = T_NONE;
    if (p->expectKey) {
        p->target = T_KEY;
        p->keyLen = 0;
    } else if (p->depth == 1 && !strcmp(k, "role")) {
        p->target = T_ROLE;
    } else if (p->depth == 1 && !strcmp(k, "content")) {
        p->target = T_TEXT;
        p->strContent = 1;
        p->textLen = 0;
        p->textCut = 0;
    } else if (p->depth == 3 && !p->list[3]) {
        if (!strcmp(k, "type"))
            p->target = T_TYPE;
        else if (!strcmp(k, "text"))
            p->target = T_TEXT;
        else if (!strcmp(k, "name"))
            p->target = T_NAME;
        else if (!strcmp(k, "id"))
            p->target = T_ID;
    }
}

static void endString(Parse *p)
{
    switch (p->target) {
    case T_KEY:
        p->key[p->keyLen] = 0;
        if (p->depth == 1)
            strcpy(p->key1, p->key);
        else if (p->depth == 3)
            strcpy(p->key3, p->key);
        break;
    case T_ROLE: p->role[p->roleLen] = 0; break;
    case T_TYPE: p->type[p->typeLen] = 0; break;
    case T_NAME: p->name[p->nameLen] = 0; break;
    case T_ID:   p->id[p->idLen] = 0; break;
    case T_TEXT:
        if (p->depth == 1)            /* a plain string content: the message's whole text */
            emitText(p, isUser(p));
        break;
    }
    p->target = T_NONE;
}

static void capture(Parse *p, char c)
{
    switch (p->target) {
    case T_KEY:  putc_(p->key, &p->keyLen, sizeof p->key, c); break;
    case T_ROLE: putc_(p->role, &p->roleLen, sizeof p->role, c); break;
    case T_TYPE: putc_(p->type, &p->typeLen, sizeof p->type, c); break;
    case T_NAME: putc_(p->name, &p->nameLen, sizeof p->name, c); break;
    case T_ID:
        if (p->idLen >= sizeof p->id - 1)
            p->idCut = 1;
        putc_(p->id, &p->idLen, sizeof p->id, c);
        break;
    case T_TEXT:
        if (p->textLen < sizeof p->text)
            p->text[p->textLen++] = c;
        else
            p->textCut = 1;
        break;
    }
}

/* A tool call's input object, copied as it is up to INPUT_RAW bytes. */
static void inputByte(Parse *p, char c)
{
    if (p->inputLen < sizeof p->input)
        p->input[p->inputLen++] = c;
    else
        p->inputCut = 1;
    if (p->inputStr) {
        if (p->inputEsc)
            p->inputEsc = 0;
        else if (c == '\\')
            p->inputEsc = 1;
        else if (c == '"')
            p->inputStr = 0;
    } else if (c == '"') {
        p->inputStr = 1;
    } else if (c == '{' || c == '[') {
        p->inputDepth++;
    } else if ((c == '}' || c == ']') && --p->inputDepth == 0) {
        p->expectKey = 0;
    }
}

static void feed(Parse *p, const char *b, unsigned n, ChatEnd *end)
{
    for (; n; n--, b++) {
        char c = *b;
        if (p->inputDepth) {
            inputByte(p, c);
            continue;
        }
        if (p->inStr) {
            if (p->esc)
                p->esc = 0;
            else if (c == '\\')
                p->esc = 1;
            else if (c == '"') {
                p->inStr = 0;
                endString(p);
                continue;
            }
            capture(p, c);
            continue;
        }
        switch (c) {
        case '"':
            p->inStr = 1;
            startString(p);
            break;
        case ':':
            p->expectKey = 0;
            break;
        case ',':
            p->expectKey = !p->list[p->depth <= DEPTH_MAX ? p->depth : DEPTH_MAX];
            break;
        case '{':
            if (p->depth == 3 && !p->list[3] && !strcmp(p->key3, "input")) {
                p->inputDepth = 0;
                p->inputStr = p->inputEsc = 0;
                inputByte(p, c);
                break;
            }
            open_(p, 0);
            break;
        case '[':
            open_(p, 1);
            break;
        case '}':
        case ']':
            close_(p, end);
            break;
        }
    }
}

int chatfile_read(ChatRead rd, void *ctx, long from, long to, const ChatShow *show, ChatEnd *end)
{
    Parse *p = (Parse *)malloc(sizeof(Parse));
    int rc = 0;

    if (end) {
        end->kind = CHAT_END_NONE;
        end->toolId[0] = 0;
    }
    if (!p)
        return -1;
    memset(p, 0, sizeof *p);
    p->show = show;
    while (from < to) {
        unsigned want = to - from > READ_BUF ? READ_BUF : (unsigned)(to - from);
        int n = rd(ctx, from, p->buf, want);
        if (n <= 0) {
            rc = n < 0 ? -1 : 0;
            break;
        }
        feed(p, p->buf, (unsigned)n, end);
        from += n;
    }
    free(p);
    return rc;
}

/* --- the list's summary ---------------------------------------------------- */

unsigned chatfile_first(char *raw, unsigned n, char *out, unsigned max)
{
    unsigned start = MSG_MARK_LEN + USER_REST_LEN, i, k, m;
    int esc = 0;

    out[0] = 0;
    if (n < start || memcmp(raw, MSG_MARK, MSG_MARK_LEN) || memcmp(raw + MSG_MARK_LEN, USER_REST, USER_REST_LEN))
        return 0;
    for (i = start; i < n; i++) {     /* to the string's closing quote, or as far as there is */
        if (esc)
            esc = 0;
        else if (raw[i] == '\\')
            esc = 1;
        else if (raw[i] == '"')
            break;
    }
    k = json_unescape(raw + start, i - start, raw, n);
    m = utf8_to_cp437(raw, k, out, max - 1);
    out[m] = 0;
    for (i = 0; i < m; i++)
        if ((unsigned char)out[i] < ' ')
            out[i] = ' ';
    return m;
}
