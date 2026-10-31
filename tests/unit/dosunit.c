/* DOSUNIT.EXE - unit tests for ClawDOS's hardware-free code (parsers).
 * Runs on DOS; image/tests/units.bat sends its output to COM1 and
 * scripts/boottest.py checks for UNITS-OK.
 */

#include <stdio.h>
#include <string.h>
#include "play.h"
#include "memory.h"
#include "edit.h"
#include "search.h"
#include "screen.h"
#include "console.h"
#include "ticks.h"
#include "utf8.h"
#include "sink.h"
#include "json.h"
#include "chunked.h"
#include "sse.h"

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

/* --- memory ----------------------------------------------------------------- */

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

/* memory_list through the real executors: a full 8-character name (12 with
   .TXT, the longest find_t gives) is listed with its summary. */
static void testMemoryList(void)
{
    ToolCtx t;
    char *line;

    tout_reset(&t);
    t.unattended = 1;
    targ_reset();
    targ_append("name", "ABCDEFGH");
    targ_append("text", "Unit test memory\nsecond line");
    CHECK("mem write long name", tool_memory_write(&t) == TS_OK);

    tout_reset(&t);
    targ_reset();
    CHECK("mem list ok", tool_memory_list(&t) == TS_OK);
    line = strstr(t.out, "ABCDEFGH");
    CHECK("mem list has long name", line != NULL);
    if (line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        CHECK("mem list long name summary", strstr(line, "Unit test memory") != NULL);
        CHECK("mem list long name size", strstr(line, "   29  ") != NULL);
    }
    remove(MEM_DIR "\\ABCDEFGH.TXT");
}

/* --- bios ------------------------------------------------------------------- */

static void testTicks(void)
{
    CHECK("ticks: elapsed", ticks_elapsed(100, 40) == 60);
    CHECK("ticks: none", ticks_elapsed(40, 40) == 0);
    CHECK("ticks: across midnight", ticks_elapsed(5, TICKS_PER_DAY - 10) == 15);
    CHECK("ticks: a minute round-trips", TICKS_TO_SECS(SECS_TO_TICKS(60)) == 60);
    CHECK("ticks: 10 s is 182 ticks", SECS_TO_TICKS(10) == 182);
}

/* --- console ---------------------------------------------------------------- */

/* A stand-in for screen.c: a character grid with no footer, so console.c's
   wrapping can be checked without a CGA. */
static unsigned short grid[SCR_ROWS][SCR_COLS];

void scr_putc(int row, int col, unsigned char ch, unsigned char attr) { grid[row][col] = ch | (attr << 8); }
void scr_puts(int row, int col, const char *s, unsigned char attr)
{
    for (; *s; s++, col++)
        scr_putc(row, col, (unsigned char)*s, attr);
}
void scr_putn(int row, int col, const unsigned short *cells, int n) { memcpy(grid[row] + col, cells, n * 2); }
void scr_fill(int row, int col, int n, unsigned char ch, unsigned char attr)
{
    while (n-- > 0)
        scr_putc(row, col++, ch, attr);
}
void scr_clear(unsigned char attr)
{
    int r;
    for (r = 0; r < SCR_ROWS; r++)
        scr_fill(r, 0, SCR_COLS, ' ', attr);
}
void scr_scroll(unsigned char attr)
{
    memmove(grid[0], grid[1], sizeof(grid) - sizeof(grid[0]));
    scr_fill(SCR_ROWS - 1, 0, SCR_COLS, ' ', attr);
}
int  scr_footer_rows(void) { return 0; }
void scr_cursor(int row, int col) { (void)row; (void)col; }

/* The text on grid row r, trailing blanks removed. */
static const char *gridRow(int r)
{
    static char text[SCR_COLS + 1];
    int n = SCR_COLS;

    while (n > 0 && (grid[r][n - 1] & 0xFF) == ' ')
        n--;
    for (text[n] = 0; n--;)
        text[n] = (char)grid[r][n];
    return text;
}

