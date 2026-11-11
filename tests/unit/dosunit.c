/* DOSUNIT.EXE - unit tests for ClawDOS's hardware-free code (parsers, and the
 * file tools on files the tests make and remove).
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
#include "httpp.h"
#include "cfg.h"
#include "history.h"
#include "apireq.h"
#include "apistream.h"
#include "convo.h"
#include "dosfs.h"
#include <stdlib.h>
#include <direct.h>

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
    play("ML\tO3 C\t", err, NULL);
    CHECK_STR("play: tabs between commands", rec, "262/500 ");
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
    targ_reset();
    targ_set("name", "ABCDEFGH", 8);
    targ_set("text", "Unit test memory\nsecond line", 28);
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

/* The memory manager's calls (F4): scan sorted by name, load, delete. */
static int memIndex(const MemEntry *e, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(e[i].name, name))
            return i;
    return -1;
}

static void testMemoryManage(void)
{
    static MemEntry e[32];
    static char text[64];
    ToolCtx t;
    int n, a, b, cut;

    tout_reset(&t);
    targ_reset();
    targ_set("name", "ZZUNITB", 7);
    targ_set("text", "Second unit memory\nmore", 24);
    CHECK("memui write b", tool_memory_write(&t) == TS_OK);
    tout_reset(&t);
    targ_reset();
    targ_set("name", "ZZUNITA", 7);
    targ_set("text", "First unit memory\nline two", 27);
    CHECK("memui write a", tool_memory_write(&t) == TS_OK);

    n = mem_scan(e, 32);
    a = memIndex(e, n, "ZZUNITA");
    b = memIndex(e, n, "ZZUNITB");
    CHECK("memui scan finds both", a >= 0 && b >= 0);
    CHECK("memui scan sorted", a >= 0 && b == a + 1);
    if (a >= 0) {
        CHECK("memui scan summary", !strcmp(e[a].summary, "First unit memory"));
        CHECK("memui scan size", e[a].size == 27);
    }
    CHECK("memui scan max 1 counts all", mem_scan(e, 1) == n && n >= 2);
    CHECK("memui scan max 1 keeps first", n < 2 || strcmp(e[0].name, "ZZUNITA") <= 0);

    CHECK("memui load", mem_load("ZZUNITA", text, sizeof(text), &cut) == 26 && !cut);
    CHECK("memui load drops CR", !strcmp(text, "First unit memory\nline two"));
    CHECK("memui load cut", mem_load("ZZUNITA", text, 6, &cut) == 5 && cut && !strcmp(text, "First"));
    CHECK("memui load missing", mem_load("ZZNONE", text, sizeof(text), &cut) == -1);
    CHECK("memui load bad name", mem_load("..\\X", text, sizeof(text), &cut) == -1);

    CHECK("memui delete", mem_delete("ZZUNITA") == 0);
    CHECK("memui delete again fails", mem_delete("ZZUNITA") != 0);
    CHECK("memui delete bad name", mem_delete("*") != 0);
    n = mem_scan(e, 32);
    CHECK("memui gone after delete", memIndex(e, n, "ZZUNITA") < 0 && memIndex(e, n, "ZZUNITB") >= 0);
    mem_delete("ZZUNITB");
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

    con_init();
    con_write("a\tb");
    CHECK_STR("console: a tab is drawn as spaces", gridRow(0), "  a    b");
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

/* --- http head ---------------------------------------------------------------- */

static void testHttpHead(void)
{
    HttpHead h;
    CHECK("head ok", http_parse_head("HTTP/1.1 200 OK\r\nDate: Fri, 31 Oct 2026 14:03:07 GMT\r\n"
                                     "transfer-encoding: Chunked\r\nConnection: keep-alive\r\n\r\n", &h) == 0);
    CHECK("head 200 chunked", h.status == 200 && h.chunked && h.contentLength == -1 && !h.close && h.retryAfter == 0);
    CHECK("head 529", http_parse_head("HTTP/1.1 529 Overloaded\r\nContent-Length: 87\r\nretry-after: 12\r\n\r\n", &h) == 0
          && h.status == 529 && h.contentLength == 87 && !h.chunked && h.retryAfter == 12);
    CHECK("head close", http_parse_head("HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n", &h) == 0 && h.close);
    CHECK("head no blank line", http_parse_head("HTTP/1.1 200 OK\r\nX: y\r\n", &h) == -1);
    CHECK("head bad status", http_parse_head("HTTP/1.1 abc\r\n\r\n", &h) == -1);
}

/* --- cfg ---------------------------------------------------------------------- */

static const char CFG_TEXT[] =
    "# ClawDOS settings\r\n"
    "provider anthropic\r\n"
    "theme claude   \r\n"
    "timezone -5\r\n"
    "name Claude\r\n"
    "\r\n"
    "[anthropic]\r\n"
    "type anthropic\r\n"
    "tunnel 192.168.1.10:8443\r\n"
    "host api.anthropic.com\r\n"
    "key sk-ant-abc#def\r\n"
    "model claude-opus-5-5\r\n"
    "effort low\r\n"
    "\r\n"
    "[openrouter]\r\n"
    "type anthropic\r\n"
    "tunnel 192.168.1.10:8444\r\n"
    "host openrouter.ai\r\n"
    "path /api/v1/messages\r\n"
    "auth bearer\r\n"
    "key sk-or-x\r\n"
    "model anthropic/claude-opus-5.5\r\n"
    "maxtokens 32000\r\n"
    "cache 0\r\n";

static void testCfg(void)
{
    Cfg *c = (Cfg *)malloc(sizeof(Cfg));
    Provider *p;
    char text[400];
    int i;

    cfg_defaults(c);
    CHECK("cfg parse ok", cfg_parse(c, CFG_TEXT) == 0);
    CHECK("cfg two providers", c->nproviders == 2);
    CHECK_STR("cfg startup provider", c->provider, "anthropic");
    p = cfg_provider(c, "ANTHROPIC");
    CHECK("cfg provider case", p != NULL);
    if (p) {
        CHECK_STR("cfg default path", p->path, "/v1/messages");
        CHECK_STR("cfg default auth", p->auth, "x-api-key");
        CHECK_STR("cfg key keeps hash", p->key, "sk-ant-abc#def");
        CHECK("cfg default cache", p->cache == 1);
        CHECK("cfg default maxtokens", p->maxTokens == 64000L);
        CHECK_STR("cfg effort", p->effort, "low");
        CHECK("cfg nothing missing", cfg_missing(p) == NULL);
    }
    p = cfg_provider(c, "openrouter");
    CHECK("cfg openrouter", p != NULL && !strcmp(p->auth, "bearer") && p->cache == 0
          && !strcmp(p->path, "/api/v1/messages") && p->maxTokens == 32000L);
    CHECK("cfg unknown provider", cfg_provider(c, "nope") == NULL);
    cfg_defaults(c);
    CHECK("cfg missing tunnel", cfg_parse(c, "[x]\ntype anthropic\nhost h\nkey k\nmodel m\n") == 0
          && !strcmp(cfg_missing(&c->providers[0]), "tunnel"));
    cfg_defaults(c);
    CHECK("cfg bad global key line", cfg_parse(c, "name x\nbogus 1\n") == 2);
    cfg_defaults(c);
    CHECK("cfg bad maxtokens", cfg_parse(c, "[x]\nmaxtokens 64k\n") == 2
          && cfg_parse(c, "[y]\nmaxtokens 0\n") == 2 && cfg_parse(c, "[z]\nmaxtokens 128000\n") == 0
          && c->providers[2].maxTokens == 128000L);
    cfg_defaults(c);
    text[0] = 0;
    for (i = 0; i < 9; i++)
        sprintf(text + strlen(text), "[p%d]\n", i);
    CHECK("cfg ninth section", cfg_parse(c, text) == 9);
    cfg_defaults(c);
    CHECK("cfg defaults", !strcmp(c->name, "Claude") && c->nproviders == 0);
    CHECK("cfg old keys ignored", cfg_parse(c, "clock 0\ntimezone -5\ntheme x\n") == 0);
    free(c);
}

/* --- history ------------------------------------------------------------------ */

static const char *histText(void)
{
    static char buf[1200];
    Sink s;
    sink_buffer(&s, buf, sizeof buf);
    hist_copy(&s);
    return buf;
}

static void testHistory(void)
{
    FILE *f;
    remove("C:\\HISTTEST.JSN");
    CHECK("hist open new", hist_open("C:\\HISTTEST.JSN") == 0);
    hist_puts("{\"role\":\"user\",\"content\":\"a\"}");
    hist_message_done();
    hist_puts("{\"role\":\"assist");
    CHECK("hist rollback message", hist_rollback_message() == 0);
    CHECK_STR("hist after rollback", histText(), "{\"role\":\"user\",\"content\":\"a\"}");
    hist_puts("{\"role\":\"assistant\",\"content\":[]}");
    hist_message_done();
    CHECK_STR("hist two messages", histText(),
              "{\"role\":\"user\",\"content\":\"a\"},\r\n{\"role\":\"assistant\",\"content\":[]}");
    CHECK("hist size", hist_size() == (long)strlen(histText()));
    hist_turn_begin();
    hist_puts("{\"role\":\"user\",\"content\":\"b\"}");
    hist_message_done();
    hist_puts("{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"}\"}]}");
    hist_message_done();
    CHECK("hist turn messages", hist_turn_messages() == 2);
    CHECK("hist rollback turn", hist_rollback_turn() == 0 && hist_turn_messages() == 0);
    CHECK_STR("hist after turn rollback", histText(),
              "{\"role\":\"user\",\"content\":\"a\"},\r\n{\"role\":\"assistant\",\"content\":[]}");
    hist_close();
    f = fopen("C:\\HISTTEST.JSN", "ab");
    fputs(",\r\n{\"role\":\"us", f);
    fclose(f);
    CHECK("hist reopen", hist_open("C:\\HISTTEST.JSN") == 0);
    CHECK_STR("hist reopen drops the half message", histText(),
              "{\"role\":\"user\",\"content\":\"a\"},\r\n{\"role\":\"assistant\",\"content\":[]}");
    hist_puts("{\"role\":\"user\",\"content\":\"c\"}");
    hist_message_done();
    CHECK_STR("hist append after reopen", histText(),
              "{\"role\":\"user\",\"content\":\"a\"},\r\n{\"role\":\"assistant\",\"content\":[]},\r\n"
              "{\"role\":\"user\",\"content\":\"c\"}");
    hist_turn_begin();
    hist_reopen_last(1);
    hist_puts(",\"x\":1}");
    hist_message_done();
    CHECK_STR("hist reopen last message", histText(),
              "{\"role\":\"user\",\"content\":\"a\"},\r\n{\"role\":\"assistant\",\"content\":[]},\r\n"
              "{\"role\":\"user\",\"content\":\"c\",\"x\":1}");
    CHECK("hist rollback after reopen restores the message", hist_rollback_turn() == 0
          && !strcmp(histText() + strlen(histText()) - 29, "{\"role\":\"user\",\"content\":\"c\"}"));
    hist_close();
    remove("C:\\HISTTEST.JSN");
}

