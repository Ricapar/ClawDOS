/* Claude-XT: the BIOS tick counter at 0040:006C, 18.2 ticks a second.
 * It is reset to 0 at midnight (after TICKS_PER_DAY ticks), so two
 * readings must never be subtracted directly: use ticks_elapsed(). Time-outs
 * are then "ticks_elapsed(TICKS_NOW, start) >= SECS_TO_TICKS(n)".
 */
#ifndef CXT_TICKS_H
#define CXT_TICKS_H

#include <i86.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TICKS_NOW           (*(volatile unsigned long __far *)MK_FP(0x40, 0x6C))
#define TICKS_PER_DAY       0x1800B0ul
#define SECS_TO_TICKS(s)    ((unsigned long)(s) * 182 / 10)
#define TICKS_TO_SECS(t)    ((unsigned)((t) * 10 / 182))

/* Ticks from `start` to `now`, both readings of TICKS_NOW, allowing for one
   midnight between them. */
unsigned long ticks_elapsed(unsigned long now, unsigned long start);

#ifdef __cplusplus
}
#endif

#endif
