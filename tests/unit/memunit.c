/* DOSUNIT: lib/runmem.c, the block the harness lends to run programs, on the
 * DOS the tests run under. In a module of its own, for room in the code
 * segment. It also times the lending and the taking back (and the C heap's
 * trim that goes with them), which the 8088 check reports.
 */

#include <stdio.h>
#include <malloc.h>
#include <dos.h>
#include <i86.h>
#include "runmem.h"
#include "ticks.h"

void check(const char *name, int ok, const char *detail);   /* dosunit.c */
#define CHECK(name, cond) check((name), (cond), "")

#define TIMES 200

static const unsigned bytes[RM_SLOTS] = {
    RM_SSE_BYTES, RM_TOOLIN_BYTES, RM_ARGS_BYTES, RM_DECODE_BYTES, RM_RX_BYTES, RM_HTTP_RX_BYTES,
    RM_HTTP_HEAD_BYTES, RM_HTTP_TX_BYTES, RM_SCRATCH_BYTES, RM_ERRTEXT_BYTES, RM_ERRBODY_BYTES
};

static unsigned strategy(void)
{
    union REGS r;
    r.x.ax = 0x5800;
    intdos(&r, &r);
    return r.x.ax;
}

static unsigned largest(void)
{
    union REGS r;
    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    intdos(&r, &r);
    return r.x.bx;
}

void testRunmem(void)
{
    unsigned seg, low, total = 0, before = strategy(), i;
    const unsigned char __far *next;
    unsigned long start, took;
    int offsets = 1;

    CHECK("runmem: dos_block of more than there is is 0", dos_block(0xFFFF) == 0);
    low = dos_block(1);
    CHECK("runmem: dos_block of a paragraph", low != 0);
    for (i = 0; i < RM_SLOTS; i++)
        total += bytes[i] / 16;
    CHECK("runmem: not held at first", !runmem_held() && runmem_slot(RM_SSE) == NULL);
    CHECK("runmem: taken", runmem_take() == 0 && runmem_held());
    CHECK("runmem: the allocation strategy restored", strategy() == before);
    seg = FP_SEG(runmem_slot(0));
    for (i = 0; i < RM_SLOTS; i++) {
        offsets &= FP_OFF(runmem_slot(i)) == 0;
        if (i + 1 < RM_SLOTS)
            offsets &= FP_SEG(runmem_slot(i + 1)) - FP_SEG(runmem_slot(i)) == bytes[i] / 16;
    }
    CHECK("runmem: slots one after another", offsets);
    CHECK("runmem: the block is 82,000 bytes", total == 82000u / 16);
    CHECK("runmem: taken again is the same block", runmem_take() == 0 && FP_SEG(runmem_slot(0)) == seg);
    /* last fit: above a first-fit block, and nothing free above it */
    next = (const unsigned char __far *)MK_FP(seg + total, 0);
    CHECK("runmem: at the top of memory", seg > low
          && (*(const unsigned __far *)(next + 1) != 0 || (next[0] != 'M' && next[0] != 'Z')));
    runmem_lend();
    CHECK("runmem: lent, not held, slots NULL", !runmem_held() && runmem_slot(RM_ERRBODY) == NULL);
    CHECK("runmem: lent, DOS has it", largest() >= total);
    runmem_lend();                    /* twice: nothing */
    CHECK("runmem: back after lending", runmem_take() == 0 && runmem_held());
    _dos_freemem(low);

    start = TICKS_NOW;
    for (i = 0; i < TIMES; i++) {
        runmem_lend();
        runmem_take();
    }
    took = ticks_elapsed(TICKS_NOW, start);
    printf("runmem: lend and take back %u times in %lu ms (about %lu us each)\n", TIMES, took * 55,
           took * 55000UL / TIMES);
    start = TICKS_NOW;
    for (i = 0; i < TIMES; i++)
        _fheapshrink();
    took = ticks_elapsed(TICKS_NOW, start);
    printf("runmem: the C heap trimmed %u times in %lu ms (about %lu us each)\n", TIMES, took * 55,
           took * 55000UL / TIMES);
    runmem_lend();
}
