/* ClawDOS edit tool: see edit.h. */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <io.h>
#include <fcntl.h>
#include "edit.h"
#include "dosfs.h"

#define SHOW_MAX   20                 /* lines of the result shown to the model */
#define SHOW_COLS  120
#define HINT_MAX   5                  /* line numbers listed in an error */

unsigned edit_count(const char *src, const char *old, unsigned *first)
{
    unsigned n = 0, olen = strlen(old);
    const char *p = src, *m;

    if (olen == 0)
        return 0;
    while ((m = strstr(p, old)) != NULL) {
        if (n++ == 0)
            *first = (unsigned)(m - src);
        p = m + olen;
    }
    return n;
}

unsigned edit_line_of(const char *src, unsigned pos)
{
    unsigned line = 1, i;
    for (i = 0; i < pos && src[i]; i++)
        line += src[i] == '\n';
    return line;
}

unsigned edit_lines(const char *s)
{
    unsigned n = 0;
    if (!*s)
        return 0;
    for (; *s; s++)
        n += *s == '\n';
    return n + (s[-1] != '\n');
}

int edit_replace(const char *src, const char *old, const char *repl, int all,
                 EditOut out, void *ctx)
{
    unsigned olen = strlen(old), rlen = strlen(repl), n;
    const char *p = src, *m;

    if (olen == 0)
        return -1;
    while ((m = strstr(p, old)) != NULL) {
        if (m > p && out(ctx, p, (unsigned)(m - p)))
            return 1;
        if (rlen && out(ctx, repl, rlen))
            return 1;
        p = m + olen;
        if (!all)
            break;
    }
    n = strlen(p);
    return n && out(ctx, p, n) ? 1 : 0;
}


/* --- the file ------------------------------------------------------------------ */

/* The whole file at path in DOS memory (given back with _dos_freemem, so a
   program run later gets it: the C heap would keep it), CRLF folded to LF
   and NUL-terminated. NULL after reporting why. */
static char *load(ToolCtx *t, const char *path, unsigned *seg, int *crlf, int *status)
{
    unsigned size, got, i, j;
    long len;
    char *buf;
    int h, rc;

    if ((rc = _dos_open(path, O_RDONLY, &h)) != 0) {
        *status = tool_fail(t, dos_error(rc));
        return NULL;
    }
    len = lseek(h, 0, SEEK_END);
    if (len < 0 || len > (long)EDIT_FILE_MAX) {
        _dos_close(h);
        *status = tool_fail(t, "file too big to edit (max 60000 bytes); use write");
        return NULL;
    }
    size = (unsigned)len;
    if (_dos_allocmem((size + 16) >> 4, seg)) {
        _dos_close(h);
        *status = tool_fail(t, "not enough memory for the file");
        return NULL;
    }
    buf = (char *)MK_FP(*seg, 0);
    lseek(h, 0, SEEK_SET);
    rc = _dos_read(h, buf, size, &got);
    _dos_close(h);
    if (rc || got != size) {
        _dos_freemem(*seg);
        *status = tool_fail(t, "read fault");
        return NULL;
    }
    *crlf = 0;
    for (i = j = 0; i < size; i++) {
        if (buf[i] == 0) {
            _dos_freemem(*seg);
            *status = tool_fail(t, "binary file: edit only changes text files");
            return NULL;
        }
        if (buf[i] == '\r' && i + 1 < size && buf[i + 1] == '\n') {
            *crlf = 1;
            continue;
        }
        buf[j++] = buf[i];
    }
    buf[j] = 0;
    return buf;
}

typedef struct {
    int h, crlf, err;
    unsigned n;
} FileOut;

static unsigned char wbuf[512];

static int flushOut(FileOut *f)
{
    unsigned wrote;
    if (f->n && (_dos_write(f->h, wbuf, f->n, &wrote) || wrote < f->n))
        f->err = 1;
    f->n = 0;
    return f->err;
}

static int fileOut(void *ctx, const char *p, unsigned n)
{
    FileOut *f = (FileOut *)ctx;
    for (; n; n--, p++) {
        if (*p == '\r')               /* line ends are ours to write (a lone CR goes too) */
            continue;
        if (f->n >= sizeof(wbuf) - 2 && flushOut(f))
            return 1;
        if (*p == '\n' && f->crlf)
            wbuf[f->n++] = '\r';
        wbuf[f->n++] = *p;
    }
    return 0;
}

/* Lines from..to of what edit_replace produces, numbered like the read tool. */
typedef struct {
    ToolCtx *t;
    unsigned line, from, to, len;
    char text[SHOW_COLS + 1];
} ShowOut;

static int showOut(void *ctx, const char *p, unsigned n)
{
    ShowOut *s = (ShowOut *)ctx;
    for (; n; n--, p++) {
        if (*p == '\r')
            continue;
        if (*p != '\n') {
            if (s->len < SHOW_COLS)
                s->text[s->len++] = *p;
            continue;
        }
        if (s->line >= s->from) {
            s->text[s->len] = 0;
            tout_printf(s->t, "%5u %s\n", s->line, s->text);
        }
        s->len = 0;
        if (++s->line > s->to)
            return 1;
    }
    return 0;
}