/* --- api request -------------------------------------------------------------- */

static void writeFile(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

static void testApiReq(void)
{
    static char body[900], head[600];
    Provider *p = (Provider *)malloc(sizeof(Provider));
    ApiReq r;
    Sink count, buf;
    static const char want[] =
        "{\"model\":\"claude-opus-5-5\",\"max_tokens\":64000,\"stream\":true,"
        "\"output_config\":{\"effort\":\"low\"},\"cache_control\":{\"type\":\"ephemeral\"},"
        "\"tool_choice\":{\"type\":\"auto\",\"disable_parallel_tool_use\":true},"
        "\"system\":[{\"type\":\"text\",\"text\":\"Hi Claude.\\nMem:\\n(none)\",\"cache_control\":{\"type\":\"ephemeral\"}}],"
        "\"tools\":[{\"name\":\"t\"}],\"messages\":[";

    writeFile("C:\\PTEST.TXT", "Hi {name}.\r\nMem:\r\n{memories}");
    writeFile("C:\\TTEST.JSN", "[{\"name\":\"t\"}]");
    remove("C:\\HISTTEST.JSN");
    hist_open("C:\\HISTTEST.JSN");
    hist_puts("{\"role\":\"user\",\"content\":\"a\"}");
    hist_message_done();

    memset(p, 0, sizeof *p);
    strcpy(p->host, "api.anthropic.com"); strcpy(p->path, "/v1/messages"); strcpy(p->auth, "x-api-key");
    strcpy(p->key, "sk-ant-abc"); strcpy(p->model, "claude-opus-5-5"); strcpy(p->effort, "low"); p->cache = 1;
    p->maxTokens = 64000L;
    r.p = p; r.promptPath = "C:\\PTEST.TXT"; r.toolsPath = "C:\\TTEST.JSN"; r.name = "Claude";
    r.memories = ""; r.noTools = 0;

    sink_count(&count);
    CHECK("req count ok", api_body(&count, &r) == 0);
    sink_buffer(&buf, body, sizeof body);
    CHECK("req write ok", api_body(&buf, &r) == 0);
    CHECK("req count matches", count.count == strlen(body));
    CHECK("req prefix", !strncmp(body, want, sizeof want - 1));
    CHECK_STR("req tail", body + strlen(body) - 31, "{\"role\":\"user\",\"content\":\"a\"}]}");

    p->maxTokens = 128000L;
    sink_buffer(&buf, body, sizeof body);
    api_body(&buf, &r);
    CHECK("req maxtokens", strstr(body, "\"max_tokens\":128000,\"stream\"") != NULL);
    p->effort[0] = 0;
    sink_buffer(&buf, body, sizeof body);
    api_body(&buf, &r);
    CHECK("req no effort", strstr(body, "output_config") == NULL);
    p->cache = 0;
    sink_buffer(&buf, body, sizeof body);
    api_body(&buf, &r);
    CHECK("req no cache", strstr(body, "cache_control") == NULL);
    r.noTools = 1;
    sink_buffer(&buf, body, sizeof body);
    api_body(&buf, &r);
    CHECK("req tool_choice none", strstr(body, "\"tool_choice\":{\"type\":\"none\"}") != NULL);
    r.toolsPath = "C:\\NOPE.JSN";
    sink_buffer(&buf, body, sizeof body);
    CHECK("req missing file", api_body(&buf, &r) == -1);

    sink_buffer(&buf, head, sizeof head);
    api_head(&buf, p, 123);
    CHECK("head request line", !strncmp(head, "POST /v1/messages HTTP/1.1\r\n", 28));
    CHECK("head fields", strstr(head, "Host: api.anthropic.com\r\n") && strstr(head, "x-api-key: sk-ant-abc\r\n")
          && strstr(head, "anthropic-version: 2023-06-01\r\n") && strstr(head, "Content-Length: 123\r\n")
          && strstr(head, "Accept: text/event-stream\r\n") && strstr(head, "Connection: keep-alive\r\n")
          && strstr(head, "Content-Type: application/json\r\n") && !strstr(head, "anthropic-beta")
          && !strcmp(head + strlen(head) - 4, "\r\n\r\n"));
    strcpy(p->auth, "bearer"); strcpy(p->key, "sk-or-x"); strcpy(p->betas, "b-2026");
    sink_buffer(&buf, head, sizeof head);
    api_head(&buf, p, 1);
    CHECK("head bearer and betas", strstr(head, "Authorization: Bearer sk-or-x\r\n") && !strstr(head, "x-api-key")
          && strstr(head, "anthropic-beta: b-2026\r\n"));
    hist_close();
    remove("C:\\HISTTEST.JSN"); remove("C:\\PTEST.TXT"); remove("C:\\TTEST.JSN");
    free(p);
}

/* --- api stream --------------------------------------------------------------- */

static char asText[800], asHist[3000]; static int asThinking;
static void asTextFn(void *c, const char *t, unsigned n) { (void)c; strncat(asText, t, n); }
static void asThinkFn(void *c) { (void)c; asThinking++; }
static int asHistFn(void *c, const char *p, unsigned n) { (void)c; strncat(asHist, p, n); return 0; }

static int ev(ApiStream *a, const char *event, const char *data)
{
    return apistream_event(a, event, data, strlen(data));
}

static void asReset(ApiStream *a, char *inputBuf, unsigned max)
{
    asText[0] = asHist[0] = 0; asThinking = 0;
    apistream_init(a, inputBuf, max);
    a->text = asTextFn; a->thinking = asThinkFn; a->hist = asHistFn; a->ctx = NULL;
}

static void testApiStream(void)
{
    static char input[64];
    ApiStream a;
    int rc;

    asReset(&a, input, sizeof input);
    rc = ev(&a, "message_start", "{\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"model\":\"claude-opus-5-5\","
        "\"content\":[],\"usage\":{\"input_tokens\":100,\"cache_creation_input_tokens\":0,\"cache_read_input_tokens\":50,\"output_tokens\":1}}}");
    rc |= ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\",\"signature\":\"\"}}");
    rc |= ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"Let me \"}}");
    rc |= ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"think.\"}}");
    rc |= ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"abc\"}}");
    rc |= ev(&a, "content_block_stop", "{\"type\":\"content_block_stop\",\"index\":0}");
    rc |= ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}");
    rc |= ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hi \\u00e9\"}}");
    rc |= ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\"!\"}}");
    rc |= ev(&a, "content_block_stop", "{\"type\":\"content_block_stop\",\"index\":1}");
    rc |= ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":2,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"read\",\"input\":{}}}");
    rc |= ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\":\"}}");
    rc |= ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"C:\\\\\\\\A.TXT\\\"}\"}}");
    rc |= ev(&a, "content_block_stop", "{\"type\":\"content_block_stop\",\"index\":2}");
    rc |= ev(&a, "ping", "{\"type\":\"ping\"}");
    rc |= ev(&a, "message_delta", "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":7}}");
    CHECK("stream events ok so far", rc == 0);
    CHECK("stream message_stop", ev(&a, "message_stop", "{\"type\":\"message_stop\"}") == 1);
    CHECK_STR("stream text", asText, "Hi \x82!");
    CHECK("stream thinking twice", asThinking == 2);
    CHECK_STR("stream history", asHist,
        "{\"role\":\"assistant\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"Let me think.\",\"signature\":\"abc\"},"
        "{\"type\":\"text\",\"text\":\"Hi \\u00e9!\"},"
        "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"read\",\"input\":{\"path\":\"C:\\\\A.TXT\"}}]}");
    CHECK("stream tool", a.hasTool && !strcmp(a.toolId, "toolu_1") && !strcmp(a.toolName, "read"));
    CHECK_STR("stream tool input", a.toolInput, "{\"path\":\"C:\\\\A.TXT\"}");
    CHECK("stream stop reason", !strcmp(a.stopReason, "tool_use") && !strcmp(a.model, "claude-opus-5-5"));
    CHECK("stream usage", a.inTok == 100 && a.outTok == 7 && a.cacheRead == 50 && a.cacheWrite == 0);

    asReset(&a, input, sizeof input);
    ev(&a, "message_start", "{\"type\":\"message_start\",\"message\":{\"model\":\"m\",\"usage\":{\"input_tokens\":1}}}");
    ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}");
    ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"\\ud83d\"}}");
    ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"\\ude00\"}}");
    ev(&a, "content_block_stop", "{\"type\":\"content_block_stop\",\"index\":0}");
    ev(&a, "message_delta", "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":2}}");
    ev(&a, "message_stop", "{\"type\":\"message_stop\"}");
    CHECK_STR("stream split surrogate on screen", asText, "??");
    CHECK("stream split surrogate in history", strstr(asHist, "\"text\":\"\\ud83d\\ude00\"") != NULL);
    CHECK("stream no tool", !a.hasTool && !strcmp(a.stopReason, "end_turn"));

    asReset(&a, input, sizeof input);
    CHECK("stream error event", ev(&a, "error", "{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}") == -1
          && !strcmp(a.error, "Overloaded") && !strcmp(a.errorType, "overloaded_error"));

    asReset(&a, input, sizeof input);
    ev(&a, "message_start", "{\"type\":\"message_start\",\"message\":{\"model\":\"m\",\"usage\":{\"input_tokens\":1}}}");
    CHECK("stream unknown block", ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"fallback\"}}") == -1
          && strstr(a.error, "fallback") != NULL);

    asReset(&a, input, 16);
    ev(&a, "message_start", "{\"type\":\"message_start\",\"message\":{\"model\":\"m\",\"usage\":{\"input_tokens\":1}}}");
    ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"t2\",\"name\":\"write\",\"input\":{}}}");
    ev(&a, "content_block_delta", "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"text\\\":\\\"0123456789abcdef\\\"}\"}}");
    ev(&a, "content_block_stop", "{\"type\":\"content_block_stop\",\"index\":0}");
    ev(&a, "message_delta", "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},\"usage\":{\"output_tokens\":2}}");
    CHECK("stream input overflow", ev(&a, "message_stop", "{\"type\":\"message_stop\"}") == 1 && a.toolInputOverflow
          && strstr(asHist, "\"input\":{\"text\":\"0123456789abcdef\"}}]}") != NULL);
}

