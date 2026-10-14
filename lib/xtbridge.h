/* Claude-XT bridge client: talks the XTBridge line protocol (docs/PROTOCOL.md)
 * over one mTCP socket. Built on mTCP, so programs using it are GPLv3.
 *
 * Typical use:
 *   XTBridge::startStack();                 // packet driver + mTCP
 *   XTBridge b; b.connect("10.0.2.2", 8023);
 *   b.send("MSG", "hello");
 *   loop { if (b.poll() > 0) handle(b.verb(), b.payload()); ... }
 */
#ifndef CXT_XTBRIDGE_H
#define CXT_XTBRIDGE_H

#include "types.h"

#define XTBRIDGE_PORT     8023
#define XTBRIDGE_LINE_MAX 1024

class TcpSocket;

class XTBridge {
  public:
    static volatile uint8_t breakPressed;      /* set by Ctrl-Break */

    /* Read MTCPCFG, bring up the TCP/IP stack. Prints errors to stderr. */
    static int8_t startStack(void);
    static void   stopStack(void);

    /* Our IP address as "a.b.c.d" (valid after startStack). buf >= 16 bytes. */
    static void localAddress(char *buf);

    /* Look up an app setting (e.g. "CLAUDE_BRIDGE") in the mTCP config file. */
    static int8_t configValue(const char *key, char *val, uint16_t len);

    XTBridge();

    /* Resolve and connect; host may be "name", "a.b.c.d" or "host:port".
       Blocks for up to ~10 s per step; returns 0 on success. errorText()
       describes failures. */
    int8_t connect(const char *host, uint16_t port);
    void   close(void);
    int    isConnected(void) { return sock != 0 && !dead; }

    /* Service the network. Returns 1 when a line is ready (see verb()/payload(),
       valid until the next poll), 0 when nothing is ready, -1 when the
       connection is gone. */
    int poll(void);
    const char *verb(void) { return lineVerb; }
    const char *payload(void) { return linePayload; }

    /* Send "VERB payload\n". Returns 0 on success. */
    int8_t send(const char *verb, const char *payload = 0);

    const char *errorText(void) { return err; }

  private:
    TcpSocket *sock;
    uint8_t dead;                     /* poll() reported the connection gone */
    char  rx[XTBRIDGE_LINE_MAX];
    uint16_t rxLen;
    uint16_t consumed;
    char *lineVerb;
    char *linePayload;
    const char *err;

    int takeLine(void);
};

#endif
