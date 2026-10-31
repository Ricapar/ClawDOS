/* ClawDOS tool calls: what an executor sees. A call's arguments are strings
 * (targ_set stores them, converted to CP437); an executor reads them with
 * targ*, writes its result for the model with tout_*, sets a one-line summary
 * for the transcript, and returns one of the TS_ statuses.
 */
#ifndef CLAWDOS_TOOLAPI_H
#define CLAWDOS_TOOLAPI_H

#ifdef __cplusplus
extern "C" {
#endif

enum { TS_OK, TS_ERROR, TS_DENIED, TS_STOPPED };

#define TOOL_OUT_MAX  4096
#define TOOL_ARGS_MAX 16384u

/* Arguments of the current call, kept in a far buffer of TOOL_ARGS_MAX bytes. */
void        targ_reset(void);
/* Set key to the UTF-8 text (n bytes), converted to CP437. -1 if the store is full. */
int         targ_set(const char *key, const char *utf8, unsigned n);
const char *targ(const char *key);                 /* NULL if absent */
long        targ_num(const char *key, long def);   /* decimal or 0x hex */
int         targ_hex(const char *key, unsigned *out);                /* "3D9"; -1 if bad */
int         targ_addr(const char *key, unsigned *seg, unsigned *off); /* "B800:0000" */

typedef struct ToolCtx {
    char *out;                /* result for the model, NUL-terminated */
    unsigned len;
    int truncated;            /* some output didn't fit */
    char summary[64];         /* shown in the transcript */
    int unattended;           /* nobody at the keyboard (-q): don't wait long */
} ToolCtx;

void tout_reset(ToolCtx *t);
void tout_puts(ToolCtx *t, const char *s);
void tout_printf(ToolCtx *t, const char *fmt, ...);
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
