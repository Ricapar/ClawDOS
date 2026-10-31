/* HTTP response head parsing (status line and the headers we use) and the
 * RFC 1123 Date header, which gives the DOS PC its clock.
 */
#ifndef CLAWDOS_HTTPP_H
#define CLAWDOS_HTTPP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HttpHead {
    int status;                 /* 200, 429, ...; 0 if the status line is bad */
    long contentLength;         /* -1 if absent */
    int chunked;                /* Transfer-Encoding: chunked */
    int close;                  /* Connection: close */
    int retryAfter;             /* seconds, 0 if absent or not a number */
    char date[32];              /* raw Date value, "" if absent */
} HttpHead;

/* Parse "HTTP/1.1 200 OK\r\nName: value\r\n...\r\n\r\n" (the head only,
   NUL-terminated, ending in the blank line). 0 ok, -1 malformed. */
int http_parse_head(const char *head, HttpHead *h);

/* "Fri, 31 Oct 2026 14:03:07 GMT" -> fields. 0 ok, -1 bad. */
int http_parse_date(const char *date, int *y, int *mo, int *d, int *h, int *mi, int *s);

/* Shift a date/time by `hours` (may be negative), carrying days, months, years. */
void http_shift_time(int *y, int *mo, int *d, int *h, int hours);

#ifdef __cplusplus
}
#endif

#endif