/* --- tool arguments ----------------------------------------------------------- */

static void testToolArgs(void)
{
    static char big[1100];
    int i, rc = 0;
    targ_reset();
    CHECK("targ_set ok", targ_set("t", "\xC3\xA9", 2) == 0);
    CHECK_STR("targ_set converts to cp437", targ("t"), "\x82");
    CHECK("targ_set path", targ_set("path", "C:\\A.TXT", 8) == 0);
    CHECK_STR("targ_set path round trip", targ("path"), "C:\\A.TXT");
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    for (i = 0; i < 20 && rc == 0; i++) {
        char key[8];
        sprintf(key, "k%d", i);
        rc = targ_set(key, big, sizeof big - 1);
    }
    CHECK("targ_set fills past 16 KB", rc == -1 && i < 20 && i > 12);
    targ_reset();
    CHECK("targ_set after reset", targ_set("a", "1", 1) == 0 && !strcmp(targ("a"), "1") && targ("path") == NULL);
    CHECK("targ_set tab", targ_set("tab", "a\tb", 3) == 0);
    CHECK_STR("targ_set keeps a tab", targ("tab"), "a\tb");
}

/* --- the file tools, on files the tests make and remove ------------------------ */

static ToolCtx ft;

/* A new call: no arguments yet, an empty result. */
static void newCall(void)
{
    targ_reset();
    tout_reset(&ft);
}

