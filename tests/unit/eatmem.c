/* EATMEM KB PROG [ARGS]: takes memory from DOS until the largest free block is
 * KB kilobytes (or that many bytes, when over 4096), then runs PROG (started
 * directly) and says what was left and how it ended: a program at a chosen
 * edge of memory, without a harness of that size.
 * EATMEM KB /K: the same, then stays resident with that memory taken, so the
 * programs started after it get only KB (run inside the harness, it makes the
 * harness's next runs short of memory; nothing but a reboot gives it back).
 * EATMEM /F frees what a resident EATMEM keeps (the boot tests go on after it).
 * EATMEM 0 just prints the largest free block.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <dos.h>
#include <i86.h>
#include <malloc.h>

static unsigned largest(void)
{
    union REGS r;
    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    intdos(&r, &r);
    return r.x.bx;
}

/* The first memory control block (INT 21h 52h: the word before the list of lists). */
static unsigned firstMcb(void)
{
    union REGS r;
    struct SREGS sr;
    segread(&sr);
    r.h.ah = 0x52;
    intdosx(&r, &r, &sr);
    return *(const unsigned __far *)MK_FP(sr.es, r.x.bx - 2);
}

/* Frees every block owned by a resident EATMEM other than this one: its
   program block (named EATMEM by DOS) names it. */
static int freeResident(void)
{
    unsigned seg, owner, gone = 0, freed = 0, me = _psp;
    const unsigned char __far *m;

    for (seg = firstMcb(); ; seg += *(const unsigned __far *)(m + 3) + 1) {
        m = (const unsigned char __far *)MK_FP(seg, 0);
        owner = *(const unsigned __far *)(m + 1);
        if (owner == seg + 1 && owner != me && !_fmemcmp(m + 8, "EATMEM", 6))
            gone = owner;
        if (m[0] != 'M')
            break;
    }
    if (!gone)
        return puts("EATMEM: none resident"), 1;
    for (seg = firstMcb(); ; seg += *(const unsigned __far *)(m + 3) + 1) {
        m = (const unsigned char __far *)MK_FP(seg, 0);
        if (*(const unsigned __far *)(m + 1) == gone && _dos_freemem(seg + 1) == 0)
            freed++;
        if (m[0] != 'M')
            break;
    }
    printf("EATMEM: freed %u blocks; largest free %lu bytes\n", freed, (unsigned long)largest() * 16);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned want, have, seg;
    long n;
    int code, keep = argc > 2 && !stricmp(argv[2], "/K");

    if (argc < 2)
        return puts("usage: EATMEM KB [PROG ARGS | /K] | /F"), 2;
    if (!stricmp(argv[1], "/F"))
        return freeResident();
    if (keep) {                             /* the hole left below what it keeps: as small as it can be */
        setvbuf(stdout, NULL, _IONBF, 0);
        _nheapshrink();
    }
    n = atol(argv[1]);                      /* KB, or bytes when over 4096 */
    want = (unsigned)(n > 4096 ? n / 16 : n * 64);
    have = largest();
    if (want && have > want + 1) {
        if (_dos_allocmem(have - want - 1, &seg))     /* its own header is the 1 */
            return printf("EATMEM: cannot take %u paragraphs\n", have - want - 1), 2;
    }
    printf("EATMEM: largest free %lu bytes\n", (unsigned long)largest() * 16);
    if (argc < 3)
        return 0;
    fflush(stdout);
    if (keep)
        _dos_keep(0, 16);                   /* the PSP alone: no code of ours runs again */
    code = spawnv(P_WAIT, argv[2], (const char **)argv + 2);
    printf("EATMEM: %s exit %d\n", argv[2], code);
    return 0;
}
