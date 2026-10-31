/* ClawDOS networking: the mTCP stack and one TCP connection. Built on mTCP, so
 * programs using it are GPLv3.
 */
#ifndef CLAWDOS_NET_H
#define CLAWDOS_NET_H

#include "types.h"

class TcpSocket;

class Net {
  public:
    static volatile uint8_t breakPressed;      /* set by Ctrl-Break */

    /* Read MTCPCFG, bring up the TCP/IP stack. Prints errors to stderr. */
    static int8_t startStack(void);
    static void   stopStack(void);

    /* Let the stack send what it has queued and take in new packets. Call it
       at least every few seconds while busy elsewhere. */
    static void   service(void);

    /* Our IP address as "a.b.c.d" (valid after startStack). buf >= 16 bytes. */
    static void localAddress(char *buf);

    /* mTCP's running totals, for working out what the network did when a link dies. */
    struct Counters {
        uint32_t sent, received, retransmitted, sendErrors, dropped;
    };
    static void counters(Counters *c);

    /* Look up an app setting in the mTCP config file. */
    static int8_t configValue(const char *key, char *val, uint16_t len);
};

class NetConn {
  public:
    NetConn();

    /* Resolve and connect; host may be "name", "a.b.c.d" or "host:port" (which
       overrides port). Blocks up to ~10 s per step; 0 on success. */
    int8_t connect(const char *host, uint16_t port);
    void   close(void);
    int    isConnected(void) { return sock != 0 && !dead; }

    /* Send all n bytes (waits up to 30 s for the network); 0 ok, -1 the link is gone. */
    int    send(const char *p, unsigned n);
    /* Bytes received (>0), 0 nothing yet, -1 the link is gone (see remoteClosed()). */
    int    recv(char *buf, unsigned max);
    int    remoteClosed(void) { return closedByPeer; }

    const char *errorText(void) { return err; }

  private:
    TcpSocket *sock;
    uint8_t dead;
    uint8_t closedByPeer;
    const char *err;
};

#endif