/* An argument as the harness stores it: UTF-8 through targ_set. */
static void arg(const char *key, const char *value)
{
    targ_set(key, value, strlen(value));
}

/* The bytes of a small file, NUL-terminated ("" if it can't be read). */
static const char *fileText(const char *path)
{
    static char text[700];
    FILE *f = fopen(path, "rb");
    unsigned n = 0;

    if (f) {
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
    }
    text[n] = 0;
    return text;
}

/* The result's lines from `from` up to `end` all have exactly `width`
   characters: none was cut off by the 4 KB cap. Returns how many there are,
   or -1 if one is short or long. */
static int wholeLines(const char *from, const char *end, unsigned width)
{
    int lines = 0;
    while (from < end) {
        const char *nl = strchr(from, '\n');
        if (nl == NULL || nl > end || (unsigned)(nl - from) != width)
            return -1;
        lines++;
        from = nl + 1;
    }
    return lines;
}

/* The last line of the result (where a trailer goes), or NULL. */
static const char *lastLine(void)
{
    const char *p = ft.out + ft.len;
    if (ft.len == 0 || p[-1] != '\n')
        return NULL;
    for (p--; p > ft.out && p[-1] != '\n'; p--)
        ;
    return p;
}

static void testTypedArgs(void)
{
    ToolCtx t;
    long n;
    int on;

    targ_reset();
    tout_reset(&t);
    arg("dec", "010");
    arg("hex", "0x10");
    arg("neg", "-3");
    arg("frac", "1.5");
    arg("t", "TRUE");
    arg("f", "0");
    arg("bad", "yes");
    CHECK("targ_long decimal, not octal", targ_long(&t, "dec", 0, &n) == 0 && n == 10);
    CHECK("targ_long 0x hex", targ_long(&t, "hex", 0, &n) == 0 && n == 16);
    CHECK("targ_long negative", targ_long(&t, "neg", 0, &n) == 0 && n == -3);
    CHECK("targ_long absent is the default", targ_long(&t, "none", 7, &n) == 0 && n == 7);
    CHECK("targ_long fraction fails", targ_long(&t, "frac", 7, &n) == -1);
    CHECK_STR("targ_long error names the argument", t.out, "frac must be a whole number");
    CHECK("targ_flag true", targ_flag("t") == 1 && targ_flag("f") == 0 && targ_flag("none") == 0);
    tout_reset(&t);
    CHECK("targ_bool bad fails", targ_bool(&t, "bad", &on) == -1 && on == -1);
    CHECK_STR("targ_bool error names the argument", t.out, "bad must be true or false");
}

