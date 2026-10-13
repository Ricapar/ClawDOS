/* Toolchain smoke test: prints a line and the BIOS equipment word. */
#include <stdio.h>
#include <i86.h>

int main(void)
{
    union REGS r;

    int86(0x11, &r, &r);
    printf("Claude-XT toolchain OK. Equipment word: %04X\n", r.x.ax);
    return 0;
}
