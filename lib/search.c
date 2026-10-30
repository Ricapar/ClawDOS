/* ClawDOS search tool: see search.h. */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <fcntl.h>
#include "search.h"
#include "dosfs.h"

#define LINE_MAX   255                /* longer lines are matched on this much */
#define SHOW_COLS  100
#define MATCH_MAX  80                 /* matches listed; the rest are counted */

static unsigned char buf[512];
static char line[LINE_MAX + 1];

int search_match(const char *line, const char *text, int matchCase)
{
    unsigned n = strlen(text), i;

    if (n == 0)
        return 0;
    if (matchCase)
        return strstr(line, text) != NULL;
    for (; *line; line++) {
        for (i = 0; i < n && line[i]; i++)
            if (toupper((unsigned char)line[i]) != toupper((unsigned char)text[i]))
                break;
        if (i == n)
            return 1;
    }
    return 0;
}

typedef struct {
    const char *text;
    int matchCase;
    unsigned shown, more, files, hitFiles, binary;
} Search;

/* Check line[0..len) of `path`; the file's name goes above its first match. */
static void checkLine(ToolCtx *t, Search *s, const char *path, unsigned len,
                      unsigned lineno, unsigned *hits)
{
    line[len] = 0;
    if (!search_match(line, s->text, s->matchCase))
        return;
    if (s->shown >= MATCH_MAX || t->truncated) {
        s->more++;
    } else {
        if (*hits == 0)
            tout_printf(t, "%s\n", path);
        line[SHOW_COLS] = 0;
        tout_printf(t, "%5u: %s\n", lineno, line);
        s->shown++;
    }
    ++*hits;
}

static void searchFile(ToolCtx *t, Search *s, const char *path)
{
    unsigned got, i, len = 0, lineno = 1, hits = 0;
    int h, ended = 0;

    if (_dos_open(path, O_RDONLY, &h))
        return;
    if (_dos_read(h, buf, sizeof(buf), &got)) {
        _dos_close(h);
        return;
    }
    if (memchr(buf, 0, got)) {        /* not text */
        s->binary++;
        _dos_close(h);
        return;
    }
    s->files++;
    for (;;) {
        for (i = 0; i < got; i++) {
            unsigned char c = buf[i];
            if (c == 0x1A) {          /* DOS end-of-file mark */
                ended = 1;
                break;
            }
            if (c == '\r')
                continue;
            if (c != '\n') {
                if (len < LINE_MAX)
                    line[len++] = c;
                continue;
            }
            checkLine(t, s, path, len, lineno++, &hits);
            len = 0;
        }
        if (ended || _dos_read(h, buf, sizeof(buf), &got) || got == 0)
            break;
    }
    if (len)
        checkLine(t, s, path, len, lineno, &hits);
    _dos_close(h);
    if (hits)
        s->hitFiles++;
}

/* One path from the list: a file, a directory or a wildcard. */
static int searchSpec(ToolCtx *t, Search *s, const char *spec)
{
    char pattern[FS_PATH_MAX + 8], path[FS_PATH_MAX + 16];
    struct find_t f;
    unsigned n = strlen(spec), dirLen, found = 0;
    const char *slash;

    strcpy(pattern, spec);
    if (n == 0 || pattern[n - 1] == '\\' || pattern[n - 1] == ':') {
        strcat(pattern, "*.*");
    } else if (!strpbrk(pattern, "*?") && !_dos_findfirst(pattern, _A_SUBDIR, &f)
               && (f.attrib & _A_SUBDIR)) {
        strcat(pattern, "\\*.*");
    }
    slash = strrchr(pattern, '\\');
    if (slash == NULL)
        slash = strchr(pattern, ':');
    dirLen = slash ? (unsigned)(slash - pattern) + 1 : 0;
    memcpy(path, pattern, dirLen);

    if (_dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_ARCH, &f) == 0) {
        do {
            if (tool_esc())
                return TS_STOPPED;
            strcpy(path + dirLen, f.name);
            searchFile(t, s, path);
            found++;
        } while (_dos_findnext(&f) == 0);
    }
    if (!found)
        tout_printf(t, "(no files match %s)\n", spec);
    return TS_OK;
}

int tool_search(ToolCtx *t)
{
    const char *text = targ("text"), *paths = targ("path");
    char spec[FS_PATH_MAX + 1];
    Search s;
    int status;

    if (text == NULL || !*text)
        return tool_fail(t, "missing text");
    if (strchr(text, '\n'))
        return tool_fail(t, "text must be on one line");
    if (paths == NULL || !*paths)
        return tool_fail(t, "missing path");
    memset(&s, 0, sizeof(s));
    s.text = text;
    s.matchCase = targ_num("case", 0) != 0;

    while (*paths) {
        unsigned n;
        paths += strspn(paths, " ;,\t");
        n = strcspn(paths, " ;,\t");
        if (n == 0)
            break;
        if (n > FS_PATH_MAX)
            return tool_fail(t, "path too long (max 64 characters each)");
        memcpy(spec, paths, n);
        spec[n] = 0;
        paths += n;
        if ((status = searchSpec(t, &s, spec)) != TS_OK)
            return status;
    }

    if (s.more)
        tout_printf(t, "+%u more matches (narrow the search)\n", s.more);
    if (s.shown + s.more == 0)
        tout_printf(t, "no matches in %u file%s\n", s.files, s.files == 1 ? "" : "s");
    if (s.binary)
        tout_printf(t, "(skipped %u binary file%s)\n", s.binary, s.binary == 1 ? "" : "s");
    sprintf(t->summary, "%u match%s in %u file%s", s.shown + s.more, s.shown + s.more == 1 ? "" : "es",
            s.hitFiles, s.hitFiles == 1 ? "" : "s");
    return TS_OK;
}