static void testFileRead(void)
{
    FILE *f = fopen("C:\\ZZREAD.TXT", "wb");
    const char *tail;
    unsigned i, from = 0;
    int shown;

    for (i = 1; i <= 200; i++)        /* 200 lines of 60 characters */
        fprintf(f, "line %03u xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n", i);
    fclose(f);

    newCall();
    arg("path", "C:\\ZZREAD.TXT");
    arg("lines", "200");
    CHECK("read over the cap ok", tool_read(&ft) == TS_OK);
    tail = lastLine();
    CHECK("read over the cap: the result fits", ft.len <= TOOL_OUT_MAX);
    CHECK("read over the cap: a trailer says it stopped",
          tail != NULL && sscanf(tail, "...(more lines from %u; use start)", &from) == 1);
    shown = tail ? wholeLines(ft.out, tail, 66) : -1;
    CHECK("read over the cap: no line is cut", shown > 50);
    CHECK("read over the cap: the trailer names the next line", shown > 0 && from == (unsigned)shown + 1);

    newCall();
    arg("path", "C:\\ZZREAD.TXT");
    arg("start", "195");              /* a JSON integer arrives as its digits */
    CHECK("read near the end ok", tool_read(&ft) == TS_OK && !strncmp(ft.out, "  195 line 195", 14));
    CHECK("read near the end: no trailer", strstr(ft.out, "more lines") == NULL
          && wholeLines(ft.out, ft.out + ft.len, 66) == 6);

    newCall();
    arg("path", "C:\\ZZREAD.TXT");
    arg("lines", "ten");
    CHECK("read unparsable lines fails", tool_read(&ft) == TS_ERROR && strstr(ft.out, "lines") != NULL);
    remove("C:\\ZZREAD.TXT");
}

static void testFileWrite(void)
{
    newCall();
    arg("path", "C:\\ZZWRITE.TXT");
    arg("text", "a\tb\none\n");
    CHECK("write ok", tool_write(&ft) == TS_OK);
    CHECK_STR("write keeps tabs", fileText("C:\\ZZWRITE.TXT"), "a\tb\r\none\r\n");

    newCall();
    arg("path", "C:\\ZZWRITE.TXT");
    arg("text", "two\n");
    arg("append", "1");               /* JSON true, as setArg stores it */
    CHECK("write append boolean ok", tool_write(&ft) == TS_OK);
    CHECK_STR("write append boolean appends", fileText("C:\\ZZWRITE.TXT"), "a\tb\r\none\r\ntwo\r\n");

    newCall();
    arg("path", "C:\\ZZWRITE.TXT");
    arg("text", "three\n");
    arg("append", "true");            /* the string a model may send instead */
    CHECK("write append \"true\" ok", tool_write(&ft) == TS_OK);
    CHECK_STR("write append \"true\" appends", fileText("C:\\ZZWRITE.TXT"),
              "a\tb\r\none\r\ntwo\r\nthree\r\n");

    newCall();
    arg("path", "C:\\ZZWRITE.TXT");
    arg("text", "x");
    arg("append", "false");
    CHECK("write append \"false\" overwrites", tool_write(&ft) == TS_OK
          && !strcmp(fileText("C:\\ZZWRITE.TXT"), "x"));

    newCall();
    arg("path", "C:\\ZZWRITE.TXT");
    arg("text", "y");
    arg("append", "maybe");
    CHECK("write append unparsable fails", tool_write(&ft) == TS_ERROR && strstr(ft.out, "append") != NULL);
    CHECK_STR("write append unparsable leaves the file", fileText("C:\\ZZWRITE.TXT"), "x");
    remove("C:\\ZZWRITE.TXT");
}

