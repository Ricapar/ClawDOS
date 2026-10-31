/* HTTP head and Date parsing, see httpp.h. */
#include <stdlib.h>
#include <string.h>
#include "httpp.h"

/* The value of header `name` in head (case-insensitive), copied to val; 0 if present. */
static int header(const char *head, const char *name, char *val, unsigned max)
{
    unsigned nlen = strlen(name);
    const char *p = strchr(head, '\n');
    while (p) {
        const char *line = ++p;
        if (*line == '\r' || *line == '\n' || *line == 0)
            break;
        if (!strnicmp(line, name, nlen) && line[nlen] == ':') {
            const char *v = line + nlen + 1, *e;
            unsigned n;
            while (*v == ' ' || *v == '\t')
                v++;
            e = strpbrk(v, "\r\n");
            n = e ? (unsigned)(e - v) : strlen(v);
            if (n >= max)
                n = max - 1;
            memcpy(val, v, n);
            val[n] = 0;
            return 0;
        }
        p = strchr(line, '\n');
    }
    return -1;
}

int http_parse_head(const char *head, HttpHead *h)
{
    char val[40];
    const char *p;

    memset(h, 0, sizeof *h);
    h->contentLength = -1;
    if (!strstr(head, "\r\n\r\n") && !strstr(head, "\n\n"))
        return -1;
    if (strncmp(head, "HTTP/", 5))
        return -1;
    p = strchr(head, ' ');
    if (!p || p[1] < '1' || p[1] > '5')
        return -1;
    h->status = atoi(p + 1);
    if (h->status < 100)
        return -1;
    if (!header(head, "Content-Length", val, sizeof val))
        h->contentLength = atol(val);
    if (!header(head, "Transfer-Encoding", val, sizeof val))
        h->chunked = strnicmp(val, "chunked", 7) == 0;
    if (!header(head, "Connection", val, sizeof val))
        h->close = strnicmp(val, "close", 5) == 0;
    if (!header(head, "Retry-After", val, sizeof val))
        h->retryAfter = atoi(val);
    header(head, "Date", h->date, sizeof h->date);
    return 0;
}

static int monthOf(const char *s)
{
    static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;
    for (i = 0; i < 12; i++)
        if (!strnicmp(s, names + i * 3, 3))
            return i + 1;
    return 0;
}

int http_parse_date(const char *date, int *y, int *mo, int *d, int *h, int *mi, int *s)
{
    const char *p = strchr(date, ',');     /* "Fri, " */
    char mon[4];
    if (!p)
        return -1;
    p++;
    while (*p == ' ')
        p++;
    *d = atoi(p);
    p = strchr(p, ' ');
    if (!p || *d < 1 || *d > 31)
        return -1;
    strncpy(mon, p + 1, 3);
    mon[3] = 0;
    if ((*mo = monthOf(mon)) == 0)
        return -1;
    p += 5;                                /* past " Oct " */
    *y = atoi(p);
    p = strchr(p, ' ');
    if (!p || *y < 1970)
        return -1;
    p++;
    *h = atoi(p);
    if (strlen(p) < 8 || p[2] != ':' || p[5] != ':')
        return -1;
    *mi = atoi(p + 3);
    *s = atoi(p + 6);
    return 0;
}

static int daysIn(int y, int mo)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;
    return days[mo - 1];
}

void http_shift_time(int *y, int *mo, int *d, int *h, int hours)
{
    *h += hours;
    while (*h < 0) {
        *h += 24;
        if (--*d < 1) {
            if (--*mo < 1) { *mo = 12; --*y; }
            *d = daysIn(*y, *mo);
        }
    }
    while (*h >= 24) {
        *h -= 24;
        if (++*d > daysIn(*y, *mo)) {
            *d = 1;
            if (++*mo > 12) { *mo = 1; ++*y; }
        }
    }
}