static void testConsole(void)
{
    /* 77 columns of text (reply margins 2..78), the last of them a letter */
    static const char full[] = "abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd xy";

    CHECK("console: fixture is 77 wide", strlen(full) == 77);

    con_margins(2, SCR_COLS - 1);         /* set first: con_init starts at the left margin */
    con_init();
    con_write(full);
    con_write("\nnext");
    CHECK_STR("console: full line stays whole", gridRow(0) + 2, full);
    CHECK_STR("console: newline after a full line adds no blank row", gridRow(1), "  next");

    con_init();
    con_write(full);
    con_write(" more");
    CHECK_STR("console: space at the margin breaks there", gridRow(0) + 2, full);
    CHECK_STR("console: the word after it starts the next row", gridRow(1), "  more");

    con_init();
    con_write("abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcdefg");
    CHECK_STR("console: overflowing word wraps whole", gridRow(1), "  abcdefg");

    con_margins(2, SCR_COLS);             /* textart: 78 columns between margins 2 and 80 */
    con_init();
    con_write("12345678901234567890123456789012345678901234567890123456789012345678901234567\xDB\n"
              "second row");
    CHECK_STR("console: 78-column art row is not torn", gridRow(1), "  second row");
    con_margins(2, SCR_COLS - 1);
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

/* --- utf8 ------------------------------------------------------------------- */

static void testUtf8(void)
{
    char out[64]; unsigned n;
    n = utf8_to_cp437("caf\xC3\xA9 \xE2\x80\x94 \xE2\x80\x9Cok\xE2\x80\x9D", 18, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 accents and quotes", out, "caf\x82 -- \"ok\"");
    n = utf8_to_cp437("\xE2\x94\x80\xE2\x96\x88", 6, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 box drawing", out, "\xC4\xDB");
    n = utf8_to_cp437("\xC5\x81\xC3\xB8 \xF0\x9F\x98\x80", 9, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 fallbacks", out, "Lo ?");
    n = utf8_to_cp437("a\xE2\x80\x8B" "b\tc", 7, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 zero width and tab", out, "ab    c");
    n = utf8_to_cp437("\xC3", 1, out, sizeof out); out[n] = 0;
    CHECK_STR("utf8 truncated sequence", out, "?");
    n = utf8_to_cp437("abcdef", 6, out, 3);
    CHECK("utf8 respects outmax", n == 3);
    n = cp437_to_utf8(0x82, out); out[n] = 0;
    CHECK_STR("cp437 e-acute", out, "\xC3\xA9");
    n = cp437_to_utf8(0xC4, out); out[n] = 0;
    CHECK_STR("cp437 box", out, "\xE2\x94\x80");
    n = cp437_to_utf8('A', out); out[n] = 0;
    CHECK_STR("cp437 ascii", out, "A");
    n = cp437_to_utf8(0x07, out); out[n] = 0;
    CHECK_STR("cp437 bullet glyph", out, "\xE2\x80\xA2");
    n = cp437_to_utf8('\n', out);
    CHECK("cp437 newline stays", n == 1 && out[0] == '\n');
}

/* --- json ------------------------------------------------------------------- */

static const char EV[] = "{\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hi \\\"there\\\" \\u00e9\\n\"},\"n\":[1,{\"x\":2}],\"ok\":true}";
static int eachCount; static char eachKeys[64];
static int eachFn(void *c, const char *k, unsigned kl, const char *v, unsigned vl, int t)
{ (void)c; (void)v; (void)vl; eachCount++; sprintf(eachKeys + strlen(eachKeys), "%.*s=%d ", kl, k, t); return 0; }

static void testJson(void)
{
    const char *v; unsigned n; char out[64]; Sink s;
    CHECK("json find string", json_find(EV, sizeof EV - 1, "delta.text", &v, &n) == JSON_STRING
          && n == 21 && !memcmp(v, "Hi \\\"there\\\" \\u00e9\\n", 21));
    CHECK("json find number", json_find(EV, sizeof EV - 1, "index", &v, &n) == JSON_NUMBER && n == 1 && *v == '0');
    CHECK("json find true", json_find(EV, sizeof EV - 1, "ok", &v, &n) == JSON_TRUE);
    CHECK("json find array", json_find(EV, sizeof EV - 1, "n", &v, &n) == JSON_ARRAY && n == 11);
    CHECK("json find missing", json_find(EV, sizeof EV - 1, "delta.nope", &v, &n) == JSON_NONE);
    CHECK("json find skips nested", json_find(EV, sizeof EV - 1, "x", &v, &n) == JSON_NONE);
    n = json_unescape("Hi \\\"there\\\" \\u00e9\\n", 21, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape", out, "Hi \"there\" \xC3\xA9\n");
    n = json_unescape("\\ud83d\\ude00!", 13, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape surrogate pair", out, "\xF0\x9F\x98\x80!");
    n = json_unescape("a\\ud83d", 7, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape lone surrogate", out, "a?");
    n = json_unescape("\\/\\\\\\t", 6, out, sizeof out); out[n] = 0;
    CHECK_STR("json unescape slash backslash tab", out, "/\\\t");
    sink_buffer(&s, out, sizeof out);
    json_escape_cp437(&s, "say \"hi\"\n\x82\xC4\x01", 12);
    CHECK_STR("json escape cp437", out, "say \\\"hi\\\"\\n\xC3\xA9\xE2\x94\x80\xE2\x98\xBA");
    CHECK("json escape count", s.count == 20);
    eachCount = 0; eachKeys[0] = 0;
    json_each(EV, sizeof EV - 1, eachFn, NULL);
    CHECK_STR("json each keys", eachKeys, "type=1 index=2 delta=3 n=4 ok=5 ");
    CHECK("json find in truncated doc", json_find(EV, 20, "delta.text", &v, &n) == JSON_NONE);
}

/* --- chunked ------------------------------------------------------------------ */

static char collected[200]; static unsigned collectedLen;
static int collectBytes(void *ctx, const char *p, unsigned n)
{ (void)ctx; if (collectedLen + n < sizeof collected) { memcpy(collected + collectedLen, p, n); collectedLen += n; collected[collectedLen] = 0; } return 0; }

static void testChunked(void)
{
    static const char body[] = "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    unsigned total = sizeof body - 1, split;
    Chunked c;
    int rc, allOk = 1;

    chunked_init(&c);
    collectedLen = 0; collected[0] = 0;
    rc = chunked_feed(&c, body, total, collectBytes, NULL);
    CHECK("chunked whole", rc == 1);
    CHECK_STR("chunked whole text", collected, "hello world");
    for (split = 1; split < total; split++) {
        chunked_init(&c);
        collectedLen = 0; collected[0] = 0;
        rc = chunked_feed(&c, body, split, collectBytes, NULL);
        if (rc != 0) { allOk = 0; break; }
        rc = chunked_feed(&c, body + split, total - split, collectBytes, NULL);
        if (rc != 1 || strcmp(collected, "hello world")) { allOk = 0; break; }
    }
    CHECK("chunked every split", allOk);
    chunked_init(&c);
    collectedLen = 0; collected[0] = 0;
    rc = chunked_feed(&c, "5;ext=1\r\nhello\r\n0\r\n\r\n", 21, collectBytes, NULL);
    CHECK("chunked extension", rc == 1 && !strcmp(collected, "hello"));
    chunked_init(&c);
    CHECK("chunked bad size", chunked_feed(&c, "zz\r\n", 4, collectBytes, NULL) == -1);
}

/* --- sse ---------------------------------------------------------------------- */

static char events[300];
static void onEvent(void *ctx, const char *ev, const char *data, unsigned n)
{ (void)ctx; sprintf(events + strlen(events), "%s:%.*s|", ev, n, data); }

static void testSse(void)
{
    static const char feed[] = "event: a\ndata: {\"x\":1}\n\n: ping\n\ndata: l1\ndata: l2\n\n";
    static const char feedCrlf[] = "event: a\r\ndata: {\"x\":1}\r\n\r\ndata: l1\r\ndata: l2\r\n\r\n";
    char buf[64];
    char big[80];
    unsigned i, total = sizeof feed - 1;
    Sse s;
    int rc;

    sse_init(&s, buf, sizeof buf);
    events[0] = 0;
    rc = sse_feed(&s, feed, total, onEvent, NULL);
    CHECK("sse whole", rc == 0);
    CHECK_STR("sse whole events", events, "a:{\"x\":1}|:l1\nl2|");
    sse_init(&s, buf, sizeof buf);
    events[0] = 0;
    for (i = 0; i < total; i++)
        sse_feed(&s, feed + i, 1, onEvent, NULL);
    CHECK_STR("sse byte by byte", events, "a:{\"x\":1}|:l1\nl2|");
    sse_init(&s, buf, sizeof buf);
    events[0] = 0;
    sse_feed(&s, feedCrlf, sizeof feedCrlf - 1, onEvent, NULL);
    CHECK_STR("sse crlf", events, "a:{\"x\":1}|:l1\nl2|");
    sse_init(&s, buf, sizeof buf);
    events[0] = 0;
    memset(big, 'x', sizeof big);
    rc = sse_feed(&s, "data: ", 6, onEvent, NULL);
    rc |= sse_feed(&s, big, sizeof big, onEvent, NULL);         /* 80 > 64 */
    CHECK("sse overflow reported", rc == -1 && s.overflow);
    sse_feed(&s, "\n\ndata: ok\n\n", 12, onEvent, NULL);
    CHECK_STR("sse after overflow", events, ":ok|");
}

int main(void)
{
    CHECK("sanity", 1 + 1 == 2);
    testPlay();
    testMemory();
    testMemoryList();
    testTicks();
    testConsole();
    testEdit();
    testSearch();
    testUtf8();
    testJson();
    testChunked();
    testSse();

    printf("%s %d\n", failed ? "UNITS-FAILED" : "UNITS-OK", failed ? failed : passed);
    return failed != 0;
}