static void testFileEdit(void)
{
    writeFile("C:\\ZZEDIT.C", "int main()\r\n{\r\n\tx;\r\n\treturn 0;\r\n\tx;\r\n}\r\n");

    newCall();
    arg("path", "C:\\ZZEDIT.C");
    arg("old", "\treturn 0;");
    arg("new", "\treturn 1;");
    CHECK("edit a tabbed line ok", tool_edit(&ft) == TS_OK);
    CHECK_STR("edit a tabbed line", fileText("C:\\ZZEDIT.C"),
              "int main()\r\n{\r\n\tx;\r\n\treturn 1;\r\n\tx;\r\n}\r\n");

    newCall();
    arg("path", "C:\\ZZEDIT.C");
    arg("old", "\tx;");
    arg("new", "\ty;");
    arg("all", "true");
    CHECK("edit all \"true\" ok", tool_edit(&ft) == TS_OK);
    CHECK_STR("edit all \"true\" replaces both", fileText("C:\\ZZEDIT.C"),
              "int main()\r\n{\r\n\ty;\r\n\treturn 1;\r\n\ty;\r\n}\r\n");
    remove("C:\\ZZEDIT.C");
}

static void testFileSearch(void)
{
    FILE *f = fopen("C:\\ZZSRCH.TXT", "wb");
    const char *tail, *first;
    unsigned i, more = 0;
    int shown;

    for (i = 1; i <= 100; i++)        /* 100 matching lines of 70 characters */
        fprintf(f, "match %03u xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n", i);
    fprintf(f, "a\tb\r\n");
    fclose(f);

    newCall();
    arg("text", "MATCH");
    arg("path", "C:\\ZZSRCH.TXT");
    CHECK("search over the cap ok", tool_search(&ft) == TS_OK);
    tail = lastLine();
    first = strchr(ft.out, '\n');     /* after the file's name */
    CHECK("search over the cap: the result fits", ft.len <= TOOL_OUT_MAX);
    CHECK("search over the cap: a trailer says it stopped",
          tail != NULL && sscanf(tail, "+%u more matches", &more) == 1);
    shown = tail && first ? wholeLines(first + 1, tail, 77) : -1;
    CHECK("search over the cap: no line is cut", shown > 40);
    CHECK("search over the cap: the trailer counts the rest", shown > 0 && shown + more == 100);

    newCall();
    arg("text", "a\tb");
    arg("path", "C:\\ZZSRCH.TXT");
    CHECK("search finds a tab", tool_search(&ft) == TS_OK && strstr(ft.out, "  101: a\tb\n") != NULL);

    newCall();
    arg("text", "MATCH");
    arg("path", "C:\\ZZSRCH.TXT");
    arg("case", "true");
    CHECK("search case \"true\" matches case", tool_search(&ft) == TS_OK && strstr(ft.out, "no matches") != NULL);
    remove("C:\\ZZSRCH.TXT");
}

static void testFileDir(void)
{
    char path[24];
    const char *tail;
    unsigned i, more = 0;
    int shown;

    mkdir("C:\\ZZDIR");
    for (i = 0; i < 100; i++) {
        sprintf(path, "C:\\ZZDIR\\F%03u.TXT", i);
        writeFile(path, "");
    }
    newCall();
    arg("path", "C:\\ZZDIR");
    CHECK("dir over the cap ok", tool_dir(&ft) == TS_OK);
    tail = lastLine();
    CHECK("dir over the cap: the result fits", ft.len <= TOOL_OUT_MAX);
    CHECK("dir over the cap: a trailer says it stopped", tail != NULL && sscanf(tail, "+%u more", &more) == 1);
    shown = tail ? wholeLines(ft.out, tail, 47) : -1;
    CHECK("dir over the cap: no line is cut", shown > 40);
    CHECK("dir over the cap: the trailer counts the rest", shown > 0 && shown + more == 101);   /* and .. */
    for (i = 0; i < 100; i++) {
        sprintf(path, "C:\\ZZDIR\\F%03u.TXT", i);
        remove(path);
    }
    rmdir("C:\\ZZDIR");
}

static void testCfgFile(void)
{
    Cfg *c = (Cfg *)malloc(sizeof(Cfg));

    writeFile("C:\\ZZCFG.CFG", "name Bob\r\n[x]\r\nhost h\r\n\x1A");
    cfg_defaults(c);
    CHECK("cfg file ending in Ctrl-Z loads", cfg_load(c, "C:\\ZZCFG.CFG") == 0);
    CHECK("cfg file ending in Ctrl-Z settings", !strcmp(c->name, "Bob") && c->nproviders == 1);
    writeFile("C:\\ZZCFG.CFG", "name Ann\r\n\x1Agarbage\r\nbogus 1\r\n");
    cfg_defaults(c);
    CHECK("cfg file: nothing after Ctrl-Z is read", cfg_load(c, "C:\\ZZCFG.CFG") == 0 && !strcmp(c->name, "Ann"));
    remove("C:\\ZZCFG.CFG");
    free(c);
}

