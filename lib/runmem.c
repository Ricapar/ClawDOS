/* ClawDOS memory lent to run programs: see runmem.h. */

#include <dos.h>
#include <i86.h>
#include "runmem.h"

/* Each slot's size in paragraphs; a slot starts where the one before ends. */
static const unsigned paras[RM_SLOTS] = {
    RM_SSE_BYTES / 16, RM_TOOLIN_BYTES / 16, RM_ARGS_BYTES / 16, RM_DECODE_BYTES / 16, RM_RX_BYTES / 16,
    RM_HTTP_RX_BYTES / 16, RM_HTTP_HEAD_BYTES / 16, RM_HTTP_TX_BYTES / 16, RM_SCRATCH_BYTES / 16,
    RM_ERRTEXT_BYTES / 16, RM_ERRBODY_BYTES / 16
};
static unsigned seg;                  /* 0 while not held */

/* DOS's allocation strategy (INT 21h 58h): get, or set. */
static unsigned strategy(int set, unsigned value)
{
    union REGS r;
    r.x.ax = set ? 0x5801 : 0x5800;
    r.x.bx = value;
    intdos(&r, &r);
    return r.x.ax;
}

/* Last fit puts the block at the top of the highest free block, above where
   the C heap grows, so freeing it adds to the free memory below it. */
int runmem_take(void)
{
    unsigned total = 0, old, got;
    int i, rc;

    if (seg)
        return 0;
    for (i = 0; i < RM_SLOTS; i++)
        total += paras[i];
    old = strategy(0, 0);
    strategy(1, 2);
    rc = _dos_allocmem(total, &got);
    strategy(1, old);
    if (rc)
        return -1;
    seg = got;
    return 0;
}

char *runmem_slot(int i)
{
    unsigned at = seg;
    int k;

    if (!seg)
        return 0;
    for (k = 0; k < i; k++)
        at += paras[k];
    return (char *)MK_FP(at, 0);
}

void runmem_lend(void)
{
    if (seg)
        _dos_freemem(seg);
    seg = 0;
}

int runmem_held(void)
{
    return seg != 0;
}
