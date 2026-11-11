"""The TLS tunnel: plain TCP in from the DOS PC, HTTPS out to the API.

  python3 tunnel/tunnel.py [--map 8443=api.anthropic.com:443]... [--listen 0.0.0.0]
                           [--allow ADDR]... [--key-file PATH]

An 8088 cannot do TLS, so this is the one thing that runs elsewhere (a PC, a
Raspberry Pi, anything with Python 3). It knows nothing about the API: for each
client connection it opens a TLS connection to the mapped host and copies bytes
both ways until one side closes. It never logs or inspects the bytes (the API
key passes through here). The default map is Anthropic's API on local port 8443;
add `--map 8444=openrouter.ai:443` for a second provider on another port.

`--allow ADDR` (repeatable, single addresses) lets only those clients connect; the
rest are closed and logged. Without it anyone who can reach the port is served.

`--key-file PATH` is the one exception to "knows nothing about the API", and is
strictly opt-in: the tunnel keeps the API key, so the DOS PC's disk need not
(`build.py --no-key`, `auth tunnel` in CLAWDOS.CFG). For each request it forwards
to api.anthropic.com it removes any `x-api-key` and `authorization` header the
client sent and adds `x-api-key: <the file's contents>`. To find each request's
head it follows HTTP/1.1 framing: head up to the blank line, then Content-Length
bytes of body, then the next request on the same connection. A request it cannot
frame (Transfer-Encoding, Upgrade, CONNECT, a malformed head) is refused with an
error and the connection closed. Responses are still copied unread. The key and
the requests are never logged. It will not start without an `--allow`, unless
`--listen` is a loopback address: a tunnel that adds a key must not serve everyone.

Any TLS forwarder is a drop-in replacement, e.g.

  socat TCP-LISTEN:8443,fork,reuseaddr OPENSSL:api.anthropic.com:443

or an stunnel client stanza:

  [anthropic]
  client = yes
  accept = 8443
  connect = api.anthropic.com:443

Stdlib only.
"""

import argparse
import asyncio
import ipaddress
import logging
import ssl
import sys

log = logging.getLogger('tunnel')

# The key is only ever added for this host: the default map's, never a second provider's.
KEY_HOST = 'api.anthropic.com'
MAX_HEAD = 64 * 1024          # a request head longer than this is refused


def parse_map(spec):
    """"8443=host:443" -> (8443, "host", 443); the remote port defaults to 443."""
    port, _, dest = spec.partition('=')
    host, _, rport = dest.partition(':')
    if not port.isdigit() or not host:
        raise argparse.ArgumentTypeError(f'bad map {spec!r}: want PORT=HOST[:PORT]')
    return int(port), host, int(rport) if rport else 443


def normalize(addr):
    """An ip_address from text or an ip_address, with an IPv4-mapped IPv6 address as the IPv4 one."""
    ip = ipaddress.ip_address(str(addr).split('%')[0])
    return ip.ipv4_mapped if ip.version == 6 and ip.ipv4_mapped else ip


def parse_allow(spec):
    """A single client address for --allow (not a network)."""
    try:
        return normalize(spec)
    except ValueError:
        raise argparse.ArgumentTypeError(f'bad --allow {spec!r}: want a single IP address') from None


def is_loopback(listen):
    """True if the listen address can only be reached from this machine."""
    if listen == 'localhost':
        return True
    try:
        return normalize(listen).is_loopback
    except ValueError:
        return False


def read_key(path):
    """The API key in the file, without surrounding whitespace. ValueError (never showing the
    contents) if the file cannot be read, is empty, or holds anything but visible ASCII."""
    try:
        with open(path, 'rb') as f:
            raw = f.read(4096)
    except OSError as e:
        raise ValueError(f'cannot read key file {path}: {e.strerror}') from None
    key = raw.strip()
    if not key:
        raise ValueError(f'key file {path} is empty')
    if not all(0x21 <= b <= 0x7E for b in key):
        raise ValueError(f'key file {path} must hold one key of visible ASCII characters, no spaces or line breaks')
    return key.decode('ascii')


class BadRequest(Exception):
    """A request the key mode will not forward: (status line, why for the log)."""

    def __init__(self, status, why):
        super().__init__(why)
        self.status = status
        self.why = why