/* --- long deltas cut safely ------------------------------------------------------ */

static void longDelta(ApiStream *a, const char *prefix, unsigned prefixLen, const char *tail)
{
    static char data[1200];
    unsigned i;
    strcpy(data, "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"");
    for (i = 0; i < prefixLen; i++)
        strncat(data, prefix, 1);
    strcat(data, tail);
    strcat(data, "\"}}");
    ev(a, "content_block_delta", data);
}

static void testLongDeltas(void)
{
    static char input[700];
    static char want[800];
    ApiStream a;

    asReset(&a, input, sizeof input);
    ev(&a, "message_start", "{\"type\":\"message_start\",\"message\":{\"model\":\"m\",\"usage\":{\"input_tokens\":1}}}");
    ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}");
    longDelta(&a, "a", 511, "\\\\b");                 /* the \\ escape straddles byte 512 */
    memset(want, 'a', 511); strcpy(want + 511, "\\b");
    CHECK_STR("long delta: escape at the cut", asText, want);
    asText[0] = asHist[0] = 0;
    longDelta(&a, "a", 508, "\\ud83d\\ude00!");      /* the pair straddles the cut */
    memset(want, 'a', 508); strcpy(want + 508, "?!");
    CHECK_STR("long delta: surrogate pair at the cut", asText, want);
    asText[0] = asHist[0] = 0;
    longDelta(&a, "a", 511, "\xC3\xA9!");             /* raw UTF-8 at the cut */
    memset(want, 'a', 511); strcpy(want + 511, "\x82!");
    CHECK_STR("long delta: utf-8 at the cut", asText, want);
    ev(&a, "content_block_stop", "{\"type\":\"content_block_stop\",\"index\":0}");

    asReset(&a, input, sizeof input);
    ev(&a, "message_start", "{\"type\":\"message_start\",\"message\":{\"model\":\"m\",\"usage\":{\"input_tokens\":1}}}");
    ev(&a, "content_block_start", "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"write\",\"input\":{}}}");
    {
        static char data[1300];
        unsigned i;
        /* partial_json: {"path":"C:\\A.TXT","text":"aaaa...a"} with the \\ of the path at the cut */
        strcpy(data, "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"text\\\":\\\"");
        for (i = 0; i < 494; i++)
            strcat(data, "a");
        strcat(data, "\\\",\\\"path\\\":\\\"C:\\\\\\\\A.TXT\\\"}\"}}");
        ev(&a, "content_block_delta", data);
    }
    ev(&a, "content_block_stop", "{\"type\":\"content_block_stop\",\"index\":0}");
    memset(want, 'a', 494); want[494] = 0;
    CHECK("long input: valid JSON after the cut", strstr(a.toolInput, "\"path\":\"C:\\\\A.TXT\"}") != NULL
          && strstr(asHist, "\"path\":\"C:\\\\A.TXT\"}}") != NULL);
}

/* --- conversation messages ---------------------------------------------------- */

static int countOf(const char *hay, const char *needle)
{
    int n = 0;
    while ((hay = strstr(hay, needle)) != NULL) { n++; hay += strlen(needle); }
    return n;
}

static void testConvo(void)
{
    Convo c;
    const char *t;
    remove("C:\\HISTTEST.JSN");
    hist_open("C:\\HISTTEST.JSN");
    convo_init(&c);
    convo_turn_begin(&c);
    convo_user_text(&c, "first");
    hist_puts("{\"role\":\"assistant\",\"content\":[{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"read\",\"input\":{}}]}");
    convo_assistant_done(&c);
    convo_tool_result(&c, "t1", "    1 hi\n", 0, NULL);
    convo_settle(&c);                    /* the next request failed: the turn ends here */
    CHECK("convo: results last means merge", c.mergeNext == 1);
    convo_turn_begin(&c);
    convo_user_text(&c, "second");
    t = histText();
    CHECK("convo: roles alternate after merge", countOf(t, "\"role\":\"user\"") == 2 && countOf(t, "\"role\":\"assistant\"") == 1);
    CHECK("convo: text joined the results", strstr(t, "\"is_error\":false},{\"type\":\"text\",\"text\":\"second\"}]}") != NULL);
    convo_rollback_turn(&c);             /* nothing answered: back to before "second" */
    t = histText();
    CHECK("convo: rollback restores results message", c.mergeNext == 1 && !strcmp(t + strlen(t) - 2, "]}")
          && strstr(t, "second") == NULL && countOf(t, "\"role\":\"user\"") == 2);
    convo_turn_begin(&c);
    convo_user_text(&c, "third");
    hist_puts("{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"ok\"}]}");
    convo_assistant_done(&c);
    CHECK("convo: plain reply clears merge", c.mergeNext == 0 && convo_settle(&c) == 0);
    convo_tool_result(&c, "t9", "x", 1, "Tool limit reached; answer now without tools.");
    CHECK("convo: result with note", strstr(histText(), "\"is_error\":true},{\"type\":\"text\",\"text\":\"Tool limit reached") != NULL);
    hist_close();
    remove("C:\\HISTTEST.JSN");
}

/* A file whose scan can't finish (a read fails partway) is left whole and
   the open fails, instead of being cut to what was read. */
