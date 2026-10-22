/* CXTUNIT.EXE - unit tests for Claude-XT's hardware-free code (parsers).
 * Runs on DOS; image/tests/units.bat sends its output to COM1 and
 * scripts/boottest.py checks for UNITS-OK.
 */

#include <stdio.h>
#include <string.h>
#include "play.h"

static int passed, failed;

#define CHECK(name, cond) check((name), (cond), "")
#define CHECK_STR(name, got, want) check_str((name), (got), (want))

static void check(const char *name, int ok, const char *detail)
{
    if (ok) {
        passed++;
        printf("PASS %s\n", name);
    } else {
        failed++;
        printf("FAIL %s%s\n", name, detail);
    }
}

static void check_str(const char *name, const char *got, const char *want)
{
    static char detail[400];
    if (strcmp(got, want) == 0) {
        check(name, 1, "");
    } else {
        sprintf(detail, ": got \"%.150s\" want \"%.150s\"", got, want);
        check(name, 0, detail);
    }
}

/* --- play ------------------------------------------------------------------- */

static char rec[600];
static int stopAfter;                 /* stop() returns 1 once this many tones played */
static int tones;

static void recTone(void *ctx, unsigned hz, unsigned ms)
{
    (void)ctx;
    sprintf(rec + strlen(rec), "%u/%u ", hz, ms);
    tones++;
}

static int recStop(void *ctx)
{
    (void)ctx;
    return stopAfter && tones >= stopAfter;
}

static const PlayOps recPlay = { 0, recTone, recStop };

/* Play `music` into rec; returns play_run's result, err holds any error. */
static int play(const char *music, char *err, unsigned long *total)
{
    unsigned long ms;
    rec[0] = 0;
    tones = 0;
    err[0] = 0;
    return play_run(music, &recPlay, total ? total : &ms, err, 80);
}

static void testPlay(void)
{
    char err[80];
    unsigned long total;
    int rc;

    play("T120 L4 O3 A", err, NULL);
    CHECK_STR("play: MN quarter A", rec, "440/437 0/63 ");
    play("ML O3 C", err, NULL);
    CHECK_STR("play: middle C legato", rec, "262/500 ");
    play("MLP4", err, NULL);
    CHECK_STR("play: rest", rec, "0/500 ");
    play("ML O3 C.", err, NULL);
    CHECK_STR("play: dotted", rec, "262/750 ");
    play("ML N46", err, NULL);
    CHECK_STR("play: note number", rec, "440/500 ");
    play("ML O3 C# C- ", err, NULL);
    CHECK_STR("play: sharp and flat", rec, "277/500 247/500 ");
    play("ML L8 C C16", err, NULL);
    CHECK_STR("play: lengths", rec, "523/250 523/125 ");
    play("ML O3 C > C < < C", err, NULL);
    CHECK_STR("play: octave shifts", rec, "262/500 523/500 131/500 ");
    play("MS T60 L4 C", err, NULL);
    CHECK_STR("play: staccato", rec, "523/750 0/250 ");

    rc = play("O9", err, NULL);
    CHECK("play: bad octave fails", rc == -1);
    CHECK_STR("play: bad octave message", err, "col 2: O out of range");
    rc = play("ML C X", err, NULL);
    CHECK("play: bad char fails", rc == -1);
    CHECK_STR("play: bad char message", err, "col 6: unexpected 'X'");

    stopAfter = 1;
    rc = play("ML CDE", err, NULL);
    stopAfter = 0;
    CHECK("play: stop returns 1", rc == 1);
    CHECK_STR("play: stop after one note", rec, "523/500 ");

    play("T120 L4 CDE", err, &total);
    CHECK("play: total ms", total == 1500);
}

int main(void)
{
    CHECK("sanity", 1 + 1 == 2);
    testPlay();

    printf("%s %d\n", failed ? "UNITS-FAILED" : "UNITS-OK", failed ? failed : passed);
    return failed != 0;
}
