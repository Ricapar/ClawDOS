/* Conversation messages, see convo.h. */
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include "convo.h"
#include "history.h"
#include "json.h"

#define RESULT_LOST "Error: the result could not be written to the conversation file."
#define IMAGE_LOST  "(The screenshot could not be attached: its file could not be read or written.)"
#define B64_IN      192             /* file bytes a piece (a multiple of 3) */
#define B64_OUT     (B64_IN / 3 * 4)

static int histPut(Sink *s, const char *p, unsigned n)
{
    (void)s;
    return hist_write(p, n);
}

static void histSink(Sink *s)
{
    s->put = histPut;
    s->ctx = 0;
    s->count = 0;
}

void convo_init(Convo *c)
{
    memset(c, 0, sizeof *c);
}

void convo_turn_begin(Convo *c)
{
    hist_turn_begin();
    c->mergeAtStart = c->mergeNext;
    c->assistantInTurn = 0;
}

int convo_user_text(Convo *c, const char *text)
{
    Sink s;
    int rc = 0;
    histSink(&s);
    if (c->mergeNext == CONVO_JOIN_TEXT) {
        rc |= hist_reopen_last(2);             /* the "} of the typed message never answered */
        rc |= hist_puts("\\n\\n");
    } else if (c->mergeNext) {
        rc |= hist_reopen_last(2);             /* the "]}" of the tool results message */
        rc |= hist_puts(",{\"type\":\"text\",\"text\":\"");
    } else {
        rc |= hist_puts("{\"role\":\"user\",\"content\":\"");
    }
    rc |= json_escape_cp437(&s, text, strlen(text));
    rc |= hist_puts(c->mergeNext == CONVO_JOIN_RESULTS ? "\"}]}" : "\"}");
    rc |= hist_message_done();
    c->mergeNext = 0;
    return rc ? -1 : 0;
}

int convo_assistant_done(Convo *c)
{
    if (hist_message_done()) {
        hist_rollback_message();               /* not all written: the reply goes */
        return -1;
    }
    c->assistantInTurn++;
    c->mergeNext = 0;
    return 0;
}

/* The file at path, base64, into the history a piece at a time: a picture is
   ~16 KB, too big for any buffer we keep. -1 if it cannot be read (or is
   empty) or a write failed. */
static int putBase64(const char *path)
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char *in = (unsigned char *)malloc(B64_IN + B64_OUT);
    char *enc = (char *)in + B64_IN;
    unsigned long total = 0;
    int fd, n = 0, rc = 0;

    if (in == NULL)
        return -1;
    fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) {
        free(in);
        return -1;
    }
    for (;;) {
        unsigned got = 0, i, k = 0;
        while (got < B64_IN && (n = read(fd, in + got, B64_IN - got)) > 0)
            got += n;
        if (n < 0) {
            rc = -1;
            break;
        }
        for (i = 0; i < got; i += 3) {
            unsigned long v = (unsigned long)in[i] << 16;
            if (i + 1 < got)
                v |= (unsigned)in[i + 1] << 8;
            if (i + 2 < got)
                v |= in[i + 2];
            enc[k++] = digits[(unsigned)(v >> 18) & 63];
            enc[k++] = digits[(unsigned)(v >> 12) & 63];
            enc[k++] = i + 1 < got ? digits[(unsigned)(v >> 6) & 63] : '=';
            enc[k++] = i + 2 < got ? digits[(unsigned)v & 63] : '=';
        }
        total += got;
        if (k && hist_write(enc, k)) {
            rc = -1;
            break;
        }
        if (got < B64_IN)
            break;                     /* the end of the file */
    }
    close(fd);
    free(in);
    return rc || total == 0 ? -1 : 0;
}

static int writeResult(const char *id, const char *text, int isError, const char *note, const char *image)
{
    Sink s;
    int rc = 0;
    histSink(&s);
    rc |= hist_puts("{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"");
    rc |= hist_puts(id);
    rc |= hist_puts(image ? "\",\"content\":[{\"type\":\"text\",\"text\":\"" : "\",\"content\":\"");
    rc |= json_escape_cp437(&s, text, strlen(text));
    if (image) {
        rc |= hist_puts("\"},{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/png\",\"data\":\"");
        rc |= rc ? -1 : putBase64(image);
        rc |= hist_puts("\"}}]");
    } else {
        rc |= hist_puts("\"");
    }
    rc |= hist_puts(isError ? ",\"is_error\":true}" : ",\"is_error\":false}");
    if (note) {
        rc |= hist_puts(",{\"type\":\"text\",\"text\":\"");
        rc |= json_escape_cp437(&s, note, strlen(note));
        rc |= hist_puts("\"}");
    }
    rc |= hist_puts("]}");
    rc |= hist_message_done();
    return rc ? -1 : 0;
}

int convo_tool_result(Convo *c, const char *id, const char *text, int isError, const char *note)
{
    return convo_tool_result_image(c, id, text, isError, note, NULL);
}

int convo_tool_result_image(Convo *c, const char *id, const char *text, int isError, const char *note,
                            const char *image)
{
    if (writeResult(id, text, isError, note, image) == 0) {
        c->mergeNext = CONVO_JOIN_RESULTS;     /* a results message is last until the next request answers it */
        return 0;
    }
    hist_rollback_message();
    if (image) {                               /* the result without its picture */
        if (writeResult(id, text, isError, note ? note : IMAGE_LOST, NULL) == 0) {
            c->mergeNext = CONVO_JOIN_RESULTS;
            return 0;
        }
        hist_rollback_message();
    }
    /* The call must still be answered for the file to stay valid: a short
       error result, or failing that the whole turn goes. */
    if (writeResult(id, RESULT_LOST, 1, NULL, NULL) == 0) {
        c->mergeNext = CONVO_JOIN_RESULTS;
        return -1;
    }
    hist_rollback_message();
    convo_rollback_turn(c);
    return -1;
}

int convo_settle(Convo *c)
{
    if (c->assistantInTurn == 0) {
        convo_rollback_turn(c);
        return c->mergeNext;
    }
    return c->mergeNext;                       /* results last: the next text joins them */
}

void convo_rollback_turn(Convo *c)
{
    hist_rollback_turn();
    c->mergeNext = c->mergeAtStart;
    c->assistantInTurn = 0;
}
