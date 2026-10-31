"""The TLS tunnel: plain TCP in from the DOS PC, HTTPS out to the API.

  python3 tunnel/tunnel.py [--map 8443=api.anthropic.com:443]... [--listen 0.0.0.0]

An 8088 cannot do TLS, so this is the one thing that runs elsewhere (a PC, a
Raspberry Pi, anything with Python 3). It knows nothing about the API: for each
client connection it opens a TLS connection to the mapped host and copies bytes
both ways until one side closes. It never logs or inspects the bytes (the API
key passes through here). The default map is Anthropic's API on local port 8443;
add `--map 8444=openrouter.ai:443` for a second provider on another port.

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
import logging
import ssl
import sys

log = logging.getLogger('tunnel')


def parse_map(spec):
    """"8443=host:443" -> (8443, "host", 443); the remote port defaults to 443."""
    port, _, dest = spec.partition('=')
    host, _, rport = dest.partition(':')
    if not port.isdigit() or not host:
        raise argparse.ArgumentTypeError(f'bad map {spec!r}: want PORT=HOST[:PORT]')
    return int(port), host, int(rport) if rport else 443


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
        try:
            writer.close()
        except Exception:   # pylint: disable=broad-except  # closing a dead socket must not raise
            pass


async def serve_client(reader, writer, host, rport, ctx):
    peer = writer.get_extra_info('peername')
    log.info('open  %s -> %s:%d', peer, host, rport)
    try:
        rreader, rwriter = await asyncio.open_connection(host, rport, ssl=ctx, server_hostname=host)
    except (OSError, ssl.SSLError) as e:
        log.warning('%s: cannot reach %s:%d: %s', peer, host, rport, e)
        writer.close()
        return
    up, down = [0], [0]
    await asyncio.gather(pump(reader, rwriter, up), pump(rreader, writer, down))
    log.info('close %s -> %s:%d (%d bytes up, %d down)', peer, host, rport, up[0], down[0])


async def run(listen, maps):
    ctx = ssl.create_default_context()
    servers = []
    for port, host, rport in maps:
        def handler(reader, writer, host=host, rport=rport):
            return serve_client(reader, writer, host, rport, ctx)
        servers.append(await asyncio.start_server(handler, listen, port))
        log.info('listening on %s:%d for %s:%d', listen, port, host, rport)
    await asyncio.gather(*(s.serve_forever() for s in servers))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--map', action='append', type=parse_map, metavar='PORT=HOST[:PORT]',
                    help='local port -> TLS host (default 8443=api.anthropic.com:443; repeatable)')
    ap.add_argument('--listen', default='0.0.0.0', help='address to listen on (default all)')
    ap.add_argument('--verbose', action='store_true', help='also log asyncio debug messages')
    a = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO,
                        format='%(asctime)s %(message)s', datefmt='%H:%M:%S', stream=sys.stdout)
    maps = a.map or [(8443, 'api.anthropic.com', 443)]
    try:
        asyncio.run(run(a.listen, maps))
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
