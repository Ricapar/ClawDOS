/* DOSUNIT.EXE's screenshot tests (lib/shotpng.c's PNG encoder and palettes,
 * SHOT.RAW, lib/convo.c's image results), in a module of their own: dosunit.c's
 * code segment is full. main() in dosunit.c calls them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sink.h"
#include "history.h"
#include "convo.h"
#include "shotpng.h"
#include "ticks.h"

void check(const char *name, int ok, const char *detail);   /* dosunit.c */
#define CHECK(name, cond) check((name), (cond), "")

/* The history file's text (the tests keep it short). */
static const char *histText(void)
{
    static char buf[1200];
    Sink s;
    sink_buffer(&s, buf, sizeof buf);
    hist_copy(&s);
    return buf;
}

static int endsWith(const char *text, const char *end)
{
    unsigned n = strlen(text), k = strlen(end);
    return n >= k && !strcmp(text + n - k, end);
}

/* The two files hold the same bytes. */
static int sameFile(const char *a, const char *b)
{
    static char x[256], y[256];
    FILE *f = fopen(a, "rb"), *g = fopen(b, "rb");
    int same = f != NULL && g != NULL;
    unsigned n, m;

    while (same) {
        n = fread(x, 1, sizeof x, f);
        m = fread(y, 1, sizeof y, g);
        same = n == m && !memcmp(x, y, n);
        if (n == 0)
            break;
    }
    if (f)
        fclose(f);
    if (g)
        fclose(g);
    return same;
}

