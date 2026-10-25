/* Claude-XT bridge client: see xtbridge.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "utils.h"
#include "timer.h"
#include "packet.h"
#include "arp.h"
#include "udp.h"
#include "dns.h"
#include "tcp.h"
#include "tcpsockm.h"

#include "xtbridge.h"

/* How long one line may take to go out. WiFi stalls on the real XT last tens
   of seconds and then recover; giving up sooner drops a link that would have
   carried on, and loses whatever tool result was being sent. */
#define TIMEOUT_TICKS (30 * 18ul)

volatile uint8_t XTBridge::breakPressed = 0;

static void __interrupt __far ctrlBreakHandler(void) { XTBridge::breakPressed = 1; }
static void __interrupt __far ctrlCHandler(void) { }

int8_t XTBridge::startStack(void)
{
    if (Utils::parseEnv() != 0)
        return -1;
    if (Utils::initStack(1, TCP_SOCKET_RING_SIZE, ctrlBreakHandler, ctrlCHandler)) {
        fprintf(stderr, "Failed to initialize TCP/IP\n");
        return -1;
    }
    return 0;
}

void XTBridge::stopStack(void)
{
    Utils::endStack();
}

void XTBridge::localAddress(char *buf)
{
    sprintf(buf, "%u.%u.%u.%u", MyIpAddr[0], MyIpAddr[1], MyIpAddr[2], MyIpAddr[3]);
}

int8_t XTBridge::configValue(const char *key, char *val, uint16_t len)
{
    int8_t rc;

    val[0] = 0;
    if (Utils::openCfgFile() == NULL)
        return -1;
    rc = Utils::getAppValue(key, val, len);
    Utils::closeCfgFile();
    return rc;
}

XTBridge::XTBridge() : sock(NULL), dead(0), rxLen(0), consumed(0), err("") { }

int8_t XTBridge::connect(const char *hostSpec, uint16_t port)
{
    char host[80];
    IpAddr_t addr;
    char *colon;

    strncpy(host, hostSpec, sizeof(host) - 1);
    host[sizeof(host) - 1] = 0;
    colon = strchr(host, ':');
    if (colon) {
        *colon = 0;
        port = (uint16_t)atoi(colon + 1);
    }

    if (Dns::resolve(host, addr, 1) < 0) {
        err = "Can't look up the bridge host name";
        return -1;
    }
    clockTicks_t start = TIMER_GET_CURRENT();
    while (Dns::isQueryPending()) {
        if (breakPressed || Timer_diff(start, TIMER_GET_CURRENT()) > TIMER_TICKS_PER_SEC * 10) {
            err = "Timed out looking up the bridge host name";
            return -1;
        }
        Utils::processPackets();
        Dns::drivePendingQuery();
    }
    if (Dns::resolve(host, addr, 0) != 0) {
        err = "Can't look up the bridge host name";
        return -1;
    }

    sock = TcpSocketMgr::getSocket();
    if (sock == NULL) {
        err = "Out of sockets";
        return -1;
    }
    sock->setRecvBuffer(4096);
    uint16_t localPort = 2048 + (uint16_t)(TIMER_GET_CURRENT() % 20000);
    if (sock->connect(localPort, addr, port, 10000)) {
        err = "Can't connect to the bridge (is xtbridge.py running?)";
        TcpSocketMgr::freeSocket(sock);
        sock = NULL;
        return -1;
    }
    rxLen = consumed = 0;
    dead = 0;
    return 0;
}

void XTBridge::close(void)
{
    if (sock) {
        sock->close();
        TcpSocketMgr::freeSocket(sock);
        sock = NULL;
    }
}

/* If rx holds a complete line, split it into verb/payload and return 1. */
int XTBridge::takeLine(void)
{
    char *nl = (char *)memchr(rx, '\n', rxLen);

    if (nl == NULL) {
        if (rxLen < sizeof(rx) - 1)
            return 0;
        nl = rx + rxLen - 1;              /* overlong: deliver what we have */
    }
    *nl = 0;
    consumed = (uint16_t)(nl - rx) + 1;
    if (nl > rx && nl[-1] == '\r')
        nl[-1] = 0;

    lineVerb = rx;
    char *sp = strchr(rx, ' ');
    if (sp) {
        *sp = 0;
        linePayload = sp + 1;
    } else {
        linePayload = rx + strlen(rx);    /* empty string */
    }
    return 1;
}

int XTBridge::poll(void)
{
    if (sock == NULL)
        return -1;
    if (dead)                             /* a send failed: report it here */
        return -1;
    if (consumed) {                       /* drop the line handed out last time */
        rxLen -= consumed;
        memmove(rx, rx + consumed, rxLen);
        consumed = 0;
    }
    if (takeLine())
        return 1;

    Utils::processPackets();
    int16_t n = sock->recv((uint8_t *)rx + rxLen, sizeof(rx) - 1 - rxLen);
    if (n < 0) {
        err = "Connection error";
        dead = 1;
        return -1;
    }
    rxLen += n;
    if (takeLine())
        return 1;
    if (n == 0 && sock->isRemoteClosed()) {
        err = "The bridge closed the connection";
        dead = 1;
        return -1;
    }
    return 0;
}

int8_t XTBridge::send(const char *verb, const char *payload)
{
    static char tx[XTBRIDGE_LINE_MAX + 16];
    uint16_t len, off = 0;

    if (sock == NULL || dead)
        return -1;
    len = strlen(verb);
    memcpy(tx, verb, len);
    if (payload && *payload) {
        uint16_t plen = strlen(payload);
        if (plen > XTBRIDGE_LINE_MAX - len - 2)
            plen = XTBRIDGE_LINE_MAX - len - 2;
        tx[len++] = ' ';
        memcpy(tx + len, payload, plen);
        len += plen;
    }
    tx[len++] = '\n';

    clockTicks_t start = TIMER_GET_CURRENT();
    while (off < len) {
        int16_t n = sock->send((uint8_t *)tx + off, len - off);
        /* A line that didn't go out (or only partly) leaves the bridge and us
           out of step: the link is no use any more, so say so and let the
           caller reconnect rather than carry on as if it had been sent. */
        if (n < 0) {
            err = "Send failed";
            dead = 1;
            return -1;
        }
        off += n;
        Utils::processPackets();
        if (Timer_diff(start, TIMER_GET_CURRENT()) > TIMEOUT_TICKS) {
            err = "Send timed out (the network stalled)";
            dead = 1;
            return -1;
        }
    }
    return 0;
}

void XTBridge::service(void)
{
    Utils::processPackets();
}

void XTBridge::counters(Counters *c)
{
    c->sent = Packets_sent;
    c->received = Packets_received;
    c->retransmitted = Tcp::Packets_Retransmitted;
    c->sendErrors = Packets_send_errs;
    c->dropped = Packets_dropped;
}
