/* CXTUNIT.EXE - unit tests for Claude-XT's hardware-free code (parsers).
 * Runs on DOS; image/tests/units.bat sends its output to COM1 and
 * scripts/boottest.py checks for UNITS-OK.
 */

#include <stdio.h>
#include <string.h>
#include "play.h"
#include "basdraw.h"
#include "raster.h"
#include "memory.h"
#include "edit.h"
#include "search.h"

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

/* --- basdraw ---------------------------------------------------------------- */

static int stmts;

#define REC (rec + strlen(rec))
static void dScreen(void *c, int m) { (void)c; sprintf(REC, "scr%d ", m); }
static void dColor(void *c, int bg, int pal) { (void)c; sprintf(REC, "col%d,%d ", bg, pal); }
static void dCls(void *c) { (void)c; strcat(rec, "cls "); }
static void dPset(void *c, int x, int y, int k) { (void)c; sprintf(REC, "pset%d,%d,%d ", x, y, k); }
static void dLine(void *c, int x1, int y1, int x2, int y2, int k)
{ (void)c; sprintf(REC, "line%d,%d,%d,%d,%d ", x1, y1, x2, y2, k); }
static void dBox(void *c, int x1, int y1, int x2, int y2, int k, int f)
{ (void)c; sprintf(REC, "box%d,%d,%d,%d,%d,%d ", x1, y1, x2, y2, k, f); }
static void dEll(void *c, int x, int y, int rx, int ry, int k)
{ (void)c; sprintf(REC, "ell%d,%d,%d,%d,%d ", x, y, rx, ry, k); }
static int dPaint(void *c, int x, int y, int k, int b)
{ (void)c; sprintf(REC, "paint%d,%d,%d,%d ", x, y, k, b); return 0; }
static void dText(void *c, int r, int col, const char *s, int k)
{ (void)c; sprintf(REC, "text%d,%d,%s,%d ", r, col, s, k); }
static void dSleep(void *c, int s) { (void)c; sprintf(REC, "sleep%d ", s); }
static int dStop(void *c) { (void)c; return stopAfter && ++stmts >= stopAfter; }
static int lines;
static void dCountLine(void *c, int x1, int y1, int x2, int y2, int k)
{ (void)c; (void)x1; (void)y1; (void)x2; (void)y2; (void)k; lines++; }

static const DrawOps recDraw = { 0, dScreen, dColor, dCls, dPset, dLine, dBox, dEll,
                                 dPaint, dText, dSleep, dStop };

static int draw(const char *script, char *err)
{
    rec[0] = 0;
    stmts = 0;
    err[0] = 0;
    return bas_draw(script, &recDraw, err, 80);
}

static void drawIs(const char *name, const char *script, const char *want)
{
    char err[80];
    int rc = draw(script, err);
    if (rc != 0) {
        CHECK_STR(name, err, "(no error)");
        return;
    }
    CHECK_STR(name, rec, want);
}

static void drawFails(const char *name, const char *script, const char *want)
{
    char err[80];
    int rc = draw(script, err);
    CHECK(name, rc == -1);
    CHECK_STR(name, err, want);
}

