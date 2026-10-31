/* Conversation history on disk, see history.h. */
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <dos.h>
#include "history.h"

#define COPY_BUF 512

static int fd = -1;
static long end;                /* bytes in the file */
static long boundary;           /* end of the last complete message */
static long turnStart;
static int turnMessages;
static int needSep;             /* at a boundary in a non-empty file: next write gets ",\r\n" */
static char savedTail[8];       /* what hist_reopen_last took off, so a rollback can put it back */
static unsigned savedTailLen;
static long savedTailAt;

/* Find the end of the last complete top-level object in the file. */
static long scanBoundary(void)
{
    char *buf = (char *)malloc(COPY_BUF);
    long pos = 0, last = 0;
    int depth = 0, inStr = 0, esc = 0, n;

    if (!buf)
        return 0;
    lseek(fd, 0, SEEK_SET);
    while ((n = read(fd, buf, COPY_BUF)) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            char c = buf[i];
            pos++;
            if (inStr) {
                if (esc) esc = 0;
                else if (c == '\\') esc = 1;
                else if (c == '"') inStr = 0;
            } else if (c == '"') {
                inStr = 1;
            } else if (c == '{' || c == '[') {
                depth++;
            } else if (c == '}' || c == ']') {
                if (--depth == 0)
                    last = pos;
            }
        }
    }
    free(buf);
    return last;
}

int hist_open(const char *path)
{
    hist_close();
    fd = open(path, O_RDWR | O_CREAT | O_BINARY, S_IREAD | S_IWRITE);
    if (fd < 0)
        return -1;
    end = lseek(fd, 0, SEEK_END);
    boundary = end ? scanBoundary() : 0;
    if (boundary != end) {
        chsize(fd, boundary);
        end = boundary;
    }
    turnStart = boundary;
    turnMessages = 0;
    needSep = end > 0;
    return 0;
}

void hist_close(void)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
    end = boundary = turnStart = 0;
    turnMessages = 0;
    needSep = 0;
}

long hist_size(void)
{
    return end;
}

int hist_write(const char *p, unsigned n)
{
    if (fd < 0)
        return -1;
    lseek(fd, end, SEEK_SET);
    if (needSep) {
        if (write(fd, ",\r\n", 3) != 3)
            return -1;
        end += 3;
        needSep = 0;
    }
    if (n && write(fd, p, n) != (int)n)
        return -1;
    end += n;
    return 0;
}

int hist_puts(const char *z)
{
    return hist_write(z, strlen(z));
}

/* INT 21h AH=68h: flush the file's buffers and directory entry, so the bytes
   survive a crash or power cut (DOS only updates the size on close otherwise). */
static void commit(void)
{
    union REGS r;
    r.h.ah = 0x68;
    r.x.bx = (unsigned)fd;
    intdos(&r, &r);
}

int hist_message_done(void)
{
    boundary = end;
    needSep = end > 0;
    turnMessages++;
    commit();
    return 0;
}

void hist_turn_begin(void)
{
    turnStart = boundary;
    turnMessages = 0;
    savedTailLen = 0;
}

static int truncateTo(long pos)
{
    if (fd < 0)
        return -1;
    if (chsize(fd, pos))
        return -1;
    end = boundary = pos;
    needSep = end > 0;
    return 0;
}

int hist_rollback_message(void)
{
    return truncateTo(boundary);
}

int hist_rollback_turn(void)
{
    turnMessages = 0;
    if (savedTailLen && savedTailAt + (long)savedTailLen == turnStart) {
        /* the turn reopened the message before it: put its closing bytes back */
        if (truncateTo(savedTailAt))
            return -1;
        lseek(fd, savedTailAt, SEEK_SET);
        if (write(fd, savedTail, savedTailLen) != (int)savedTailLen)
            return -1;
        end = boundary = turnStart;
        needSep = 1;
        savedTailLen = 0;
        return 0;
    }
    return truncateTo(turnStart);
}

int hist_turn_messages(void)
{
    return turnMessages;
}

int hist_reopen_last(unsigned n)
{
    if (fd < 0 || (long)n > end || n > sizeof savedTail)
        return -1;
    lseek(fd, end - (long)n, SEEK_SET);
    if (read(fd, savedTail, n) != (int)n)
        return -1;
    savedTailLen = n;
    savedTailAt = end - (long)n;
    end -= n;
    if (chsize(fd, end))
        return -1;
    boundary = end;
    needSep = 0;
    return 0;
}

int hist_copy(Sink *s)
{
    char *buf;
    long left = end;
    int rc = 0;

    if (fd < 0)
        return -1;
    buf = (char *)malloc(COPY_BUF);
    if (!buf)
        return -1;
    lseek(fd, 0, SEEK_SET);
    while (left > 0) {
        unsigned want = left > COPY_BUF ? COPY_BUF : (unsigned)left;
        int n = read(fd, buf, want);
        if (n <= 0) {
            rc = -1;
            break;
        }
        if (sink_put(s, buf, (unsigned)n)) {
            rc = -1;
            break;
        }
        left -= n;
    }
    free(buf);
    return rc;
}
