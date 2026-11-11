/* ClawDOS screenshots, the capture: see shot.h. */

#include <dos.h>
#include <i86.h>
#include "shot.h"
#include "ems.h"

#define BIOS_SEG   0x40
#define VRAM_SEG   0xB800

/* Changed by the timer hook: volatile, or the optimizer carries the values
   from before the run across it. */
static volatile unsigned left;              /* ticks to go; 0: nothing to do */
static volatile unsigned char taken;        /* the copy was made */
static volatile unsigned char bios[3];      /* mode, mode and colour shadows at that moment */
static unsigned copySeg;                    /* where the copy goes (offset 0) */
static unsigned dosSeg;                     /* the DOS block, when there was no EMS */
static int armed;                           /* 0, 1 armed, -1 asked but no buffer */
static void (__interrupt __far *oldInt1C)();

/* Runs on whatever stack the timer interrupted, while the model's program
   runs. Like lib/shell.c's and lib/screen.c's hooks it calls none of our
   functions: the copy is inlined. 8 K words at the CGA's wait states take
   about 40 ms on the 8088, once. */
static void __interrupt __far shotIsr(void)
{
    if (left && --left == 0) {
        unsigned dst = copySeg;
        _asm {
            push    ds
            push    es
            push    si
            push    di
            mov     ax, dst
            mov     es, ax
            mov     ax, VRAM_SEG
            mov     ds, ax
            xor     si, si
            xor     di, di
            mov     cx, 2000h
            cld
            rep     movsw
            pop     di
            pop     si
            pop     es
            pop     ds
        }
        bios[0] = *(volatile unsigned char __far *)MK_FP(BIOS_SEG, 0x49);
        bios[1] = *(volatile unsigned char __far *)MK_FP(BIOS_SEG, 0x65);
        bios[2] = *(volatile unsigned char __far *)MK_FP(BIOS_SEG, 0x66);
        taken = 1;
    }
    _chain_intr(oldInt1C);
}

/* The EMS page's segment, or 0: the frame address normalized so that the
   copy starts at offset 0. */
static unsigned emsSeg(void)
{
    void __far *p = ems_map(EMS_SHOT_PAGE);
    return p ? FP_SEG(p) + (FP_OFF(p) >> 4) : 0;
}

int shot_arm(unsigned ticks)
{
    taken = 0;
    left = 0;
    copySeg = emsSeg();
    if (copySeg == 0) {
        if (_dos_allocmem(SHOT_BYTES / 16, &dosSeg)) {
            dosSeg = 0;
            armed = -1;
            return -1;
        }
        copySeg = dosSeg;
    }
    oldInt1C = _dos_getvect(0x1C);
    left = ticks ? ticks : 1;
    _dos_setvect(0x1C, shotIsr);
    armed = 1;
    return 0;
}

void shot_disarm(void)
{
    left = 0;                                /* inert from now, hooked or not */
    /* Put the old vector back only while the vector is still ours: a program
       that hooked it after us and stayed would otherwise be cut out. */
    if (armed == 1 && _dos_getvect(0x1C) == shotIsr)
        _dos_setvect(0x1C, oldInt1C);
}

const unsigned char __far *shot_frame(ShotInfo *info)
{
    const unsigned char __far *bda = (const unsigned char __far *)MK_FP(BIOS_SEG, 0);

    if (armed == 1 && taken) {
        info->how = SHOT_TIMED;
        info->mode = bios[0];
        info->modeReg = bios[1];
        info->colorReg = bios[2];
        if (dosSeg == 0)
            copySeg = emsSeg();              /* the program may have mapped its own page there */
        if (copySeg)
            return (const unsigned char __far *)MK_FP(copySeg, 0);
    }
    info->how = armed == 1 ? SHOT_LATE : armed < 0 ? SHOT_NOBUF : SHOT_AT_EXIT;
    info->mode = bda[0x49];
    info->modeReg = bda[0x65];
    info->colorReg = bda[0x66];
    return (const unsigned char __far *)MK_FP(VRAM_SEG, 0);
}

void shot_release(void)
{
    if (dosSeg)
        _dos_freemem(dosSeg);
    dosSeg = 0;
    copySeg = 0;
    armed = 0;
}
