/* ClawDOS file tools: see dosfs.h. Thin wrappers over DOS calls that turn
 * DOS error codes into words the model can act on. */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <io.h>
#include <fcntl.h>
#include "dosfs.h"

#define DIR_MAX   150
#define LINE_MAX  160
#define BIN_MAX   256

static unsigned char buf[512];

/* The path argument `key`, or NULL after reporting why it can't be used. */
static const char *path_arg(ToolCtx *t, const char *key, int *status)
{
    const char *p = targ(key);
    if (p == NULL || !*p) {
        static char msg[40];
        sprintf(msg, "missing %s", key);
        *status = tool_fail(t, msg);
        return NULL;
    }
    if (strlen(p) > FS_PATH_MAX) {
        *status = tool_fail(t, "path too long (max 64 characters)");
        return NULL;
    }
    return p;
}

static int dos_fail(ToolCtx *t, int code)
{
    return tool_fail(t, dos_error(code));
}

/* INT 21h with DS:DX = a and (for rename) ES:DI = b. Returns 0 or a DOS error. */
static int dos_path_call(unsigned char ah, const char *a, const char *b)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    r.h.ah = ah;
    s.ds = FP_SEG(a);
    r.x.dx = FP_OFF(a);
    if (b) {
        s.es = FP_SEG(b);
        r.x.di = FP_OFF(b);
    }
    intdosx(&r, &r, &s);
    return r.x.cflag ? r.x.ax : 0;
}

long fs_size(const char *path)
{
    struct find_t f;
    if (_dos_findfirst(path, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_ARCH, &f))
        return -1;
    return (long)f.size;
}


/* --- dir --------------------------------------------------------------------- */

int tool_dir(ToolCtx *t)
{
    char pattern[FS_PATH_MAX + 8], size[12];
    struct find_t f;
    unsigned attrs = _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_SUBDIR | _A_ARCH;
    unsigned shown = 0, more = 0;
    int status, n;
    const char *p = path_arg(t, "path", &status);

    if (p == NULL)
        return status;
    strcpy(pattern, p);
    n = strlen(pattern);
    if (n == 0 || pattern[n - 1] == '\\' || pattern[n - 1] == ':') {
        strcat(pattern, "*.*");
    } else if (!strpbrk(pattern, "*?") && !_dos_findfirst(pattern, attrs, &f)
               && (f.attrib & _A_SUBDIR)) {
        strcat(pattern, "\\*.*");
    }

    if ((status = _dos_findfirst(pattern, attrs, &f)) != 0)
        return status == 18 ? tool_fail(t, "no matching files") : dos_fail(t, status);
    do {
        if (!strcmp(f.name, "."))
            continue;
        if (shown >= DIR_MAX) {
            more++;
            continue;
        }
        if (tool_esc())
            return TS_STOPPED;
        if (f.attrib & _A_SUBDIR)
            strcpy(size, "<DIR>");
        else
            sprintf(size, "%lu", (unsigned long)f.size);
        tout_printf(t, "%-12s %10s  %04u-%02u-%02u %02u:%02u  %c%c%c%c\n", f.name, size,
                    (f.wr_date >> 9) + 1980, (f.wr_date >> 5) & 15, f.wr_date & 31,
                    f.wr_time >> 11, (f.wr_time >> 5) & 63,
                    f.attrib & _A_RDONLY ? 'R' : '-', f.attrib & _A_HIDDEN ? 'H' : '-',
                    f.attrib & _A_SYSTEM ? 'S' : '-', f.attrib & _A_ARCH ? 'A' : '-');
        shown++;
    } while (_dos_findnext(&f) == 0);
    if (more)
        tout_printf(t, "+%u more\n", more);
    sprintf(t->summary, "%u entries", shown + more);
    return TS_OK;
}


/* --- read -------------------------------------------------------------------- */

static int looks_binary(const unsigned char *p, unsigned n)
{
    unsigned i, ctl = 0;
    for (i = 0; i < n; i++) {
        if (p[i] == 0)
            return 1;
        if (p[i] < 32 && p[i] != '\t' && p[i] != '\r' && p[i] != '\n' && p[i] != 0x1A && p[i] != 0x0C)
            ctl++;
    }
    return ctl * 10 > n;
}

static int read_binary(ToolCtx *t, int h)
{
    long offset = targ_num("offset", 0);
    long count = targ_num("bytes", BIN_MAX);
    long size = lseek(h, 0, SEEK_END);
    unsigned got;

    if (count < 1 || count > BIN_MAX)
        count = BIN_MAX;
    if (offset < 0 || offset >= size) {
        sprintf(t->summary, "offset past end (%ld bytes)", size);
        tout_printf(t, "binary file, %ld bytes; offset %ld is past the end\n", size, offset);
        return TS_ERROR;
    }
    lseek(h, offset, SEEK_SET);
    if (_dos_read(h, buf, (unsigned)count, &got))
        return tool_fail(t, "read fault");
    tout_printf(t, "binary file, %ld bytes; hex dump from offset %ld:\n", size, offset);
    tout_hexdump(t, buf, got, 0xFFFF, (unsigned long)offset);
    sprintf(t->summary, "%u bytes (hex)", got);
    return TS_OK;
}

