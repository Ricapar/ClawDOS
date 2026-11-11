/* ClawDOS memories, and the list of skills: see memory.h. */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include "memory.h"

static char buf[512];

int mem_name_ok(const char *name)
{
    unsigned n = 0;

    for (; *name; name++, n++)
        if (!isalnum((unsigned char)*name) && *name != '_' && *name != '-')
            return 0;
    return n >= 1 && n <= 8;
}

void mem_summary(const char *text, unsigned n, char *out)
{
    const char *end = text + n;
    unsigned len = 0;

    while (text < end && (*text == '\r' || *text == '\n' || *text == ' ' || *text == '\t'))
        text++;
    while (text < end && *text != '\r' && *text != '\n' && *text != 0x1A) {
        if (len < MEM_SUMMARY)
            out[len] = *text;
        len++;
        text++;
    }
    if (len > MEM_SUMMARY) {
        len = MEM_SUMMARY - 3;
        strcpy(out + len, "...");
        return;
    }
    while (len && out[len - 1] == ' ')
        len--;
    out[len] = 0;
}

/* C:\MEMORY\NAME.TXT for the `name` argument, or NULL after reporting why not. */
static const char *mem_path(ToolCtx *t, int *status)
{
    static char path[32];
    const char *name = targ("name");

    if (name == NULL || !mem_name_ok(name)) {
        *status = tool_fail(t, "bad memory name: use 1-8 letters, digits, _ or -");
        return NULL;
    }
    sprintf(path, "%s\\%s.TXT", MEM_DIR, name);
    strupr(path);
    return path;
}

/* The summary of <dir>\<file> (an 8.3 name), "" if it can't be read. */
static void file_summary(const char *dir, const char *file, char *summary)
{
    char path[32];
    unsigned got;
    int h;

    sprintf(path, "%s\\%s", dir, file);
    summary[0] = 0;
    if (_dos_open(path, 0, &h) == 0) {
        if (_dos_read(h, buf, sizeof(buf), &got) == 0)
            mem_summary(buf, got, summary);
        _dos_close(h);
    }
}

int mem_scan(MemEntry *out, int max)
{
    struct find_t f;
    MemEntry e;
    int count = 0, i, n;

    if (_dos_findfirst(MEM_DIR "\\*.TXT", _A_NORMAL | _A_RDONLY | _A_ARCH, &f))
        return 0;
    do {
        strncpy(e.name, f.name, 8);
        e.name[8] = 0;
        if (strchr(e.name, '.'))
            *strchr(e.name, '.') = 0;
        e.size = f.size;
        file_summary(MEM_DIR, f.name, e.summary);
        n = count < max ? count : max;          /* insertion sort by name */
        for (i = n; i > 0 && strcmp(out[i - 1].name, e.name) > 0; i--)
            if (i < max)
                out[i] = out[i - 1];
        if (i < max)
            out[i] = e;
        count++;
    } while (_dos_findnext(&f) == 0);
    return count;
}

static char *name_path(const char *name)
{
    static char path[32];

    if (!mem_name_ok(name))
        return NULL;
    sprintf(path, "%s\\%s.TXT", MEM_DIR, name);
    strupr(path);
    return path;
}

int mem_load(const char *name, char *out, unsigned max, int *cut)
{
    const char *p = name_path(name);
    unsigned got, i, n = 0;
    int h, ended = 0;

    *cut = 0;
    if (p == NULL || max == 0 || _dos_open(p, 0, &h))
        return -1;
    while (!ended && _dos_read(h, buf, sizeof(buf), &got) == 0 && got > 0) {
        for (i = 0; i < got; i++) {
            if (buf[i] == 0x1A) {
                ended = 1;
                break;
            }
            if (buf[i] == '\r')
                continue;
            if (n >= max - 1) {
                *cut = 1;
                ended = 1;
                break;
            }
            out[n++] = buf[i];
        }
    }
    _dos_close(h);
    out[n] = 0;
    return (int)n;
}

int mem_delete(const char *name)
{
    const char *p = name_path(name);

    if (p == NULL)
        return -1;
    _dos_setfileattr(p, _A_NORMAL);   /* a read-only one goes too */
    return remove(p) ? -1 : 0;
}

