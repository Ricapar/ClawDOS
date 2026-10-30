/* ClawDOS sound: see sound.h.
 *
 * Tones come from PIT channel 2 gated onto the speaker through port 61h.
 * Timing reads PIT channel 0, which the BIOS runs in mode 3 with a divisor
 * of 65536: the count drops by 2 per 0.838 us and wraps twice per 55 ms
 * timer tick, so summing the drops between frequent reads gives ~2386 units
 * per millisecond, much finer than the 55 ms BIOS tick.
 */

#include <stdio.h>
#include <conio.h>
#include "play.h"
#include "sound.h"

#define PIT_HZ        1193182ul
#define UNITS_PER_MS  2386ul

typedef struct {
    int stopped;                      /* Esc seen */
} Speaker;

static unsigned pit0(void)
{
    unsigned lo, hi;
    outp(0x43, 0x00);                 /* latch channel 0 */
    lo = inp(0x40);
    hi = inp(0x40);
    return lo | (hi << 8);
}

static void speaker_off(void)
{
    outp(0x61, inp(0x61) & ~3);
}

/* Busy-wait `ms`, watching for Esc. */
static void wait_ms(Speaker *s, unsigned ms)
{
    unsigned long want = (unsigned long)ms * UNITS_PER_MS, got = 0;
    unsigned prev = pit0(), polls = 0;

    while (got < want) {
        unsigned now = pit0();
        got += (unsigned)(prev - now);
        prev = now;
        if (++polls == 200) {         /* the keyboard check is slow; not every poll */
            polls = 0;
            if (tool_esc()) {
                s->stopped = 1;
                return;
            }
        }
    }
}

static void tone(void *ctx, unsigned hz, unsigned ms)
{
    Speaker *s = (Speaker *)ctx;

    if (s->stopped)
        return;
    if (hz) {
        unsigned long div = PIT_HZ / hz;
        if (div > 65535ul)
            div = 65535ul;
        outp(0x43, 0xB6);             /* channel 2, lo/hi byte, mode 3 */
        outp(0x42, (int)(div & 0xFF));
        outp(0x42, (int)(div >> 8));
        outp(0x61, inp(0x61) | 3);
    } else {
        speaker_off();
    }
    wait_ms(s, ms);
}

static int stopped(void *ctx)
{
    return ((Speaker *)ctx)->stopped;
}

int tool_play(ToolCtx *t)
{
    const char *music = targ("music");
    Speaker s;
    PlayOps ops;
    unsigned long total;
    char err[80];
    int rc;

    if (music == NULL)
        return tool_fail(t, "missing music");
    s.stopped = tool_esc();           /* forget keys typed earlier, but honour Esc */
    ops.ctx = &s;
    ops.tone = tone;
    ops.stop = stopped;
    rc = play_run(music, &ops, &total, err, sizeof(err));
    speaker_off();
    if (rc < 0)
        return tool_fail(t, err);
    if (rc > 0) {
        sprintf(t->summary, "stopped by user");
        tout_puts(t, "stopped by user\n");
        return TS_STOPPED;
    }
    sprintf(t->summary, "played %lu.%lus", total / 1000, total / 100 % 10);
    tout_printf(t, "played %lu.%lu seconds of music\n", total / 1000, total / 100 % 10);
    return TS_OK;
}