static void testDraw(void)
{
    char err[80];
    int rc;

    drawIs("draw: screen and pset", "SCREEN 1:PSET (10,20),2", "scr1 pset10,20,2 ");
    drawIs("draw: default colour 3", "PSET (1,1)", "pset1,1,3 ");
    drawIs("draw: screen 2 colour 1", "SCREEN 2\nPSET (1,1)", "scr2 pset1,1,1 ");
    drawIs("draw: preset", "PRESET (4,5)", "pset4,5,0 ");
    drawIs("draw: line and continuation", "LINE (0,0)-(10,10),1:LINE -(20,5)",
           "line0,0,10,10,1 line10,10,20,5,3 ");
    drawIs("draw: box fill", "LINE (0,0)-(5,5),2,BF", "box0,0,5,5,2,1 ");
    drawIs("draw: box default colour", "LINE (0,0)-(5,5),,B", "box0,0,5,5,3,0 ");
    drawIs("draw: step", "PSET (10,10):PSET STEP(5,-2)", "pset10,10,3 pset15,8,3 ");
    drawIs("draw: circle aspect", "CIRCLE (160,100),50,1", "ell160,100,50,42,1 ");
    drawIs("draw: circle given aspect", "CIRCLE (160,100),50,1,,,.5", "ell160,100,50,25,1 ");
    drawIs("draw: circle tall", "CIRCLE (160,100),50,1,,,2", "ell160,100,25,50,1 ");
    drawIs("draw: paint", "PAINT (5,5),1,3", "paint5,5,1,3 ");
    drawIs("draw: paint border default", "PAINT (5,5),2", "paint5,5,2,2 ");
    drawIs("draw: turtle", "DRAW \"BM10,10 R5 D5\"", "line10,10,15,10,3 line15,10,15,15,3 ");
    drawIs("draw: turtle scale", "DRAW \"S8 BM0,0 R5\"", "line0,0,10,0,3 ");
    drawIs("draw: turtle no-update", "DRAW \"BM0,0 NR5 D2\"", "line0,0,5,0,3 line0,0,0,2,3 ");
    drawIs("draw: turtle diagonal colour", "DRAW \"BM0,10 C2 E3 M+2,1 M20,20\"",
           "line0,10,3,7,2 line3,7,5,8,2 line5,8,20,20,2 ");
    drawIs("draw: turtle angle", "DRAW \"BM10,10 A1 R5\"", "line10,10,10,5,3 ");
    drawIs("draw: turtle any angle", "DRAW \"BM10,10 TA45 R10\"", "line10,10,17,3,3 ");
    drawIs("draw: expressions", "PSET (100+20, 50*2)", "pset120,100,3 ");
    drawIs("draw: parens and division", "PSET ((10+2)/4, -3+5)", "pset3,2,3 ");
    drawIs("draw: line numbers and comments", "10 REM hi\n20 PSET (1,1) ' x", "pset1,1,3 ");
    drawIs("draw: locate print", "LOCATE 2,3:PRINT \"HI\"", "text2,3,HI,3 ");
    drawIs("draw: print advances", "PRINT \"A\"\nPRINT \"B\";\nPRINT \"C\"",
           "text1,1,A,3 text2,1,B,3 text2,2,C,3 ");
    drawIs("draw: color cls sleep", "COLOR 1,0:CLS:SLEEP 2", "col1,0 cls sleep2 ");
    drawIs("draw: lowercase", "pset (1,2),1", "pset1,2,1 ");

    drawFails("draw: missing paren", "PSET 10,10", "line 1: PSET: expected (");
    drawFails("draw: unknown statement", "PSET (1,1)\nFOO", "line 2: FOO: unknown statement");
    CHECK_STR("draw: statements before an error run", rec, "pset1,1,3 ");
    drawFails("draw: bad screen", "SCREEN 7", "line 1: SCREEN: only SCREEN 1 or 2");
    drawFails("draw: bad turtle", "DRAW \"Q5\"", "line 1: DRAW: unknown command Q");

    {   /* the mock tool tour's script: 120 LINEs, then LOCATE/PRINT */
        static char script[4000];
        static const DrawOps countOps = { 0, dScreen, dColor, dCls, dPset, dCountLine, dBox,
                                          dEll, dPaint, dText, dSleep, dStop };
        int i, n = sprintf(script, "SCREEN 1\n");
        for (i = 0; i < 120; i++)
            n += sprintf(script + n, "LINE (%d,0)-(%d,199),%d\n", i * 2, 319 - i * 2, 1 + i % 3);
        strcat(script, "LOCATE 12,15\nPRINT \"TOUR\"\n");
        rec[0] = 0;
        lines = 0;
        rc = bas_draw(script, &countOps, err, 80);
        CHECK("draw: long script runs", rc == 0);
        CHECK("draw: long script lines", lines == 120);
        CHECK_STR("draw: long script ends", rec, "scr1 text12,15,TOUR,3 ");
    }

    stopAfter = 1;
    rc = draw("PSET (1,1)\nPSET (2,2)", err);
    stopAfter = 0;
    CHECK("draw: stop returns 1", rc == 1);
    CHECK_STR("draw: stop after one statement", rec, "pset1,1,3 ");
}