def rewrite_head(head, key):
    """(new head, body length) for a request head (bytes, no final blank line): the client's
    x-api-key and authorization headers dropped, x-api-key: KEY first among the headers.
    BadRequest for anything whose framing or syntax is in doubt: the body length must be certain."""
    lines = head.split(b'\r\n')
    if any(b'\r' in l or b'\n' in l or b'\0' in l for l in lines):
        raise BadRequest('400 Bad Request', 'bare CR, LF or NUL in a request head')
    parts = lines[0].split(b' ')
    if len(parts) != 3 or not parts[0] or not parts[1] or not parts[2].startswith(b'HTTP/1.'):
        raise BadRequest('400 Bad Request', 'malformed request line')
    if parts[0].upper() == b'CONNECT':
        raise BadRequest('501 Not Implemented', 'CONNECT')
    out, length, dropping = [lines[0], b'x-api-key: ' + key.encode('ascii')], None, False
    for line in lines[1:]:
        if line[:1] in (b' ', b'\t'):                     # an obsolete folded continuation of the line before
            if not out[2:] and not dropping:
                raise BadRequest('400 Bad Request', 'folded header with nothing before it')
            if not dropping:
                out.append(line)
            continue
        name, colon, value = line.partition(b':')
        if not colon or not name or name != name.strip() or b' ' in name or b'\t' in name:
            raise BadRequest('400 Bad Request', 'malformed header line')
        lname = name.lower()
        dropping = lname in (b'x-api-key', b'authorization')
        if dropping:
            continue
        if lname in (b'transfer-encoding', b'upgrade'):
            raise BadRequest('501 Not Implemented', lname.decode('ascii') + ' request')
        if lname == b'content-length':
            value = value.strip()
            if length is not None or not value.isdigit() or not value.isascii():
                raise BadRequest('400 Bad Request', 'missing, repeated or bad Content-Length')
            length = int(value)
        out.append(line)
    return b'\r\n'.join(out), length or 0


async def forward_requests(reader, writer, key, counter):
    """Copy requests from reader to writer one at a time, each head rewritten by rewrite_head and
    followed by exactly its Content-Length bytes. counter[0] counts bytes, counter[1] requests.
    Returns at EOF; BadRequest if a request cannot be framed."""
    buf = b''
    while True:
        while True:
            end = buf.find(b'\r\n\r\n')
            if end >= 0:
                break
            if len(buf) > MAX_HEAD:
                raise BadRequest('431 Request Header Fields Too Large', 'request head too long')
            data = await reader.read(16384)
            if not data:
                return
            buf += data
        head, buf = buf[:end], buf[end + 4:]
        new_head, remaining = rewrite_head(head, key)
        new_head += b'\r\n\r\n'
        writer.write(new_head)
        await writer.drain()                                 # a client waiting for 100 Continue needs this out first
        counter[0] += len(new_head)
        counter[1] += 1
        while remaining:
            if not buf:
                buf = await reader.read(min(16384, remaining))
                if not buf:
                    return
            chunk, buf = buf[:remaining], buf[remaining:]
            remaining -= len(chunk)
            counter[0] += len(chunk)
            writer.write(chunk)
            await writer.drain()


def close_quietly(writer):
    """Close a stream; closing a dead socket must not raise."""
    try:
        writer.close()
    except Exception:   # pylint: disable=broad-except
        pass


async def pump(reader, writer, counter):
    """Copy reader to writer until EOF; counter[0] accumulates the bytes."""
    try:
        while True:
            data = await reader.read(16384)
            if not data:
                break
            counter[0] += len(data)
            writer.write(data)
            await writer.drain()
    except (ConnectionError, asyncio.IncompleteReadError, ssl.SSLError, OSError):
        pass
    finally:
        close_quietly(writer)


async def keyed_pump(reader, writer, key, counter, client):
    """pump for the client's side in key mode: forward_requests, and an error reply to the client
    (then everything closed) for a request that cannot be forwarded."""
    try:
        await forward_requests(reader, writer, key, counter)
    except BadRequest as e:
        log.warning('%s: refused a request: %s', client.get_extra_info('peername'), e.why)
        try:
            client.write(f'HTTP/1.1 {e.status}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'.encode('ascii'))
        except Exception:   # pylint: disable=broad-except
            pass
        close_quietly(client)
    except (ConnectionError, asyncio.IncompleteReadError, ssl.SSLError, OSError):
        pass
    finally:
        close_quietly(writer)


