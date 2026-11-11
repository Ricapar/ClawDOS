/* Conversation history on disk, see history.h. */
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <dos.h>
#include "history.h"

#define COPY_BUF 2048           /* reads of the scan and of hist_copy (heap, freed after) */
#define WBUF_MAX 2048           /* the write-behind buffer */

static int fd = -1;
static long end;                /* bytes in the file, those still in wbuf included */
static long boundary;           /* end of the last complete message */
static long turnStart;
static int turnMessages;
static int needSep;             /* at a boundary in a non-empty file: next write gets ",\r\n" */
static char savedTail[8];       /* what hist_reopen_last took off, so a rollback can put it back */
static unsigned savedTailLen;
static long savedTailAt;
/* Writes collect in wbuf and reach DOS a buffer at a time: a tool result
   escaped a character at a time, or a reply streamed a few bytes per delta,
   would otherwise be a DOS write per piece. wbuf holds the file's last
   `pending` bytes; the DOS file pointer is at end - pending unless seekNeeded. */
static char *wbuf;
static unsigned pending;
static int seekNeeded;
/* A write failed since the last cut or boundary: the message has a hole in
   it, so nothing more is taken and hist_message_done fails until a rollback
   cuts the file back. */
static int writeFailed;
/* The smallest size the file has been cut to since hist_low_reset: a message
   thread holds the file up to some offset, and is only good while nothing at
   or before it has been cut away (and maybe written differently). */
static long lowWater;

#ifdef DOSUNIT
int hist_test_scan_reads = -1;
int hist_test_writes;
int hist_test_write_ok = -1;
int hist_test_write_bad;

/* dosunit.c's full disk: after hist_test_write_ok good writes, the next
   hist_test_write_bad writes fail (0: all of them, until the test sets
   hist_test_write_ok back to -1). */
static int dosWrite(const char *p, unsigned n)
{
    hist_test_writes++;
    if (hist_test_write_ok == 0) {
        if (hist_test_write_bad > 0 && --hist_test_write_bad == 0)
            hist_test_write_ok = -1;
        return n > 1 ? write(fd, p, n / 2) : -1;   /* a short write, as DOS gives on a full disk */
    }
    if (hist_test_write_ok > 0)
        hist_test_write_ok--;
    return write(fd, p, n);
}

/* dosunit.c's way to a scan that cannot finish: after hist_test_scan_reads
   good reads, one fails as a bad sector would. */
static int scanRead(char *buf)
{
    if (hist_test_scan_reads == 0) {
        hist_test_scan_reads = -1;
        return -1;
    }
    if (hist_test_scan_reads > 0)
        hist_test_scan_reads--;
    return read(fd, buf, COPY_BUF);
}
#else
#define scanRead(buf) read(fd, buf, COPY_BUF)
#define dosWrite(p, n) write(fd, p, n)
#endif

/* Write out what wbuf holds. On a failure those bytes are gone from `end`
   too, as an unbuffered write that failed would never have counted them. */
static int flushBuf(void)
{
    unsigned n = pending;

    if (n == 0)
        return 0;
    pending = 0;
    if (seekNeeded) {
        if (lseek(fd, end - (long)n, SEEK_SET) < 0) {
            end -= n;
            writeFailed = 1;
            return -1;
        }
        seekNeeded = 0;
    }
    if (dosWrite(wbuf, n) != (int)n) {
        end -= n;
        seekNeeded = 1;             /* a short write left the pointer who knows where */
        writeFailed = 1;
        return -1;
    }
    return 0;
}

/* Find the end of the last complete top-level object in the file; -1 if the
   file can't be read to its end (no memory, a read error). */