/* --- raster ------------------------------------------------------------------ */

#define BW 40
#define BH 30
static unsigned char bitmap[BH][BW];
static unsigned plots;
static int lastX, lastY;

static void bPlot(void *c, int x, int y, int k)
{
    (void)c;
    if (++plots > 2000)               /* a runaway line: stop counting, don't hang */
        return;
    if (x < BW && y < BH)
        bitmap[y][x] = (unsigned char)k;
    lastX = x;
    lastY = y;
}

static int bPoint(void *c, int x, int y)
{
    (void)c;
    return x < BW && y < BH ? bitmap[y][x] : 0;
}

static unsigned count(int k)
{
    unsigned n = 0;
    int x, y;
    for (y = 0; y < BH; y++)
        for (x = 0; x < BW; x++)
            n += bitmap[y][x] == k;
    return n;
}

static void testMemory(void)
{
    char out[MEM_SUMMARY + 1];

    CHECK("mem name plain", mem_name_ok("TRAINS"));
    CHECK("mem name lower", mem_name_ok("trains"));
    CHECK("mem name 8", mem_name_ok("ABCD_E-9"));
    CHECK("mem name empty", !mem_name_ok(""));
    CHECK("mem name 9", !mem_name_ok("ABCDEFGHI"));
    CHECK("mem name dot", !mem_name_ok("A.TXT"));
    CHECK("mem name path", !mem_name_ok("..\\X"));
    CHECK("mem name space", !mem_name_ok("A B"));

    mem_summary("First line\r\nsecond", 19, out);
    CHECK_STR("mem summary", out, "First line");
    mem_summary("\r\n\n   Indented\n", 15, out);
    CHECK_STR("mem summary skips blank", out, "Indented");
    mem_summary("", 0, out);
    CHECK_STR("mem summary empty", out, "");
    mem_summary("x\x1A tail", 7, out);
    CHECK_STR("mem summary eof", out, "x");
    mem_summary("0123456789012345678901234567890123456789012345678901234567890123456789", 70, out);
    CHECK_STR("mem summary cut", out, "012345678901234567890123456789012345678901234567890123456...");
}

static void testRaster(void)
{
    Raster big = { 0, bPlot, bPoint, NULL, 319, 199 };
    Raster small = { 0, bPlot, bPoint, NULL, BW - 1, BH - 1 };
    int rc;

    plots = 0;
    ras_line(&big, 0, 0, 319, 199, 1);
    CHECK("raster: long line terminates", plots == 320);
    CHECK("raster: long line ends at its endpoint", lastX == 319 && lastY == 199);
    plots = 0;
    ras_line(&big, 10, 50, 10, 40, 1);
    CHECK("raster: vertical line", plots == 11 && lastY == 40);
    plots = 0;
    ras_line(&big, -5, -5, 3, 3, 1);
    CHECK("raster: clipped line", plots == 4);

    plots = 0;                        /* far off-screen coordinates (16-bit overflow) */
    ras_line(&big, 0, 0, 20000, 5, 1);
    CHECK("raster: very long line terminates", plots == 320);
    plots = 0;
    ras_line(&big, -10000, 0, 10000, 50, 1);
    CHECK("raster: line across the screen from far away", plots > 0 && plots < 400);
    memset(bitmap, 0, sizeof(bitmap));
    plots = 0;
    ras_box(&small, -30000, 2, 32767, 3, 1, 1);
    CHECK("raster: huge filled box is clipped", plots == 2 * BW && count(1) == 2 * BW);

    memset(bitmap, 0, sizeof(bitmap));
    ras_box(&small, 5, 5, 20, 15, 2, 0);
    CHECK("raster: box outline", count(2) == 2 * 16 + 2 * 9);
    rc = ras_paint(&small, 10, 10, 3, 2);
    CHECK("raster: paint inside box", rc == 0 && count(3) == 14 * 9);
    rc = ras_paint(&small, 0, 0, 1, 2);
    CHECK("raster: paint outside box", rc == 0 && count(1) == BW * BH - 16 * 11);

    memset(bitmap, 0, sizeof(bitmap));
    plots = 0;
    ras_ellipse(&small, 20, 15, 10, 6, 1);
    CHECK("raster: ellipse extremes", bitmap[15][30] == 1 && bitmap[15][10] == 1
                                      && bitmap[9][20] == 1 && bitmap[21][20] == 1);
    CHECK("raster: ellipse terminates", plots < 200);
}