static void testHistoryScanFails(void)
{
    long size;
    int i;

    remove("C:\\HISTTEST.JSN");
    hist_open("C:\\HISTTEST.JSN");
    for (i = 0; i < 40; i++) {        /* 40 messages, about 1.6 KB: four reads */
        hist_puts("{\"role\":\"user\",\"content\":\"0123456789\"}");
        hist_message_done();
    }
    hist_close();
    size = fs_size("C:\\HISTTEST.JSN");
    hist_test_scan_reads = 1;
    CHECK("hist open fails when its scan can't finish", hist_open("C:\\HISTTEST.JSN") == -1);
    hist_close();                     /* DOS updates the directory's size on close */
    CHECK("hist open after a failed scan leaves the file", fs_size("C:\\HISTTEST.JSN") == size && size > 1024);
    hist_test_scan_reads = -1;
    CHECK("hist open after the failure works", hist_open("C:\\HISTTEST.JSN") == 0 && hist_size() == size);
    hist_close();
    remove("C:\\HISTTEST.JSN");
}

static void testHistoryCommit(void)
{
    remove("C:\\HISTTEST.JSN");
    hist_open("C:\\HISTTEST.JSN");
    hist_puts("{\"role\":\"user\",\"content\":\"a\"}");
    hist_message_done();
    CHECK("hist: message_done commits to the directory", fs_size("C:\\HISTTEST.JSN") == hist_size());
    hist_close();
    remove("C:\\HISTTEST.JSN");
}

/* --- events too long for the SSE buffer (a long thinking signature) -------------- */

static ApiStream bigAs;
static int bigRefused;
static void bigOnEvent(void *ctx, const char *event, const char *data, unsigned n)
{ (void)ctx; apistream_event(&bigAs, event, data, n); }
static int bigOnBig(void *ctx, int phase, const char *event, const char *data, unsigned n)
{
    int rc = apistream_big(&bigAs, phase, event, data, n);
    (void)ctx;
    if (rc > 0)
        bigRefused++;
    return rc > 0;
}

/* Feed `text` in pieces of `step` bytes to an SSE buffer that holds the
   ordinary events but not the 300-byte string. */
static int bigFeed(const char *text, unsigned step)
{
    static char buf[200];
    static Sse s;
    unsigned i, n = strlen(text);
    int rc = 0;
    sse_init(&s, buf, sizeof buf);
    s.big = bigOnBig;
    for (i = 0; i < n; i += step)
        rc |= sse_feed(&s, text + i, n - i < step ? n - i : step, bigOnEvent, NULL);
    return rc;
}

static void testLongEvents(void)
{
    static char sig[301], feed[2000], want[800];
    static char input[64];
    unsigned i, step;
    for (i = 0; i < 300; i++)
        sig[i] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i % 64];
    sig[300] = 0;
    sprintf(want, "{\"role\":\"assistant\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"hm\",\"signature\":\"%s\"},"
                  "{\"type\":\"text\",\"text\":\"ok\"}]}", sig);
    for (step = 1; step <= 97; step += 48) {
        /* the API's order (type first), then the mock's (type last, spaces) */
        int order;
        for (order = 0; order < 2; order++) {
            asReset(&bigAs, input, sizeof input);
            bigRefused = 0;
            sprintf(feed,
                "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"model\":\"m\",\"usage\":{}}}\n\n"
                "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}\n\n"
                "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"hm\"}}\n\n"
                "event: content_block_delta\ndata: %s\n\n"
                "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
                "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
                "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\"ok\"}}\n\n"
                "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
                "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n",
                order == 0 ? "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"SIG\"}}"
                           : "{\"index\": 0, \"delta\": {\"type\": \"signature_delta\", \"signature\": \"SIG\"}, \"type\": \"content_block_delta\"}");
            {   /* put the signature in */
                char *p = strstr(feed, "SIG");
                memmove(p + 300, p + 3, strlen(p + 3) + 1);
                memcpy(p, sig, 300);
            }
            CHECK("long signature fed", bigFeed(feed, step) == 0 && bigRefused == 0);
            CHECK_STR("long signature in the history", asHist, want);
        }
    }

    /* a long redacted_thinking block: its data streams the same way */
    asReset(&bigAs, input, sizeof input);
    sprintf(feed, "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"model\":\"m\",\"usage\":{}}}\n\n"
                  "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"redacted_thinking\",\"data\":\"%s\"}}\n\n"
                  "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
                  "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n", sig);
    CHECK("long redacted fed", bigFeed(feed, 7) == 0);
    sprintf(want, "{\"role\":\"assistant\",\"content\":[{\"type\":\"redacted_thinking\",\"data\":\"%s\"}]}", sig);
    CHECK_STR("long redacted in the history", asHist, want);

    /* anything else that long is still refused: "event too long" */
    asReset(&bigAs, input, sizeof input);
    bigRefused = 0;
    sprintf(feed, "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"%s\"}}\n\n"
                  "data: {\"type\":\"ping\"}\n\n", sig);
    CHECK("long text refused", bigFeed(feed, 50) == -1 && bigRefused == 1);
}

int main(void)
{
    CHECK("sanity", 1 + 1 == 2);
    testPlay();
    testMemory();
    testMemoryList();
    testMemoryManage();
    testTicks();
    testConsole();
    testEdit();
    testSearch();
    testUtf8();
    testJson();
    testChunked();
    testSse();
    testHttpHead();
    testCfg();
    testHistory();
    testApiReq();
    testApiStream();
    testLongEvents();
    testToolArgs();
    testTypedArgs();
    testFileRead();
    testFileWrite();
    testFileEdit();
    testFileSearch();
    testFileDir();
    testCfgFile();
    testLongDeltas();
    testConvo();
    testHistoryCommit();
    testHistoryScanFails();

    printf("%s %d\n", failed ? "UNITS-FAILED" : "UNITS-OK", failed ? failed : passed);
    return failed != 0;
}