static int samePalette(const unsigned char *rgb, unsigned n, const char *want)
{
    static char hex[40];
    unsigned i;
    hex[0] = 0;
    for (i = 0; i < n; i++)
        sprintf(hex + strlen(hex), "%s%02X%02X%02X", i ? " " : "", rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
    return !strcmp(hex, want);
}

/* The PNG encoder against tests/unit/pngref.py's files (C:\TESTS\REF320.PNG
   and REF640.PNG, made by the build from the same frame), the palettes the
   mode and colour register give, and SHOT.RAW read back. */
void testShotPng(void)
{
    unsigned char *frame = (unsigned char *)malloc(SHOT_BYTES);
    unsigned char rgb[12];
    ShotInfo info, back;
    unsigned long start, ticks;
    unsigned o;
    int rc;
    FILE *f;

    if (frame == NULL) {
        CHECK("shot: memory for the frame", 0);
        return;
    }
    for (o = 0; o < SHOT_BYTES; o++)              /* as pngref.unit_frame() */
        frame[o] = (unsigned char)(o * 13u + (o >> 7));
    memset(&info, 0, sizeof info);
    info.mode = 4;
    info.modeReg = 0x2A;
    info.colorReg = 0x11;
    info.color = SHOT_NO_COLOR;
    CHECK("shot: palette 0 bright on blue", shot_palette(&info, SHOT_320, rgb) == 4
          && samePalette(rgb, 4, "0000AA 55FF55 FF5555 FFFF55"));
    start = TICKS_NOW;
    rc = png_write_cga("C:\\SHOTTEST.PNG", frame, 2, rgb, 4);
    ticks = ticks_elapsed(TICKS_NOW, start);
    printf("shot: 320x200 PNG written in %lu.%lu s\n", ticks * 10 / 182 / 10, ticks * 10 / 182 % 10);
    CHECK("shot: 320x200 PNG as the reference", rc == 0 && sameFile("C:\\SHOTTEST.PNG", "C:\\TESTS\\REF320.PNG"));
    info.mode = 6;
    info.colorReg = 0x3F;
    info.color = 0x0E;                           /* shot_color wins over the register */
    CHECK("shot: 640x200 colours", shot_palette(&info, SHOT_640, rgb) == 2 && samePalette(rgb, 2, "000000 FFFF55"));
    CHECK("shot: 640x200 PNG as the reference", png_write_cga("C:\\SHOTTEST.PNG", frame, 1, rgb, 2) == 0
          && sameFile("C:\\SHOTTEST.PNG", "C:\\TESTS\\REF640.PNG"));
    info.color = SHOT_NO_COLOR;
    info.mode = 4;
    info.colorReg = 0x00;
    CHECK("shot: palette 0 dim, brown", shot_palette(&info, SHOT_320, rgb) == 4
          && samePalette(rgb, 4, "000000 00AA00 AA0000 AA5500"));
    info.colorReg = 0x2F;
    CHECK("shot: palette 1 dim on white", shot_palette(&info, SHOT_320, rgb) == 4
          && samePalette(rgb, 4, "FFFFFF 00AAAA AA00AA AAAAAA"));
    info.mode = 5;
    info.modeReg = 0x2E;                         /* the BIOS's mode 5: colour burst off */
    info.colorReg = 0x30;
    CHECK("shot: mode 5 is cyan, red, white", shot_palette(&info, SHOT_320, rgb) == 4
          && samePalette(rgb, 4, "000000 55FFFF FF5555 FFFFFF"));
    info.color = 0x31;                           /* mode 5 from the BIOS, then only 3D9h written */
    CHECK("shot: shot_color keeps the shadow's burst", shot_palette(&info, SHOT_320, rgb) == 4
          && samePalette(rgb, 4, "0000AA 55FFFF FF5555 FFFFFF"));
    info.color = SHOT_NO_COLOR;

    info.mode = 4;
    info.modeReg = 0x2A;                         /* the shadow says burst on; the program turned it off */
    info.color = 0x30 | SHOT_BURST_OFF;
    CHECK("shot: shot_color's burst bit", shot_palette(&info, SHOT_320, rgb) == 4
          && samePalette(rgb, 4, "000000 55FFFF FF5555 FFFFFF"));
    info.color = SHOT_NO_COLOR;
    info.modeReg = 0x1E;                         /* mode 6's shadow (burst bit, 640): not mode 5's palette */
    info.colorReg = 0x30;
    CHECK("shot: 640 shadow is no burst-off palette", shot_palette(&info, SHOT_320, rgb) == 4
          && samePalette(rgb, 4, "000000 55FFFF FF55FF FFFFFF"));

    /* the launcher's file: a ShotInfo, then the frame */
    info.how = SHOT_TIMED;
    info.force = SHOT_640;
    f = fopen(SHOT_RAW, "wb");
    if (f) {
        fwrite(&info, sizeof info, 1, f);
        fwrite(frame, 1, SHOT_BYTES, f);
        fclose(f);
    }
    memset(frame, 0, SHOT_BYTES);
    CHECK("shot: SHOT.RAW read back", shot_load_raw(&back, frame) == 0 && !memcmp(&back, &info, sizeof info)
          && frame[1] == 13 && frame[SHOT_BYTES - 1] == (unsigned char)((SHOT_BYTES - 1) * 13u + 127));
    remove(SHOT_RAW);
    CHECK("shot: no SHOT.RAW", shot_load_raw(&back, frame) == -1);
    remove("C:\\SHOTTEST.PNG");
    free(frame);
}

/* shot_at's seconds as tenths. */
void testShotSeconds(void)
{
    static const char *const good[] = { "1", "0.5", "0.1", "1.5", "1.25", "2.0", "2.", "300", "300.0", "300.04" };
    static const unsigned want[] = { 10, 5, 1, 15, 12, 20, 20, 3000, 3000, 3000 };
    static const char *const bad[] = { "0", "0.0", "0.05", "300.1", "301", "-1", "1e1", "x", "", ".5", "1.5s", "99999999" };
    static char name[48];
    unsigned i, tenths;

    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        sprintf(name, "shot_at %s", good[i]);
        CHECK(name, shot_seconds(good[i], &tenths) == 0 && tenths == want[i]);
    }
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        sprintf(name, "shot_at \"%s\" refused", bad[i]);
        CHECK(name, shot_seconds(bad[i], &tenths) == -1);
    }
}

/* How often needle is in hay. */
static int countOf(const char *hay, const char *needle)
{
    int n = 0;
    while ((hay = strstr(hay, needle)) != NULL) {
        n++;
        hay += strlen(needle);
    }
    return n;
}

/* The history file's size on disk is what history.c counts. */
static int sizeAgrees(void)
{
    FILE *f = fopen("C:\\HISTTEST.JSN", "rb");
    long n = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fclose(f);
    }
    return n == hist_size();
}

/* After a picture that could not go in: exactly one result for the call,
   its text saying so, no image block, and the file as history.c counts it. */
static int withoutPicture(const char *id)
{
    const char *h = histText();
    static char want[40];
    sprintf(want, "\"tool_use_id\":\"%s\"", id);
    return countOf(h, "tool_result") == 1 && countOf(h, want) == 1 && !strstr(h, "image/png")
           && !strstr(h, "\"image\"") && strstr(h, "could not be attached") && sizeAgrees();
}

