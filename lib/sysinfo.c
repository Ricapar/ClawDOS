/* Claude-XT sysinfo tool: see sysinfo.h. */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "screen.h"
#include "sysinfo.h"

static unsigned ems_kb(void)
{
    static const char name[8] = { 'E', 'M', 'M', 'X', 'X', 'X', 'X', '0' };
    void (__interrupt __far *vec)() = _dos_getvect(0x67);
    const char __far *dev = (const char __far *)MK_FP(FP_SEG(vec), 10);
    union REGS r;
    int i;

    if (vec == 0)
        return 0;
    for (i = 0; i < 8; i++)
        if (dev[i] != name[i])
            return 0;
    r.h.ah = 0x42;                     /* get page counts */
    int86(0x67, &r, &r);
    return r.h.ah ? 0 : r.x.dx * 16;
}

int tool_sysinfo(ToolCtx *t)
{
    const char __far *biosDate = (const char __far *)MK_FP(0xF000, 0xFFF5);
    struct dosdate_t d;
    struct dostime_t tm;
    union REGS r;
    unsigned totalKb, freeKb, ems, drive;
    char date[9];
    int i;

    r.h.ah = 0x30;                     /* DOS version */
    intdos(&r, &r);
    tout_printf(t, "DOS %u.%02u\n", r.h.al, r.h.ah);

    int86(0x12, &r, &r);               /* conventional memory */
    totalKb = r.x.ax;
    r.h.ah = 0x48;                     /* largest free block, in paragraphs */
    r.x.bx = 0xFFFF;
    intdos(&r, &r);
    freeKb = r.x.bx / 64;
    tout_printf(t, "RAM %uK total, %uK largest free block\n", totalKb, freeKb);

    ems = ems_kb();
    if (ems)
        tout_printf(t, "EMS: %u KB\n", ems);
    else
        tout_puts(t, "EMS: none\n");

    for (drive = 3; drive <= 26; drive++) {     /* C: to Z:; A: and B: would seek */
        r.h.ah = 0x36;
        r.h.dl = (unsigned char)drive;
        intdos(&r, &r);
        if (r.x.ax == 0xFFFF)
            continue;
        tout_printf(t, "Drive %c: %lu KB free of %lu KB\n", 'A' + drive - 1,
                    (unsigned long)r.x.ax * r.x.cx * r.x.bx / 1024,
                    (unsigned long)r.x.ax * r.x.cx * r.x.dx / 1024);
    }

    for (i = 0; i < 8; i++)
        date[i] = biosDate[i];
    date[8] = 0;
    tout_printf(t, "BIOS date: %s\n", date);
    tout_printf(t, "Video: %s\n", scr_has_hwscroll() ? "CGA" : "EGA/VGA (CGA-compatible text)");

    _dos_getdate(&d);
    _dos_gettime(&tm);
    tout_printf(t, "Date: %04u-%02u-%02u %02u:%02u\n", d.year, d.month, d.day, tm.hour, tm.minute);

    sprintf(t->summary, "%uK RAM, %uK free", totalKb, freeKb);
    return TS_OK;
}
