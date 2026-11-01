/* mTCP stack and connection, see net.h. */

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

#include "net.h"
#include "ticks.h"

/* How long a send may take. WiFi stalls on the real PC last tens of seconds
   and then recover; giving up sooner drops a link that would have carried on. */
#define SEND_TIMEOUT_TICKS (30 * 18ul)

volatile uint8_t Net::breakPressed = 0;

static void __interrupt __far netBreakHandler(void) { Net::breakPressed = 1; }
static void __interrupt __far netCtrlCHandler(void) { }

int8_t Net::startStack(void)
{
    if (Utils::parseEnv() != 0)
        return -1;
    if (Utils::initStack(1, TCP_SOCKET_RING_SIZE, netBreakHandler, netCtrlCHandler)) {
        fprintf(stderr, "Failed to initialize TCP/IP\n");
        return -1;
    }
    return 0;
}

void Net::stopStack(void)
{
    Utils::endStack();
}

void Net::service(void)
{
    Utils::processPackets();
}

void Net::localAddress(char *buf)
{
    sprintf(buf, "%u.%u.%u.%u", MyIpAddr[0], MyIpAddr[1], MyIpAddr[2], MyIpAddr[3]);
}

void Net::counters(Counters *c)
{
    c->sent = Packets_sent;
    c->received = Packets_received;
    c->retransmitted = Tcp::Packets_Retransmitted;
    c->sendErrors = Packets_send_errs;
    c->dropped = Packets_dropped;
}

int8_t Net::configValue(const char *key, char *val, uint16_t len)
{
    int8_t rc;

    val[0] = 0;
    if (Utils::openCfgFile() == NULL)
        return -1;
    rc = Utils::getAppValue(key, val, len);
    Utils::closeCfgFile();
    return rc;
}

NetConn::NetConn() : sock(NULL), dead(0), closedByPeer(0), srcPort(0), err("") { }

int8_t NetConn::connect(const char *hostSpec, uint16_t port)
{
    char host[80];
    IpAddr_t addr;
    char *colon;

    close();
    strncpy(host, hostSpec, sizeof(host) - 1);
    host[sizeof(host) - 1] = 0;
    colon = strchr(host, ':');
    if (colon) {
        *colon = 0;
        port = (uint16_t)atoi(colon + 1);
    }

    if (Dns::resolve(host, addr, 1) < 0) {
        err = "Can't look up the host name";
        return -1;
    }
    clockTicks_t start = TIMER_GET_CURRENT();
    while (Dns::isQueryPending()) {
        if (Net::breakPressed || Timer_diff(start, TIMER_GET_CURRENT()) > TIMER_TICKS_PER_SEC * 10) {
            err = "Timed out looking up the host name";
            return -1;
        }
        Utils::processPackets();
        Dns::drivePendingQuery();
    }
    if (Dns::resolve(host, addr, 0) != 0) {
        err = "Can't look up the host name";
        return -1;
    }

    sock = TcpSocketMgr::getSocket();
    if (sock == NULL) {
        err = "Out of sockets";
        return -1;
    }
    sock->setRecvBuffer(4096);
    /* Our port comes from the time of day, not from mTCP's tick counter: that
       starts at 0 in every process, so a relaunched CLAWDOS would reuse the
       port its predecessor had just closed, and the far end ignores a SYN on a
       connection it still remembers (the resume then failed to connect). */
    srcPort = 2048 + (uint16_t)(TICKS_NOW % 20000);
    if (sock->connect(srcPort, addr, port, 10000)) {
        err = "Can't connect (is the tunnel running?)";
        TcpSocketMgr::freeSocket(sock);
        sock = NULL;
        return -1;
    }
    dead = 0;
    closedByPeer = 0;
    return 0;
}

void NetConn::close(void)
{
    if (sock) {
        sock->close();
        TcpSocketMgr::freeSocket(sock);
        sock = NULL;
    }
}

int NetConn::send(const char *p, unsigned n)
{
    unsigned off = 0;
    clockTicks_t start = TIMER_GET_CURRENT();

    if (sock == NULL || dead)
        return -1;
    while (off < n) {
        int16_t sent = sock->send((uint8_t *)p + off, n - off);
        if (sent < 0) {
            err = "Send failed";
            dead = 1;
            return -1;
        }
        off += sent;
        Utils::processPackets();
        if (Timer_diff(start, TIMER_GET_CURRENT()) > SEND_TIMEOUT_TICKS) {
            err = "Send timed out (the network stalled)";
            dead = 1;
            return -1;
        }
    }
    return 0;
}

int NetConn::farEndClosed(void)
{
    if (sock == NULL || dead)
        return 1;
    Utils::processPackets();
    return sock->isRemoteClosed();
}

int NetConn::recv(char *buf, unsigned max)
{
    if (sock == NULL || dead)
        return -1;
    Utils::processPackets();
    int16_t n = sock->recv((uint8_t *)buf, max);
    if (n < 0) {
        err = "Connection error";
        dead = 1;
        return -1;
    }
    if (n == 0 && sock->isRemoteClosed()) {
        err = "The connection was closed";
        closedByPeer = 1;
        dead = 1;
        return -1;
    }
    return n;
}
