/* Conversation messages, see convo.h. */
#include <string.h>
#include "convo.h"
#include "history.h"
#include "json.h"

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
    if (c->mergeNext) {
        rc |= hist_reopen_last(2);             /* the "]}" of the tool results message */
        rc |= hist_puts(",{\"type\":\"text\",\"text\":\"");
    } else {
        rc |= hist_puts("{\"role\":\"user\",\"content\":\"");
    }
    rc |= json_escape_cp437(&s, text, strlen(text));
    rc |= hist_puts(c->mergeNext ? "\"}]}" : "\"}");
    rc |= hist_message_done();
    c->mergeNext = 0;
    return rc ? -1 : 0;
}

int convo_assistant_done(Convo *c)
{
    if (hist_message_done())
        return -1;                             /* not written: the caller rolls the message back */
    c->assistantInTurn++;
    c->mergeNext = 0;
    return 0;
}

int convo_tool_result(Convo *c, const char *id, const char *text, int isError, const char *note)
{
    Sink s;
    int rc = 0;
    histSink(&s);
    rc |= hist_puts("{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"");
    rc |= hist_puts(id);
    rc |= hist_puts("\",\"content\":\"");
    rc |= json_escape_cp437(&s, text, strlen(text));
    rc |= hist_puts(isError ? "\",\"is_error\":true}" : "\",\"is_error\":false}");
    if (note) {
        rc |= hist_puts(",{\"type\":\"text\",\"text\":\"");
        rc |= json_escape_cp437(&s, note, strlen(note));
        rc |= hist_puts("\"}");
    }
    rc |= hist_puts("]}");
    rc |= hist_message_done();
    c->mergeNext = 1;                          /* a results message is last until the next request answers it */
    return rc ? -1 : 0;
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
