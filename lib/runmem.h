/* ClawDOS memory lent to run programs: the harness's buffers whose contents
 * nothing needs while a `run` tool program executes (the event buffer, the
 * tool input, the argument store and its decode buffer, the network and HTTP
 * buffers, the stream's scratch, an error's text) live in one DOS block at
 * the top of memory, outside the C heap, which keeps whatever it is given. Before a program starts the
 * block goes back to DOS, where it joins the free memory below it (DOS gives
 * a program the largest free block, so only memory next to it counts); after
 * the program it is taken again. Its contents are lost in between, and it may
 * come back at another address: the owners ask runmem_slot for it each time
 * they start using it, and keep no pointer across a run.
 */
#ifndef CLAWDOS_RUNMEM_H
#define CLAWDOS_RUNMEM_H

#ifdef __cplusplus
extern "C" {
#endif

enum { RM_SSE, RM_TOOLIN, RM_ARGS, RM_DECODE, RM_RX, RM_HTTP_RX, RM_HTTP_HEAD, RM_HTTP_TX, RM_SCRATCH,
       RM_ERRTEXT, RM_ERRBODY, RM_SLOTS };

/* The slots' sizes in bytes, a multiple of 16 each. */
#define RM_SSE_BYTES    16384u        /* chat.cpp: one SSE event */
#define RM_TOOLIN_BYTES 16384u        /* chat.cpp: a tool_use input's JSON */
#define RM_ARGS_BYTES   16384u        /* toolapi.c: the argument store */
#define RM_DECODE_BYTES 16384u        /* tools.cpp: one argument, unescaped */
#define RM_RX_BYTES     2048u         /* chat.cpp: one network read */
#define RM_HTTP_RX_BYTES   2048u      /* http.cpp: raw bytes from the socket (HTTP_RX_MAX) */
#define RM_HTTP_HEAD_BYTES 4096u      /* http.cpp: a response's head (HTTP_HEAD_MAX) */
#define RM_HTTP_TX_BYTES   512u       /* http.cpp: staged outgoing bytes */
#define RM_SCRATCH_BYTES   2048u      /* apistream.c: a delta unescaped, and as CP437 */
#define RM_ERRTEXT_BYTES   4112u      /* tools.cpp: "Error: " and a failed call's output */
#define RM_ERRBODY_BYTES   1600u      /* chat.cpp: an API error's body */

/* Takes the block (at the top of memory), when it is not held: 0, or -1 when
   DOS has no room for it. */
int  runmem_take(void);
/* Slot i of the block, NULL while it is not held. */
char *runmem_slot(int i);
/* Gives the block back to DOS (nothing when it is not held). */
void runmem_lend(void);
/* 1 while the block is held. */
int  runmem_held(void);

#ifdef __cplusplus
}
#endif

#endif