int tool_read(ToolCtx *t)
{
    char line[LINE_MAX + 4];
    unsigned len = 0, lineno = 1, shown = 0, got, i;
    long start = targ_num("start", 1), count = targ_num("lines", 100);
    int h, status, ended = 0;
    const char *p = path_arg(t, "path", &status);

    if (p == NULL)
        return status;
    if ((status = _dos_open(p, O_RDONLY, &h)) != 0)
        return dos_fail(t, status);
    if (_dos_read(h, buf, sizeof(buf), &got)) {
        _dos_close(h);
        return tool_fail(t, "read fault");
    }
    if (looks_binary(buf, got)) {
        status = read_binary(t, h);
        _dos_close(h);
        return status;
    }
    if (start < 1)
        start = 1;
    if (count < 1)
        count = 100;

    while (got > 0 && !ended) {
        for (i = 0; i < got; i++) {
            unsigned char c = buf[i];
            if (c == 0x1A) {               /* DOS end-of-file mark */
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
            if (lineno >= start) {
                line[len] = 0;
                tout_printf(t, "%5u %s\n", lineno, line);
                shown++;
            }
            lineno++;
            len = 0;
            if (shown >= count || t->truncated) {
                ended = 2;                 /* stopped early: more may follow */
                break;
            }
        }
        if (!ended && _dos_read(h, buf, sizeof(buf), &got))
            got = 0;
    }
    if (ended != 2 && len > 0 && lineno >= start) {
        line[len] = 0;
        tout_printf(t, "%5u %s\n", lineno, line);
        shown++;
    }
    if (ended == 2)
        tout_printf(t, "...(more lines from %u; use start)\n", lineno);
    else if (shown == 0)
        tout_printf(t, "(the file has only %u lines)\n", lineno - (len == 0));
    _dos_close(h);
    sprintf(t->summary, "%u lines", shown);
    return TS_OK;
}


/* --- write, delete, mkdir, rename -------------------------------------------- */

int tool_write(ToolCtx *t)
{
    const char *text = targ("text");
    int append = targ_num("append", 0) != 0;
    unsigned long total = 0;
    unsigned n = 0, wrote;
    int h, status, err = 0;
    const char *p = path_arg(t, "path", &status);

    if (p == NULL)
        return status;
    if (text == NULL)
        return tool_fail(t, "missing text");
    if (append && _dos_open(p, O_WRONLY, &h) == 0) {
        lseek(h, 0, SEEK_END);
    } else if ((status = _dos_creat(p, _A_NORMAL, &h)) != 0) {
        return dos_fail(t, status);
    }
    for (;; text++) {
        if (*text == '\n' && n < sizeof(buf))
            buf[n++] = '\r';
        if (*text && *text != '\r')
            buf[n++] = *text;
        if (n >= sizeof(buf) - 1 || !*text) {
            if (n && (_dos_write(h, buf, n, &wrote) || wrote < n)) {
                err = 1;
                break;
            }
            total += n;
            n = 0;
        }
        if (!*text)
            break;
    }
    _dos_close(h);
    if (err)
        return tool_fail(t, "disk full");
    sprintf(t->summary, "%lu bytes written", total);
    tout_printf(t, "%s %lu bytes to %s\n", append ? "appended" : "wrote", total, p);
    return TS_OK;
}

int tool_delete(ToolCtx *t)
{
    int status;
    const char *p = path_arg(t, "path", &status);

    if (p == NULL)
        return status;
    if (strpbrk(p, "*?"))
        return tool_fail(t, "no wildcards (use run with DEL for those)");
    if ((status = dos_path_call(0x41, p, NULL)) != 0)
        return dos_fail(t, status);
    strcpy(t->summary, "deleted");
    tout_printf(t, "deleted %s\n", p);
    return TS_OK;
}

int tool_mkdir(ToolCtx *t)
{
    int status;
    const char *p = path_arg(t, "path", &status);

    if (p == NULL)
        return status;
    if ((status = dos_path_call(0x39, p, NULL)) != 0)
        return dos_fail(t, status == 5 ? 82 : status);
    strcpy(t->summary, "created");
    tout_printf(t, "created directory %s\n", p);
    return TS_OK;
}

int tool_rename(ToolCtx *t)
{
    int status;
    const char *from = path_arg(t, "from", &status), *to;

    if (from == NULL || (to = path_arg(t, "to", &status)) == NULL)
        return status;
    if ((status = dos_path_call(0x56, from, to)) != 0)
        return dos_fail(t, status == 5 && fs_size(to) >= 0 ? 80 : status);
    sprintf(t->summary, "renamed to %.40s", to);
    tout_printf(t, "renamed %s to %s\n", from, to);
    return TS_OK;
}
