/* HTTP response head parsing: the status line and the headers we use. */
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
} HttpHead;

/* Parse "HTTP/1.1 200 OK\r\nName: value\r\n...\r\n\r\n" (the head only,
   NUL-terminated, ending in the blank line). 0 ok, -1 malformed. */
int http_parse_head(const char *head, HttpHead *h);

#ifdef __cplusplus
}
#endif

#endif
