/* Claude-XT tick-counter helpers: see ticks.h. */

#include "ticks.h"

unsigned long ticks_elapsed(unsigned long now, unsigned long start)
{
    if (now < start)                  /* the counter restarted at midnight */
        now += TICKS_PER_DAY;
    return now - start;
}
