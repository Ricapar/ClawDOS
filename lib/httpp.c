/* HTTP head parsing, see httpp.h. */
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
    return 0;
}