static void writeFile(const char *path, unsigned n)
{
    FILE *f = fopen(path, "wb");
    unsigned i;
    for (i = 0; f && i < n; i++)
        fputc(i * 7, f);
    if (f)
        fclose(f);
}

/* A fresh history ending with a run call `id` from the assistant. */
static void imageCall(Convo *c, const char *id)
{
    hist_close();
    remove("C:\\HISTTEST.JSN");
    hist_open("C:\\HISTTEST.JSN");
    convo_init(c);
    convo_turn_begin(c);
    convo_user_text(c, "first");
    hist_puts("{\"role\":\"assistant\",\"content\":[{\"type\":\"tool_use\",\"id\":\"");
    hist_puts(id);
    hist_puts("\",\"name\":\"run\",\"input\":{}}]}");
    convo_assistant_done(c);
}

/* A tool result with a picture: the text block, then the file in base64 as
   an image block; one that cannot be read leaves the text and says so. */
void testConvoImage(void)
{
    static const char want[] = "{\"type\":\"tool_result\",\"tool_use_id\":\"t1\",\"content\":[{\"type\":\"text\","
        "\"text\":\"exit code 0\\n\"},{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":"
        "\"image/png\",\"data\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsc=\"}}],\"is_error\":false}]}";
    Convo c;
    FILE *f;
    int i;

    f = fopen("C:\\SHOTTEST.PNG", "wb");
    for (i = 0; f && i < 200; i++)               /* 192 bytes are read at a time: two pieces */
        fputc(i, f);
    if (f)
        fclose(f);
    imageCall(&c, "t1");
    CHECK("convo image: written", convo_tool_result_image(&c, "t1", "exit code 0\n", 0, NULL, "C:\\SHOTTEST.PNG") == 0
          && c.mergeNext == 1);
    CHECK("convo image: text then image", endsWith(histText(), want));
    imageCall(&c, "t2");
    f = fopen("C:\\SHOTTEST.PNG", "wb");          /* one byte: padded */
    if (f) {
        fputc('A', f);
        fclose(f);
    }
    convo_tool_result_image(&c, "t2", "ok", 0, NULL, "C:\\SHOTTEST.PNG");
    CHECK("convo image: padding", endsWith(histText(), "\"data\":\"QQ==\"}}],\"is_error\":false}]}"));
    imageCall(&c, "t4");
    hist_test_write_ok = 0;                      /* the result's only write (the last flush) fails */
    hist_test_write_bad = 1;
    CHECK("convo image: a failed write leaves one result without it",
          convo_tool_result_image(&c, "t4", "ok", 0, NULL, "C:\\SHOTTEST.PNG") == 1 && withoutPicture("t4"));
    hist_test_write_ok = -1;
    hist_test_write_bad = 0;
    writeFile("C:\\SHOTTEST.PNG", 6000);       /* 8 KB of base64: four flushes of the 2 KB buffer */
    imageCall(&c, "t5");
    hist_test_write_ok = 2;                      /* two pieces reach the disk, then a write fails */
    hist_test_write_bad = 1;
    CHECK("convo image: a write failing mid-picture leaves one result without it",
          convo_tool_result_image(&c, "t5", "ok", 0, NULL, "C:\\SHOTTEST.PNG") == 1 && withoutPicture("t5"));
    hist_test_write_ok = -1;
    hist_test_write_bad = 0;
    writeFile("C:\\SHOTTEST.PNG", 0);
    imageCall(&c, "t6");
    CHECK("convo image: an empty file leaves one result without it",
          convo_tool_result_image(&c, "t6", "ok", 0, NULL, "C:\\SHOTTEST.PNG") == 1 && withoutPicture("t6"));
    imageCall(&c, "t3");
    remove("C:\\SHOTTEST.PNG");
    CHECK("convo image: a missing file leaves one result without it",
          convo_tool_result_image(&c, "t3", "ok", 0, NULL, "C:\\SHOTTEST.PNG") == 1 && withoutPicture("t3")
          && endsWith(histText(), "\"content\":\"ok\\n(The screenshot could not be attached: its file could not "
                      "be read or written.)\",\"is_error\":false}]}"));
    hist_close();
    remove("C:\\HISTTEST.JSN");
}
