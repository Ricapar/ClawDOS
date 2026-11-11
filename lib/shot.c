/* ClawDOS screenshots, the capture: see shot.h. */

#include <stdlib.h>
#include <dos.h>
#include <i86.h>
#include "shot.h"
#include "shell.h"

#define BIOS_SEG   0x40
#define VRAM_SEG   0xB800

/* Changed by the timer hook: volatile, or the optimizer carries the values
   from before the run across it. */
static volatile unsigned left;              /* ticks to go; 0: nothing to do */
static volatile unsigned char taken;        /* the copy was made */
static volatile unsigned char bios[3];      /* mode, mode and colour shadows at that moment */
static unsigned copySeg;                    /* where the copy goes: a DOS block held for the run */
static int armed;                           /* 0, 1 armed, -1 asked but no buffer */
static unsigned pending;                    /* ticks for the hook shell_run's program start sets */
static int inChain;                         /* the hook is in INT 1Ch's chain (maybe under another) */
static int atexitSet;
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

/* shell_run's program is about to start: the count begins. The hook goes in
   only now, so it is not there while nothing of ours could take it out (and
   DOS ending us in the shell's own code would leave it pointing nowhere). */
static void start(void)
{
    if (pending == 0)
        return;
    left = pending;
    pending = 0;
    if (!inChain) {                          /* still there from a run whose unhook failed: not twice */
        oldInt1C = _dos_getvect(0x1C);
        _dos_setvect(0x1C, shotIsr);
        inChain = 1;
    }
    if (!atexitSet) {                        /* as lib/screen.c's ticker: never left in the chain at exit */
        atexit(shot_disarm);
        atexitSet = 1;
    }
}

int shot_arm(unsigned ticks)
{
    taken = 0;
    left = 0;
    /* Never an EMS page: the program may map pages of its own into the frame
       while it runs, and the copy would land on them. A block of ours is
       ours whatever the program does. */
    if (copySeg == 0 && _dos_allocmem(SHOT_BYTES / 16, &copySeg)) {
        copySeg = 0;                         /* the call leaves the largest free size there */
        armed = -1;
        return -1;
    }
    pending = ticks ? ticks : 1;
    shell_before_run = start;
    armed = 1;
    return 0;
}

void shot_disarm(void)
{
    left = 0;                                /* inert from now, hooked or not */
    pending = 0;
    /* Put the old vector back only while the vector is still ours: a program
       that hooked it after us and stayed would otherwise be cut out. Then we
       stay in the chain, inert, and the next arm does not hook again. */
    if (inChain && _dos_getvect(0x1C) == shotIsr) {
        _dos_setvect(0x1C, oldInt1C);
        inChain = 0;
    }
}

const unsigned char __far *shot_frame(ShotInfo *info)
{
    const unsigned char __far *bda = (const unsigned char __far *)MK_FP(BIOS_SEG, 0);

    if (armed == 1 && taken) {
        info->how = SHOT_TIMED;
        info->mode = bios[0];
        info->modeReg = bios[1];
        info->colorReg = bios[2];
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
    if (copySeg)
        _dos_freemem(copySeg);
    copySeg = 0;
    armed = 0;
}