def tls_connector(ctx=None):
    """The default way upstream: a TLS connection to the mapped host. Tests pass their own."""
    ctx = ctx or ssl.create_default_context()

    async def connect(host, rport):
        return await asyncio.open_connection(host, rport, ssl=ctx, server_hostname=host)
    return connect


async def serve_client(reader, writer, host, rport, connect, key=None, allow=()):
    """One client: refuse it unless allowed, open the upstream, and pump both ways. With a key, the
    client's side goes through forward_requests."""
    peer = writer.get_extra_info('peername')
    if allow:
        try:
            ok = normalize(peer[0]) in allow
        except (ValueError, TypeError, IndexError):
            ok = False
        if not ok:
            log.warning('refused %s: not in --allow', peer)
            close_quietly(writer)
            return
    log.info('open  %s -> %s:%d%s', peer, host, rport, ' (adds the key)' if key else '')
    try:
        rreader, rwriter = await connect(host, rport)
    except (OSError, ssl.SSLError) as e:
        log.warning('%s: cannot reach %s:%d: %s', peer, host, rport, e)
        close_quietly(writer)
        return
    up, down = [0, 0], [0]
    client_side = keyed_pump(reader, rwriter, key, up, writer) if key else pump(reader, rwriter, up)
    await asyncio.gather(client_side, pump(rreader, writer, down))
    log.info('close %s -> %s:%d (%d bytes up, %d down%s)', peer, host, rport, up[0], down[0],
             f', {up[1]} requests' if key else '')


async def start(listen, maps, allow=(), key=None, connect=None):
    """Start listening on every map; returns the servers. With a key it is added on the maps whose
    host is api.anthropic.com (ValueError if there is none). `connect` replaces the TLS upstream (tests)."""
    connect = connect or tls_connector()
    if key and not any(host.lower() == KEY_HOST for _, host, _ in maps):
        raise ValueError(f'--key-file adds the key only for {KEY_HOST}, and no --map goes there')
    servers = []
    for port, host, rport in maps:
        map_key = key if key and host.lower() == KEY_HOST else None

        def handler(reader, writer, host=host, rport=rport, map_key=map_key):
            return serve_client(reader, writer, host, rport, connect, map_key, allow)
        servers.append(await asyncio.start_server(handler, listen, port))
        log.info('listening on %s:%d for %s:%d%s', listen, servers[-1].sockets[0].getsockname()[1], host, rport,
                 ': the API key is added to each request' if map_key else '')
    if allow:
        log.info('only these clients may connect: %s', ', '.join(str(a) for a in sorted(allow, key=str)))
    return servers


async def run(listen, maps, allow=(), key=None, connect=None):
    servers = await start(listen, maps, allow, key, connect)
    await asyncio.gather(*(s.serve_forever() for s in servers))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--map', action='append', type=parse_map, metavar='PORT=HOST[:PORT]',
                    help='local port -> TLS host (default 8443=api.anthropic.com:443; repeatable)')
    ap.add_argument('--listen', default='0.0.0.0', help='address to listen on (default all)')
    ap.add_argument('--allow', action='append', type=parse_allow, metavar='ADDR',
                    help='only this client address may connect (repeatable; default: anyone)')
    ap.add_argument('--key-file', metavar='PATH',
                    help='add x-api-key: <the file\'s contents> to every request for api.anthropic.com, '
                         'replacing any key the client sent (needs --allow unless --listen is loopback)')
    ap.add_argument('--verbose', action='store_true', help='also log asyncio debug messages')
    a = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO,
                        format='%(asctime)s %(message)s', datefmt='%H:%M:%S', stream=sys.stdout)
    maps = a.map or [(8443, KEY_HOST, 443)]
    key = None
    if a.key_file:
        try:
            key = read_key(a.key_file)
        except ValueError as e:
            ap.error(str(e))
        if not a.allow and not is_loopback(a.listen):
            ap.error('--key-file needs at least one --allow ADDR (the DOS PC\'s address), '
                     'or --listen on a loopback address: a tunnel that adds your key must not serve everyone')
    try:
        asyncio.run(run(a.listen, maps, frozenset(a.allow or ()), key))
    except ValueError as e:
        ap.error(str(e))
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
