/* ClawDOS tool calls: what an executor sees. A call's arguments are strings
 * (targ_set stores them, converted to CP437; JSON numbers as written, booleans
 * as "1" and "0"); an executor reads them with
 * targ*, writes its result for the model with tout_*, sets a one-line summary
 * for the transcript, and returns one of the TS_ statuses.
 */
#ifndef CLAWDOS_TOOLAPI_H
#define CLAWDOS_TOOLAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* TS_RELAUNCH: a run handed to the launcher (relaunch.h); the result comes
   after CLAWDOS restarts. */
enum { TS_OK, TS_ERROR, TS_DENIED, TS_STOPPED, TS_RELAUNCH };

#define TOOL_OUT_MAX  4096
#define TOUT_RESERVE  100     /* kept free by tout_line for the lines that end a result */
#define TOOL_ARGS_MAX 16384u

/* Arguments of the current call, kept in a far buffer of TOOL_ARGS_MAX bytes. */
void        targ_reset(void);
/* Set key to the UTF-8 text (n bytes), converted to CP437 with tabs kept.
   -1 if the store is full. */
int         targ_set(const char *key, const char *utf8, unsigned n);
const char *targ(const char *key);                 /* NULL if absent */
/* A flag: 1 for true, "true" or "1"; 0 for false, "false", "0", or absent;
   -1 for anything else. */
int         targ_flag(const char *key);
int         targ_hex(const char *key, unsigned *out);                /* "3D9"; -1 if bad */
int         targ_addr(const char *key, unsigned *seg, unsigned *off); /* "B800:0000" */

typedef struct ToolCtx ToolCtx;

/* The flag key in *on; -1 after failing the call with an error naming key
   when it is neither true nor false. */
int         targ_bool(ToolCtx *t, const char *key, int *on);
/* The whole number key (decimal, or 0x hex) in *n, def when absent; -1 after
   failing the call with an error naming key when it isn't a number. */
int         targ_long(ToolCtx *t, const char *key, long def, long *n);

struct ToolCtx {
    char *out;                /* result for the model, NUL-terminated */
    unsigned len;
    int truncated;            /* some output didn't fit */
    char summary[64];         /* shown in the transcript */
    unsigned showFrom, showTo; /* out[showFrom..showTo): what the transcript shows the
                                  last lines of (a run's last output); empty if equal */
};

void tout_reset(ToolCtx *t);
/* Append text; what doesn't fit in TOOL_OUT_MAX is cut off (sets truncated). */
void tout_puts(ToolCtx *t, const char *s);
void tout_printf(ToolCtx *t, const char *fmt, ...);
/* For results that stop early with a line saying so: 1 if n more bytes fit
   and still leave TOUT_RESERVE for that line, else 0 (and truncated is set,
   so later lines are refused too). */
int  tout_room(ToolCtx *t, unsigned n);
/* Append a whole line if tout_room allows it: 0, or -1 with nothing written. */
int  tout_line(ToolCtx *t, const char *fmt, ...);
/* A list whose lines were cut short when the output filled (truncated): end
   it on its last whole line that leaves room for the line `more` (no '\n'),
   then that line. Nothing changes when nothing was cut. */
void tout_end_list(ToolCtx *t, const char *more);
/* 16 bytes per line. seg is printed as "SSSS:OOOO"; seg 0xFFFF prints file
   offsets ("00000100") instead. */
void tout_hexdump(ToolCtx *t, const unsigned char __far *p, unsigned n,
                  unsigned seg, unsigned long off);
int  tool_fail(ToolCtx *t, const char *msg);      /* error text + summary; TS_ERROR */

int  tool_esc(void);           /* drains typed keys; 1 if Esc was among them */
/* Called by tool_esc, which long-running tools call often: the app keeps its
   network connection alive from here (NULL: nothing to do). */
extern void (*tool_idle)(void);
const char *dos_error(int code);

typedef int (*ToolFn)(ToolCtx *t);

#ifdef __cplusplus
}
#endif

#endif
