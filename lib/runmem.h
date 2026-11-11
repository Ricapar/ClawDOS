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

/* The slots' sizes in bytes, a multiple of 16 each; each use checks its
   owner's size against them (RM_FITS). */
#define RM_SSE_BYTES    16384u        /* chat.cpp: one SSE event */
#define RM_TOOLIN_BYTES 16384u        /* chat.cpp: a tool_use input's JSON */
#define RM_ARGS_BYTES   16384u        /* toolapi.h's TOOL_ARGS_MAX: the argument store */
#define RM_DECODE_BYTES 16384u        /* TOOL_ARGS_MAX again: one argument, unescaped */
#define RM_RX_BYTES     2048u         /* chat.cpp: one network read */
#define RM_HTTP_RX_BYTES   2048u      /* http.h's HTTP_RX_MAX: raw bytes from the socket */
#define RM_HTTP_HEAD_BYTES 4096u      /* http.h's HTTP_HEAD_MAX: a response's head */
#define RM_HTTP_TX_BYTES   512u       /* http.h's HTTP_TX_MAX: staged outgoing bytes */
#define RM_SCRATCH_BYTES   2048u      /* apistream.h's APISTREAM_SCRATCH */
#define RM_ERRTEXT_BYTES   4112u      /* "Error: " and a failed call's TOOL_OUT_MAX */
#define RM_ERRBODY_BYTES   1600u      /* chat.cpp's ERR_BODY_MAX: an API error's body */

/* Beside each use of a slot: a compile error unless `need` bytes fit in the
   slot's `bytes` (an owner's buffer growing would overrun the next slot). */
#define RM_FITS(need, bytes) extern char runmem_fits[(need) <= (bytes) ? 1 : -1]

/* A DOS block of `paras` paragraphs: its segment, or 0 when DOS has none.
   (Open Watcom's _dos_allocmem leaves the largest free block's size in its
   segment argument when it fails: never free that.) */
unsigned dos_block(unsigned paras);

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