static long scanBoundary(void)
{
    char *buf = (char *)malloc(COPY_BUF);
    long pos = 0, last = 0;
    int depth = 0, inStr = 0, esc = 0, n;

    if (!buf)
        return -1;
    lseek(fd, 0, SEEK_SET);
    while ((n = scanRead(buf)) > 0) {
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
    return n < 0 ? -1 : last;
}

int hist_open(const char *path)
{
    hist_close();
    if (!wbuf && (wbuf = (char *)malloc(WBUF_MAX)) == NULL)
        return -1;
    fd = open(path, O_RDWR | O_CREAT | O_BINARY, S_IREAD | S_IWRITE);
    if (fd < 0)
        return -1;
    end = lseek(fd, 0, SEEK_END);
    boundary = end ? scanBoundary() : 0;
    if (boundary < 0) {               /* never cut a file we couldn't read through */
        hist_close();
        return -1;
    }
    if (boundary != end) {
        chsize(fd, boundary);
        end = boundary;
    }
    seekNeeded = 1;
    turnStart = boundary;
    turnMessages = 0;
    needSep = end > 0;
    lowWater = end;
    return 0;
}

void hist_close(void)
{
    if (fd >= 0) {
        flushBuf();
        close(fd);
    }
    fd = -1;
    end = boundary = turnStart = lowWater = 0;
    turnMessages = 0;
    needSep = 0;
    pending = 0;
    writeFailed = 0;
}

long hist_size(void)
{
    return end;
}

long hist_low(void)
{
    return lowWater;
}

void hist_low_reset(void)
{
    lowWater = end;
}

static void cutTo(long pos)
{
    if (pos < lowWater)
        lowWater = pos;
}

/* Into wbuf, writing it out each time it fills. */
static int put(const char *p, unsigned n)
{
    if (writeFailed)
        return -1;
    while (n) {
        unsigned take;
        if (pending == WBUF_MAX && flushBuf())
            return -1;
        take = WBUF_MAX - pending;
        if (take > n)
            take = n;
        memcpy(wbuf + pending, p, take);
        pending += take;
        end += take;
        p += take;
        n -= take;
    }
    return 0;
}

int hist_write(const char *p, unsigned n)
{
    if (fd < 0)
        return -1;
    if (needSep) {
        needSep = 0;
        if (put(",\r\n", 3))
            return -1;
    }
    return put(p, n);
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
    if (fd >= 0 && (flushBuf() || writeFailed))
        return -1;                    /* not all on disk: no boundary, a rollback takes it away */
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

/* Cut the file at pos. The bytes before pos are on disk already (every
   rollback point is a boundary, written out by hist_message_done), so what
   wbuf holds lies past pos and is dropped unwritten. */
static int truncateTo(long pos)
{
    if (fd < 0)
        return -1;
    if (pos > end - (long)pending && flushBuf())
        return -1;
    end -= pending;
    pending = 0;
    seekNeeded = 1;
    cutTo(pos);
    if (chsize(fd, pos))
        return -1;
    writeFailed = 0;                  /* whatever a failed write left is cut off */
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
        if (dosWrite(savedTail, savedTailLen) != (int)savedTailLen)
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

void hist_turn_get(HistTurn *t)
{
    t->start = turnStart;
    t->tailLen = savedTailLen;
    t->tailAt = savedTailAt;
    memcpy(t->tail, savedTail, sizeof t->tail);
}

int hist_turn_set(const HistTurn *t)
{
    if (fd < 0 || t->start < 0 || t->start > boundary || t->tailLen > sizeof savedTail
        || (t->tailLen && (t->tailAt < 0 || t->tailAt + (long)t->tailLen != t->start)))
        return -1;
    turnStart = t->start;
    savedTailLen = t->tailLen;
    savedTailAt = t->tailAt;
    memcpy(savedTail, t->tail, sizeof savedTail);
    return 0;
}

int hist_reopen_last(unsigned n)
{
    if (fd < 0 || flushBuf() || (long)n > end || n > sizeof savedTail)
        return -1;
    seekNeeded = 1;
    lseek(fd, end - (long)n, SEEK_SET);
    if (read(fd, savedTail, n) != (int)n)
        return -1;
    savedTailLen = n;
    savedTailAt = end - (long)n;
    end -= n;
    cutTo(end);
    if (chsize(fd, end))
        return -1;
    boundary = end;
    needSep = 0;
    return 0;
}

int hist_copy(Sink *s)
{
    return hist_copy_from(s, 0);
}

int hist_copy_from(Sink *s, long from)
{
    char *buf;
    long left;
    int rc = 0;

    if (fd < 0 || from < 0 || from > end || flushBuf())
        return -1;
    left = end - from;
    buf = (char *)malloc(COPY_BUF);
    if (!buf)
        return -1;
    seekNeeded = 1;
    lseek(fd, from, SEEK_SET);
    while (left > 0) {
        unsigned want = left > COPY_BUF ? COPY_BUF : (unsigned)left;
        int n = read(fd, buf, want);
        if (n <= 0) {
            rc = -1;
            break;
        }
        if (sink_put(s, buf, (unsigned)n)) {
            rc = -2;
            break;
        }
        left -= n;
    }
    free(buf);
    return rc;
}
