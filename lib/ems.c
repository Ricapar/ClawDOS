/* ClawDOS EMS: see ems.h. LIM EMS 3.2 calls through INT 67h. */

#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "ems.h"

static int state;                 /* 0 untried, 1 have pages, -1 no EMS */
static unsigned handle;
static unsigned pages;            /* allocated: 2, or 1 when EMS had no more */
static unsigned frame;            /* page frame segment */

static int ems(union REGS *r)
{
    int86(0x67, r, r);
    return r->h.ah;               /* 0 = success */
}

static void release(void)
{
    union REGS r;
    if (state == 1) {
        r.h.ah = 0x45;            /* deallocate pages */
        r.x.dx = handle;
        ems(&r);
        state = 0;
    }
}

/* An EMS driver is a device named EMMXXXX0 behind INT 67h. */
static int present(void)
{
    void (__interrupt __far *vec)() = _dos_getvect(0x67);
    return vec != NULL && _fmemcmp(MK_FP(FP_SEG(vec), 10), "EMMXXXX0", 8) == 0;
}

void __far *ems_map(unsigned n)
{
    union REGS r;

    if (state == 0) {
        state = -1;
        if (!present())
            return NULL;
        r.h.ah = 0x40;            /* status */
        if (ems(&r))
            return NULL;
        r.h.ah = 0x41;            /* page frame segment */
        if (ems(&r))
            return NULL;
        frame = r.x.bx;
        for (pages = 2; pages > 0; pages--) {
            r.h.ah = 0x43;        /* allocate pages */
            r.x.bx = pages;
            if (ems(&r) == 0)
                break;
        }
        if (pages == 0)
            return NULL;
        handle = r.x.dx;
        state = 1;
        atexit(release);
    }
    if (state != 1 || n >= pages)
        return NULL;
    r.h.ah = 0x44;                /* map logical page n to physical page n */
    r.h.al = (unsigned char)n;
    r.x.bx = n;
    r.x.dx = handle;
    if (ems(&r))
        return NULL;
    return MK_FP(frame + n * (EMS_PAGE_BYTES / 16), 0);
}
