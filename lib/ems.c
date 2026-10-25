/* Claude-XT EMS: see ems.h. LIM EMS 3.2 calls through INT 67h. */

#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "ems.h"

static int state;                 /* 0 untried, 1 have a page, -1 no EMS */
static unsigned handle;
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

void __far *ems_page(void)
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
        r.h.ah = 0x43;            /* allocate one page */
        r.x.bx = 1;
        if (ems(&r))
            return NULL;
        handle = r.x.dx;
        state = 1;
        atexit(release);
    }
    if (state != 1)
        return NULL;
    r.h.ah = 0x44;                /* map logical page 0 to physical page 0 */
    r.h.al = 0;
    r.x.bx = 0;
    r.x.dx = handle;
    if (ems(&r))
        return NULL;
    return MK_FP(frame, 0);
}
