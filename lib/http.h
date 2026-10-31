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

#define HTTP_HEAD_MAX 2048
#define HTTP_RX_MAX   2048

class Http {
  public:
    Http();

    int8_t open(const char *hostport);                 /* "host[:port]", port default 8443; 0 ok */
    void   close(void);
    int    isOpen(void) { return conn.isConnected(); }

    Sink  *sink(void) { return &sk; }                  /* writes go to the socket */
    int8_t sendHead(const Provider *p, unsigned long length);   /* api_head into the socket */

    /* Read the status line and headers (<= HTTP_HEAD_MAX) within `secs`.
       0 ok, -1 link lost, -2 timeout, -3 malformed. */
    int8_t readHead(HttpHead *h, int secs);
    /* Decoded body bytes: >0 bytes, 0 body finished, -1 link lost, -2 timeout. */
    int    read(char *buf, unsigned max, int secs);
    int    bodyDone(void) { return done; }

    void   service(void) { Net::service(); }
    const char *errorText(void) { return conn.errorText(); }

  private:
    NetConn conn;
    Sink sk;
    char *rx;                     /* raw bytes from the socket not yet consumed */
    unsigned rxLen, rxPos;
    HttpHead head;
    Chunked ch;
    long left;                    /* Content-Length bytes still to come */
    int done;
    char *out;                    /* where chunked payload goes during read() */
    unsigned outLen, outMax;

    int fill(int secs);           /* more raw bytes; >0, 0 timeout, -1 gone */
    static int sockPut(Sink *s, const char *p, unsigned n);
    static int chunkOut(void *ctx, const char *p, unsigned n);
};

#endif
