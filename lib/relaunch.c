/* ClawDOS relaunch files, see relaunch.h. DOS handle I/O: the launcher is
   small, and these files are written once per run. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include "relaunch.h"

static int writeFile(const char *path, const char *head, const char *body, unsigned n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
    int rc = 0;

    if (fd < 0)
        return -1;
    if (write(fd, head, strlen(head)) != (int)strlen(head))
        rc = -1;
    if (n && write(fd, body, n) != (int)n)
        rc = -1;
    close(fd);
    return rc;
}

/* The first nhead lines (CR stripped, truncated to maxes[i]) into heads[i];
   whatever follows goes to rest() in pieces. -1 if the file can't be opened. */
static int readParts(const char *path, int nhead, char *heads[], const unsigned maxes[],
                     void (*rest)(void *, const char *, unsigned), void *ctx)
{
    char buf[256];
    int fd = open(path, O_RDONLY | O_BINARY), n, line = 0;
    unsigned len = 0;

    if (fd < 0)
        return -1;
    heads[0][0] = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        int i = 0;
        while (i < n && line < nhead) {
            char c = buf[i++];
            if (c == '\n') {
                heads[line][len] = 0;
                line++;
                len = 0;
                if (line < nhead)
                    heads[line][0] = 0;
            } else if (c != '\r' && len + 1 < maxes[line]) {
                heads[line][len++] = c;
            }
        }
        if (i < n)
            rest(ctx, buf + i, n - i);
    }
    if (line < nhead)
        heads[line][len] = 0;
    close(fd);
    return 0;
}

static void toTool(void *ctx, const char *p, unsigned n)
{
    char piece[64];
    while (n) {
        unsigned k = n < sizeof piece - 1 ? n : sizeof piece - 1;
        memcpy(piece, p, k);
        piece[k] = 0;
        tout_puts((ToolCtx *)ctx, piece);
        p += k;
        n -= k;
    }
}

int relaunch_write_run(const char *head, const char *cmds)
{
    return writeFile(RELAUNCH_RUN, head, cmds, strlen(cmds));
}

int relaunch_read_run(char *cmds, unsigned max)
{
    int fd = open(RELAUNCH_RUN, O_RDONLY | O_BINARY), n;

    if (fd < 0)
        return -1;
    n = read(fd, cmds, max - 1);
    close(fd);
    cmds[n > 0 ? n : 0] = 0;
    return 0;
}

int relaunch_write_out(int status, unsigned long ticks, const ToolCtx *t)
{
    char head[144];
    sprintf(head, "status %d ticks %lu show %u %u\n%.64s\n", status, ticks, t->showFrom, t->showTo,
            t->summary);
    return writeFile(RELAUNCH_OUT, head, t->out, t->len);
}

int relaunch_read_out(ToolCtx *t, int *status, unsigned long *ticks)
{
    char line[64];
    char *heads[2];
    unsigned maxes[2];
    const char *p;

    heads[0] = line;
    maxes[0] = sizeof line;
    heads[1] = t->summary;
    maxes[1] = sizeof t->summary;
    if (readParts(RELAUNCH_OUT, 2, heads, maxes, toTool, t))
        return -1;
    *status = !strncmp(line, "status ", 7) ? atoi(line + 7) : TS_ERROR;
    p = strstr(line, "ticks ");
    *ticks = p ? strtoul(p + 6, NULL, 10) : 0;
    if ((p = strstr(line, "show ")) != NULL) {
        char *end;
        unsigned from = (unsigned)strtoul(p + 5, &end, 10), to = (unsigned)strtoul(end, NULL, 10);
        if (from <= to && to <= t->len) {
            t->showFrom = from;
            t->showTo = to;
        }
    }
    return 0;
}
