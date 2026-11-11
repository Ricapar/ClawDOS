/* An HTTP/1.1 client connection over NetConn: one keep-alive connection to the
 * TLS tunnel, requests written through a Sink (so the body writer can stream
 * from disk), responses read as decoded body bytes (chunked, Content-Length or
 * read-to-close).
 */
#ifndef CLAWDOS_HTTP_H
#define CLAWDOS_HTTP_H

#include "types.h"
#include "net.h"
#include "sink.h"
#include "httpp.h"
#include "chunked.h"
#include "cfg.h"
#include "apireq.h"

#define HTTP_HEAD_MAX 4096        /* the API sends ~1.5 KB of rate-limit and CDN headers */
#define HTTP_RX_MAX   2048
#define HTTP_TX_MAX   512         /* staged outgoing bytes: one send */

class Http {
  public:
    Http();

    int8_t open(const char *hostport);                 /* "host[:port]", port default 8443; 0 ok */
    /* The buffers to use (HTTP_RX_MAX, HTTP_HEAD_MAX and HTTP_TX_MAX bytes),
       before open: the harness lends them to run programs between requests,
       when they hold nothing (lib/runmem.h), and hands them back before each.
       Without them open, sends and reads fail. */
    void   useBuffers(char *rxBuf, char *head, char *tx) { rx = rxBuf; headBuf = head; txBuf = tx; }
    void   close(void);
    int    isOpen(void) { return conn.isConnected(); }

    Sink  *sink(void) { return &sk; }                  /* writes are staged and go out in 512-byte pieces */
    int8_t flush(void);                                /* send what the sink still holds; 0 ok */
    int8_t sendHead(const Provider *p, const ApiFixed *f, unsigned long length);   /* api_head into the socket */
    void   beginResponse(void) { headLen = 0; }                 /* after writing a request by hand */

    /* Read the status line and headers (<= HTTP_HEAD_MAX) within `secs`; call
       again after a timeout, what arrived so far is kept.
       0 ok, -1 link lost, -2 timeout, -3 malformed. */
    int8_t readHead(HttpHead *h, int secs);
    int    headStarted(void) { return headLen > 0; }   /* any byte of the response yet? */
    /* Whether an idle connection is still usable: false once the far end has
       closed it (an idle keep-alive the API or the tunnel gave up on). */
    int    alive(void);
    /* Decoded body bytes: >0 bytes, 0 body finished, -1 link lost, -2 timeout. */
    int    read(char *buf, unsigned max, int secs);
    int    bodyDone(void) { return done; }

    void   service(void) { Net::service(); }
    const char *errorText(void) { return conn.errorText(); }
    uint16_t localPort(void) { return conn.localPort(); }

    /* Called every couple of ticks while waiting for the network and after
       every piece sent, so the caller can animate a spinner and read keys. */
    void (*idle)(void);

  private:
    NetConn conn;
    Sink sk;
    char *rx;                     /* raw bytes from the socket not yet consumed */
    unsigned rxLen, rxPos;
    char *headBuf;                /* the response head being collected */
    unsigned headLen;
    char *txBuf;                  /* staged outgoing bytes */
    unsigned txLen;
    HttpHead head;
    Chunked ch;
    long left;                    /* Content-Length bytes still to come */
    int done;
    char *out;                    /* where chunked payload goes during read() */
    unsigned outLen, outMax;

    unsigned long lastIdle;
    int fill(int secs);           /* more raw bytes; >0, 0 timeout, -1 gone */
    void tick(void);              /* call idle if 2 ticks have passed */
    static int sockPut(Sink *s, const char *p, unsigned n);
    static int chunkOut(void *ctx, const char *p, unsigned n);
};

#endif