/* One line per <dir>\*.TXT: its name (a memory's, or with paths set the
   file's whole path, which is what the read tool takes), size and summary.
   How many, or -1 after an Esc. */
static int list_dir(ToolCtx *t, const char *dir, int paths)
{
    struct find_t f;
    char summary[MEM_SUMMARY + 1], name[24], pattern[20];
    int count = 0;

    sprintf(pattern, "%s\\*.TXT", dir);
    if (_dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_ARCH, &f))
        return 0;
    do {
        if (tool_esc())
            return -1;
        if (paths)
            sprintf(name, "%s\\%s", dir, f.name);
        else {
            strcpy(name, f.name);
            *strchr(name, '.') = 0;
        }
        file_summary(dir, f.name, summary);
        tout_printf(t, paths ? "%-22s %5lu  %s\n" : "%-8s %5lu  %s\n", name, (unsigned long)f.size, summary);
        count++;
    } while (_dos_findnext(&f) == 0);
    return count;
}

int tool_memory_list(ToolCtx *t)
{
    int count = list_dir(t, MEM_DIR, 0);

    if (count < 0)
        return TS_STOPPED;
    if (count == 0) {
        strcpy(t->summary, "none yet");
        tout_puts(t, "no memories yet\n");
        return TS_OK;
    }
    sprintf(t->summary, "%d memor%s", count, count == 1 ? "y" : "ies");
    return TS_OK;
}

int skill_list(ToolCtx *t)
{
    return list_dir(t, SKILL_DIR, 1) < 0 ? TS_STOPPED : TS_OK;
}

int tool_memory_read(ToolCtx *t)
{
    unsigned got, i, n;
    unsigned long total = 0;
    int h, status, ended = 0;
    const char *p = mem_path(t, &status);

    if (p == NULL)
        return status;
    if (_dos_open(p, 0, &h))
        return tool_fail(t, "no such memory");
    while (!ended && _dos_read(h, buf, sizeof(buf) - 1, &got) == 0 && got > 0) {
        for (i = n = 0; i < got; i++) {    /* drop CRs, stop at a DOS EOF mark */
            if (buf[i] == 0x1A) {
                ended = 1;
                break;
            }
            if (buf[i] != '\r')
                buf[n++] = buf[i];
        }
        buf[n] = 0;
        tout_puts(t, buf);
        total += got;
    }
    _dos_close(h);
    sprintf(t->summary, "%lu bytes", total);
    return TS_OK;
}

int tool_memory_write(ToolCtx *t)
{
    const char *text = targ("text");
    unsigned n = 0, wrote;
    unsigned long total = 0;
    int h, status, err = 0;
    const char *p = mem_path(t, &status);

    if (p == NULL)
        return status;
    if (text == NULL)
        return tool_fail(t, "missing text");
    if (strlen(text) > MEM_MAX)
        return tool_fail(t, "too long: a memory holds at most 4000 bytes");
    {
        union REGS r;                  /* mkdir C:\MEMORY; fails harmlessly if it exists */
        struct SREGS s;
        segread(&s);
        r.h.ah = 0x39;
        s.ds = FP_SEG(MEM_DIR);
        r.x.dx = FP_OFF(MEM_DIR);
        intdosx(&r, &r, &s);
    }
    if (_dos_creat(p, _A_NORMAL, &h))
        return tool_fail(t, "can't create the memory file (disk full or read-only?)");
    for (;; text++) {                  /* newlines become CRLF */
        if (*text == '\n')
            buf[n++] = '\r';
        if (*text && *text != '\r')
            buf[n++] = *text;
        if (n >= sizeof(buf) - 2 || !*text) {
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
    sprintf(t->summary, "saved, %lu bytes", total);
    tout_printf(t, "saved %lu bytes to %s\n", total, p);
    return TS_OK;
}

/* (here, not in toolapi.c: CLAWDOS.EXE links that and never ends a list) */
void tout_end_list(ToolCtx *t, const char *more)
{
    unsigned need = strlen(more) + 1;

    if (!t->truncated)
        return;
    while (t->len > 0 && (t->out[t->len - 1] != '\n' || t->len + need > TOOL_OUT_MAX))
        t->len--;
    memcpy(t->out + t->len, more, need - 1);
    t->len += need;
    t->out[t->len - 1] = '\n';
    t->out[t->len] = 0;
}
