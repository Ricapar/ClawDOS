/* The conversation's messages as the Messages API wants them, written to the
 * history file: the user's text, the assistant's reply (streamed in by
 * apistream) and tool results; and the rules that keep the file valid when a
 * turn ends early. Roles must alternate, so a user text that follows a tool
 * results message joins it as a text block.
 */
#ifndef CLAWDOS_CONVO_H
#define CLAWDOS_CONVO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Convo {
    int mergeNext;        /* the last message is the user's: the next text joins it */
    int mergeAtStart;     /* mergeNext when the turn began, for a rollback */
    int assistantInTurn;  /* assistant messages completed this turn */
} Convo;

void convo_init(Convo *c);
void convo_turn_begin(Convo *c);                     /* hist_turn_begin + remember mergeNext */
int  convo_user_text(Convo *c, const char *cp437);   /* a user message, or a text block joining the results */
/* The streamed assistant message is complete. -1: it could not all be
   written, and it has been rolled back (convo_settle then ends the turn). */
int  convo_assistant_done(Convo *c);
/* The result of the call `id`. -1: it could not be written, and the turn must
   end: the file then ends with a short error result for the call, or (when
   even that could not be written) the turn has been rolled back. */
int  convo_tool_result(Convo *c, const char *id, const char *cp437Text, int isError, const char *note);
/* The turn ended early. No assistant message yet: the whole turn is rolled
   back. Otherwise the file ends with a tool results message, which the next
   user text joins. Returns what the file ends with: 0 the user's text or an
   assistant message is last, 1 a results message is last. */
int  convo_settle(Convo *c);
void convo_rollback_turn(Convo *c);                  /* hist_rollback_turn + restore mergeNext */

#ifdef __cplusplus
}
#endif

#endif
