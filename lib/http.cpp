/* HTTP client connection, see http.h. */

#include <stdlib.h>
#include <string.h>
#include "http.h"
#include "apireq.h"
#include "ticks.h"

#define HTTP_TX_MAX 512

Http::Http() : idle(NULL), rx(NULL), rxLen(0), rxPos(0), headBuf(NULL), headLen(0), txBuf(NULL), txLen(0),
               left(0), done(1), out(NULL), outLen(0), outMax(0), lastIdle(0)
{
    sk.put = sockPut;
    sk.ctx = this;
    sk.count = 0;
    memset(&head, 0, sizeof head);
}

void Http::tick(void)
{
    if (idle && ticks_elapsed(TICKS_NOW, lastIdle) >= 2) {
        lastIdle = TICKS_NOW;
        idle();
    }
}

/* Stage outgoing bytes; a full buffer goes out as one send (one byte per TCP
   send would be a packet per byte on an XT). */
int Http::sockPut(Sink *s, const char *p, unsigned n)
{
    Http *h = (Http *)s->ctx;
    if (!h->txBuf)
        h->txBuf = (char *)malloc(HTTP_TX_MAX);
    if (!h->txBuf)
        return -1;
    while (n) {
        unsigned room = HTTP_TX_MAX - h->txLen, take = n < room ? n : room;
        memcpy(h->txBuf + h->txLen, p, take);
        h->txLen += take;
        p += take;
        n -= take;
        if (h->txLen == HTTP_TX_MAX && h->flush())
            return -1;
    }
    return 0;
}

int8_t Http::flush(void)
{
    int rc = 0;
    if (txLen) {
        rc = conn.send(txBuf, txLen);
        txLen = 0;
        tick();
    }
    return (int8_t)rc;
}

int8_t Http::open(const char *hostport)
{
    if (!rx)
        rx = (char *)malloc(HTTP_RX_MAX);
    if (!rx)
        return -1;
    rxLen = rxPos = 0;
    txLen = 0;
    done = 1;
    return conn.connect(hostport, 8443);
}

int Http::alive(void)
{
    return conn.isConnected() && !conn.farEndClosed();
}

void Http::close(void)
{
    conn.close();
    rxLen = rxPos = 0;
    done = 1;
}

int8_t Http::sendHead(const Provider *p, const ApiFixed *f, unsigned long length)
{
    sk.count = 0;
    headLen = 0;
    return (int8_t)(api_head(&sk, p, f, length) ? -1 : 0);
}

/* Receive more raw bytes into rx (compacting first). */
int Http::fill(int secs)
{
    unsigned long start = TICKS_NOW;

    if (rxPos) {
        rxLen -= rxPos;
        memmove(rx, rx + rxPos, rxLen);
        rxPos = 0;
    }
    for (;;) {
        int n = conn.recv(rx + rxLen, HTTP_RX_MAX - rxLen);
        if (n < 0)
            return -1;
        if (n > 0) {
            rxLen += n;
            return n;
        }
        tick();
        if (ticks_elapsed(TICKS_NOW, start) >= SECS_TO_TICKS(secs))
            return 0;
    }
}

int8_t Http::readHead(HttpHead *h, int secs)
{
    char *buf;
    unsigned len = headLen;

    if (!headBuf)
        headBuf = (char *)malloc(HTTP_HEAD_MAX);
    buf = headBuf;
    if (!buf || !rx)
        return -1;
    for (;;) {
        while (rxPos < rxLen) {
            if (len >= HTTP_HEAD_MAX - 1)
                return -3;
            buf[len++] = rx[rxPos++];
            headLen = len;
            if (len >= 4 && !memcmp(buf + len - 4, "\r\n\r\n", 4)) {
                buf[len] = 0;
                if (http_parse_head(buf, &head))
                    return -3;
                *h = head;
                chunked_init(&ch);
                left = head.contentLength;
                done = (!head.chunked && left == 0) || (head.status == 204 || head.status == 304);
                return 0;
            }
        }
        int n = fill(secs);
        if (n < 0)
            return -1;
        if (n == 0)
            return -2;
    }
}

int Http::chunkOut(void *ctx, const char *p, unsigned n)
{
    Http *h = (Http *)ctx;
    if (h->outLen + n > h->outMax)
        n = h->outMax - h->outLen;
    memcpy(h->out + h->outLen, p, n);
    h->outLen += n;
    return 0;
}

int Http::read(char *buf, unsigned max, int secs)
{
    if (done)
        return 0;
    if (rxPos >= rxLen) {
        int n = fill(secs);
        if (n == 0)
            return -2;
        if (n < 0) {
            if (conn.remoteClosed() && !head.chunked && head.contentLength < 0) {
                done = 1;                 /* read-to-close body: that was the end */
                return 0;
            }
            return -1;
        }
    }
    unsigned avail = rxLen - rxPos;
    if (head.chunked) {
        unsigned take = avail < max ? avail : max;
        int rc;
        out = buf;
        outLen = 0;
        outMax = max;
        rc = chunked_feed(&ch, rx + rxPos, take, chunkOut, this);
        rxPos += take;
        if (rc < 0)
            return -1;
        if (rc == 1)
            done = 1;
        if (outLen == 0 && !done)
            return read(buf, max, secs);  /* only chunk framing so far: keep going */
        return (int)outLen;
    }
    unsigned take = avail < max ? avail : max;
    if (head.contentLength >= 0 && (long)take > left)
        take = (unsigned)left;
    memcpy(buf, rx + rxPos, take);
    rxPos += take;
    if (head.contentLength >= 0) {
        left -= take;
        if (left <= 0)
            done = 1;
    }
    return (int)take;
}