/* --- edit and search ------------------------------------------------------------ */

static char edited[300];

static int collect(void *ctx, const char *p, unsigned n)
{
    (void)ctx;
    strncat(edited, p, n);
    return 0;
}

static const char *replaced(const char *src, const char *old, const char *repl, int all)
{
    edited[0] = 0;
    edit_replace(src, old, repl, all, collect, NULL);
    return edited;
}

static void testEdit(void)
{
    const char *src = "int a;\nint b;\n    x = 1;\n    x = 1;\nend";
    unsigned first = 999;

    CHECK("edit count 2", edit_count(src, "x = 1;", &first) == 2);
    CHECK("edit count first", first == 18);
    CHECK("edit count none", edit_count(src, "y", &first) == 0 && first == 18);
    CHECK("edit count empty", edit_count(src, "", &first) == 0);
    CHECK("edit line of 0", edit_line_of(src, 0) == 1);
    CHECK("edit line of b", edit_line_of(src, 7) == 2);
    CHECK("edit line of x", edit_line_of(src, first) == 3);
    CHECK("edit count no overlap", edit_count("aaaa", "aa", &first) == 2 && first == 0);
    CHECK("edit lines", edit_lines("a\nb\n") == 2 && edit_lines("a\nb") == 2 && edit_lines("") == 0
                        && edit_lines("x") == 1 && edit_lines("\n") == 1);

    CHECK_STR("edit replace first", replaced(src, "x = 1;", "x = 2;", 0),
              "int a;\nint b;\n    x = 2;\n    x = 1;\nend");
    CHECK_STR("edit replace all", replaced(src, "x = 1;", "y", 1), "int a;\nint b;\n    y\n    y\nend");
    CHECK_STR("edit replace grows", replaced(src, "int b;\n", "int b;\nint c;\n", 0),
              "int a;\nint b;\nint c;\n    x = 1;\n    x = 1;\nend");
    CHECK_STR("edit delete", replaced(src, "int a;\n", "", 0), "int b;\n    x = 1;\n    x = 1;\nend");
    CHECK_STR("edit at end", replaced(src, "end", "fin\n", 0),
              "int a;\nint b;\n    x = 1;\n    x = 1;\nfin\n");
    CHECK_STR("edit whole", replaced("abc", "abc", "z", 0), "z");
    CHECK("edit replace empty old", edit_replace(src, "", "z", 0, collect, NULL) < 0);
}

static void testSearch(void)
{
    CHECK("search plain", search_match("    outportb(0x42, d);", "outportb", 0));
    CHECK("search any case", search_match("    OutPortB(0x42, d);", "outportb", 0));
    CHECK("search case", !search_match("    OutPortB(0x42, d);", "outportb", 1));
    CHECK("search case hit", search_match("    OutPortB(0x42, d);", "OutPortB", 1));
    CHECK("search at end", search_match("x & 63", "& 63", 0));
    CHECK("search too long", !search_match("abc", "abcd", 0));
    CHECK("search empty", !search_match("abc", "", 0));
    CHECK("search partial then hit", search_match("aab", "ab", 0));
}

int main(void)
{
    CHECK("sanity", 1 + 1 == 2);
    testPlay();
    testDraw();
    testRaster();
    testMemory();
    testEdit();
    testSearch();

    printf("%s %d\n", failed ? "UNITS-FAILED" : "UNITS-OK", failed ? failed : passed);
    return failed != 0;
}