/* " (lines 12, 40, 77 ...)" for the places `what` occurs in src. */
static void whereLines(const char *src, const char *what, char *out)
{
    unsigned n = 0, wlen = strlen(what);
    const char *p = src, *m;

    *out = 0;
    while (wlen && (m = strstr(p, what)) != NULL) {
        if (n == HINT_MAX) {
            strcat(out, " ...");
            break;
        }
        sprintf(out + strlen(out), "%s%u", n ? ", " : " (line ", edit_line_of(src, (unsigned)(m - src)));
        n++;
        p = m + wlen;
    }
    if (n)
        strcat(out, ")");
}

/* Why `old` wasn't found: where its first line is, if anywhere. */
static int notFound(ToolCtx *t, const char *src, const char *old)
{
    char first[SHOW_COLS + 1], where[64];
    unsigned n;

    while (*old == ' ' || *old == '\t')
        old++;
    n = strcspn(old, "\n");
    if (n > SHOW_COLS)
        n = SHOW_COLS;
    memcpy(first, old, n);
    first[n] = 0;
    while (n && (first[n - 1] == ' ' || first[n - 1] == '\t'))
        first[--n] = 0;
    whereLines(src, first, where);
    strcpy(t->summary, "old text not found");
    if (*where && strchr(old, '\n'))
        tout_printf(t, "old text not found; its first line is in the file%s, so a later line "
                       "differs (spaces, tabs, an edit since you read it?). Read those lines "
                       "and copy them exactly.\n", where);
    else
        tout_puts(t, "old text not found: read the file and copy the text exactly, "
                     "spaces and tabs included.\n");
    return TS_ERROR;
}

/* The temporary file next to `path`. */
static void tempPath(const char *path, char *out)
{
    const char *slash = strrchr(path, '\\'), *colon = strchr(path, ':');
    const char *end = slash ? slash + 1 : colon ? colon + 1 : path;
    unsigned n = (unsigned)(end - path);
    memcpy(out, path, n);
    strcpy(out + n, "CLAWEDIT.$$$");
}

int tool_edit(ToolCtx *t)
{
    const char *path = targ("path"), *old = targ("old"), *repl = targ("new");
    int all, crlf, status, rc;
    unsigned seg, first = 0, count, line, shown;
    char tmp[FS_PATH_MAX + 16], where[64];
    char *src;
    FileOut f;
    ShowOut s;

    if (path == NULL || !*path)
        return tool_fail(t, "missing path");
    if (strlen(path) > FS_PATH_MAX)
        return tool_fail(t, "path too long (max 64 characters)");
    if (old == NULL || !*old)
        return tool_fail(t, "missing old (the exact text to replace)");
    if (repl == NULL)
        repl = "";
    if (!strcmp(old, repl))
        return tool_fail(t, "old and new are the same: nothing to change");
    if (targ_bool(t, "all", &all))
        return TS_ERROR;
    if ((src = load(t, path, &seg, &crlf, &status)) == NULL)
        return status;

    count = edit_count(src, old, &first);
    if (count == 0) {
        status = notFound(t, src, old);
        _dos_freemem(seg);
        return status;
    }
    if (count > 1 && !all) {
        whereLines(src, old, where);
        sprintf(t->summary, "old text occurs %u times", count);
        tout_printf(t, "old text occurs %u times%s: add surrounding lines to make it unique, "
                       "or set all to true to replace every one\n", count, where);
        _dos_freemem(seg);
        return TS_ERROR;
    }

    tempPath(path, tmp);
    if ((rc = _dos_creat(tmp, _A_NORMAL, &f.h)) != 0) {
        _dos_freemem(seg);
        return tool_fail(t, dos_error(rc));
    }
    f.crlf = crlf;
    f.err = 0;
    f.n = 0;
    edit_replace(src, old, repl, all, fileOut, &f);
    flushOut(&f);
    _dos_close(f.h);
    if (f.err) {
        remove(tmp);
        _dos_freemem(seg);
        return tool_fail(t, "disk full: the file was left as it was");
    }
    if (remove(path) || rename(tmp, path)) {
        _dos_freemem(seg);
        sprintf(t->summary, "couldn't replace the file");
        tout_printf(t, "couldn't replace %s; the edited text is in %s\n", path, tmp);
        return TS_ERROR;
    }

    line = edit_line_of(src, first);
    shown = edit_lines(repl);
    if (shown == 0)
        shown = 1;
    if (count > 1)
        tout_printf(t, "replaced %u occurrences in %s; around the first (line %u) now:\n",
                    count, path, line);
    else
        tout_printf(t, "edited %s: lines %u-%u became %u line%s; around them now:\n", path, line,
                    line + edit_lines(old) - 1, edit_lines(repl), edit_lines(repl) == 1 ? "" : "s");
    s.t = t;
    s.line = 1;
    s.from = line > 1 ? line - 1 : 1;
    s.to = line + shown;              /* one line of context each side */
    if (s.to - s.from >= SHOW_MAX)
        s.to = s.from + SHOW_MAX - 1;
    s.len = 0;
    if (edit_replace(src, old, repl, all, showOut, &s) == 0 && s.len && s.line <= s.to) {
        s.text[s.len] = 0;            /* a last line without a newline */
        tout_printf(t, "%5u %s\n", s.line, s.text);
    }
    _dos_freemem(seg);
    sprintf(t->summary, count > 1 ? "%u replaced" : "line %u", count > 1 ? count : line);
    return TS_OK;
}
