"""Tests for tunnel.py (python3 tunnel/test_tunnel.py, part of make test).

The tunnel's upstream is a local plain-TCP stand-in handed to it as the `connect`
function, so nothing here reaches the internet. The key is made up.
"""

import asyncio
import logging
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import tunnel  # noqa: E402

logging.getLogger('tunnel').addHandler(logging.NullHandler())   # no stderr noise from the refusals under test

KEY = 'sk-ant-made-up-test-key-123'
HERE = os.path.dirname(os.path.abspath(__file__))
OK = b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok'


class Upstream:
    """A plain-TCP stand-in for the API host. mode 'echo' sends every byte back; mode 'http' answers each
    request with a 200 after reading its head and Content-Length body, recording (head, body) pairs."""

    def __init__(self, mode):
        self.mode = mode
        self.raw = b''                  # every byte received, in order
        self.requests = []              # (head, body) in 'http' mode
        self.connections = 0
        self.server = None

    async def start(self):
        self.server = await asyncio.start_server(self.handle, '127.0.0.1', 0)
        return self.server.sockets[0].getsockname()[1]

    async def stop(self):
        self.server.close()
        await self.server.wait_closed()

    async def handle(self, reader, writer):
        self.connections += 1
        try:
            if self.mode == 'echo':
                while True:
                    data = await reader.read(4096)
                    if not data:
                        break
                    self.raw += data
                    writer.write(data)
                    await writer.drain()
            else:
                while True:
                    head = await reader.readuntil(b'\r\n\r\n')
                    length = 0
                    for line in head.split(b'\r\n'):
                        if line.lower().startswith(b'content-length:'):
                            length = int(line.split(b':')[1])
                    body = await reader.readexactly(length)
                    self.raw += head + body
                    self.requests.append((head, body))
                    writer.write(OK)
                    await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            writer.close()


def request(body=b'{"model":"m"}', headers=(), first=b'POST /v1/messages HTTP/1.1'):
    """A raw request: request line, the headers, Content-Length for the body."""
    lines = [first, b'Host: api.anthropic.com', b'Content-Length: %d' % len(body)] + list(headers)
    return b'\r\n'.join(lines) + b'\r\n\r\n' + body


async def read_response(reader):
    """The status line and body of one Content-Length response."""
    head = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 5)
    length = int(head.lower().split(b'content-length:')[1].split(b'\r\n')[0])
    return head.split(b'\r\n')[0], await asyncio.wait_for(reader.readexactly(length), 5)


def header_names(head):
    return [l.split(b':')[0].lower() for l in head.split(b'\r\n')[1:] if l]


class TunnelCase(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.upstreams = {}
        self.servers = []
        self.writers = []

    async def asyncTearDown(self):
        for w in self.writers:                  # wait_closed() waits for open connections
            w.close()
        await asyncio.sleep(0.05)
        for s in self.servers:
            s.close()
            await s.wait_closed()
        for u in self.upstreams.values():
            await u.stop()

    async def tunnel(self, upstreams, maps, **kw):
        """Start the tunnel on ephemeral ports; upstreams: {host: mode}. Returns the local ports."""
        ports = {}
        for host, mode in upstreams.items():
            self.upstreams[host] = Upstream(mode)
            ports[host] = await self.upstreams[host].start()

        async def connect(host, rport):         # rport is ignored: every host is a local stand-in
            return await asyncio.open_connection('127.0.0.1', ports[host])
        self.servers = await tunnel.start('127.0.0.1', [(0, h, 443) for h in maps], connect=connect, **kw)
        return [s.sockets[0].getsockname()[1] for s in self.servers]

    async def client(self, port):
        reader, writer = await asyncio.open_connection('127.0.0.1', port)
        self.writers.append(writer)
        return reader, writer

    async def at_eof(self, reader):
        try:
            return await asyncio.wait_for(reader.read(), 5)
        except ConnectionResetError:            # closed with our bytes unread: the same as EOF here
            return b''


class DefaultModeTest(TunnelCase):
    async def test_bytes_copied_exactly_and_nothing_added_or_removed(self):
        (port,) = await self.tunnel({'api.anthropic.com': 'echo'}, ['api.anthropic.com'])
        sent = request(headers=[b'x-api-key: client-own', b'Authorization: Bearer client-own']) \
            + b'\x00\xff garbage that is not HTTP \r\n\r\n' + bytes(range(256))
        reader, writer = await self.client(port)
        writer.write(sent)
        got = await asyncio.wait_for(reader.readexactly(len(sent)), 5)
        self.assertEqual(got, sent)
        self.assertEqual(self.upstreams['api.anthropic.com'].raw, sent)

    async def test_open_to_any_client_without_allow(self):
        (port,) = await self.tunnel({'api.anthropic.com': 'echo'}, ['api.anthropic.com'])
        reader, writer = await self.client(port)
        writer.write(b'hello')
        self.assertEqual(await asyncio.wait_for(reader.readexactly(5), 5), b'hello')


class KeyModeTest(TunnelCase):
    async def start(self, **kw):
        (port,) = await self.tunnel({'api.anthropic.com': 'http'}, ['api.anthropic.com'], key=KEY, **kw)
        return port, self.upstreams['api.anthropic.com']

    async def test_key_added_and_the_clients_own_removed(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        writer.write(request(headers=[b'X-Api-Key: client-own-key', b'AUTHORIZATION: Bearer client-own',
                                      b'anthropic-version: 2023-06-01']))
        self.assertEqual(await read_response(reader), (b'HTTP/1.1 200 OK', b'ok'))
        (head, body), = up.requests
        self.assertEqual(body, b'{"model":"m"}')
        self.assertEqual(head.split(b'\r\n')[0], b'POST /v1/messages HTTP/1.1')
        self.assertEqual(header_names(head).count(b'x-api-key'), 1)
        self.assertIn(b'x-api-key: ' + KEY.encode() + b'\r\n', head)
        self.assertNotIn(b'authorization', header_names(head))
        self.assertNotIn(b'client-own', head)
        self.assertEqual(header_names(head), [b'x-api-key', b'host', b'content-length', b'anthropic-version'])

    async def test_a_client_without_a_key_gets_one(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        writer.write(request())
        await read_response(reader)
        self.assertIn(KEY.encode(), up.requests[0][0])

    async def test_second_request_on_the_same_connection(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        for body in (b'first', b'second body, longer'):
            writer.write(request(body, headers=[b'x-api-key: client-own']))
            await read_response(reader)
        self.assertEqual([b for _, b in up.requests], [b'first', b'second body, longer'])
        for head, _ in up.requests:
            self.assertEqual(header_names(head).count(b'x-api-key'), 1)
            self.assertIn(KEY.encode(), head)
            self.assertNotIn(b'client-own', head)
        self.assertEqual(up.connections, 1)

    async def test_two_requests_in_one_send(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        writer.write(request(b'one') + request(b'two', headers=[b'Authorization: x']))
        await read_response(reader)
        await read_response(reader)
        self.assertEqual([b for _, b in up.requests], [b'one', b'two'])
        self.assertNotIn(b'authorization', header_names(up.requests[1][0]))

    async def test_head_and_body_arriving_in_pieces(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        body = bytes(range(256)) * 200                       # 51 KB: several reads
        data = request(body, headers=[b'x-api-key: client-own'])
        for i in range(0, len(data), 997):
            writer.write(data[i:i + 997])
            await writer.drain()
            await asyncio.sleep(0)
        await read_response(reader)
        self.assertEqual(up.requests[0][1], body)
        self.assertNotIn(b'client-own', up.requests[0][0])

    async def test_a_body_that_looks_like_a_head_is_not_one(self):
        port, up = await self.start()
        body = b'{"a":"\r\n\r\nPOST /x HTTP/1.1\r\nx-api-key: evil\r\n\r\n"}'
        reader, writer = await self.client(port)
        writer.write(request(body))
        await read_response(reader)
        self.assertEqual(up.requests, [(up.requests[0][0], body)])
        self.assertEqual(len(up.requests), 1)

    async def test_an_empty_body_get(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        writer.write(b'GET /v1/models HTTP/1.1\r\nHost: api.anthropic.com\r\n\r\n')
        await read_response(reader)
        self.assertEqual(up.requests[0][1], b'')
        self.assertIn(KEY.encode(), up.requests[0][0])

    async def test_folded_continuation_of_a_dropped_header_goes_with_it(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        writer.write(request(headers=[b'Authorization: Bearer one', b'  folded-secret', b'X-Kept: a', b'\tfolded-kept']))
        await read_response(reader)
        head = up.requests[0][0]
        self.assertNotIn(b'folded-secret', head)
        self.assertIn(b'X-Kept: a\r\n\tfolded-kept', head)

    async def test_requests_it_cannot_frame_are_refused_and_never_forwarded(self):
        port, up = await self.start()
        cases = {
            'chunked': (request(headers=[b'Transfer-Encoding: chunked']), b'501'),
            'upgrade': (request(headers=[b'Upgrade: websocket']), b'501'),
            'connect': (request(first=b'CONNECT api.anthropic.com:443 HTTP/1.1'), b'501'),
            'two lengths': (request(headers=[b'Content-Length: 5']), b'400'),
            'bad length': (b'POST / HTTP/1.1\r\nContent-Length: 1x\r\n\r\n', b'400'),
            'bare LF smuggling': (b'POST / HTTP/1.1\r\nX-A: b\nx-api-key: evil\r\nContent-Length: 0\r\n\r\n', b'400'),
            'header without colon': (b'POST / HTTP/1.1\r\nnonsense\r\n\r\n', b'400'),
            'space before colon': (b'POST / HTTP/1.1\r\nx-api-key : evil\r\n\r\n', b'400'),
            'junk': (b'\x16\x03\x01 not http at all\r\n\r\n', b'400'),
            'huge head': (b'POST / HTTP/1.1\r\n' + b'X-Pad: ' + b'a' * 70000, b'431'),
        }
        for name, (data, status) in cases.items():
            with self.subTest(name):
                reader, writer = await self.client(port)
                writer.write(data)
                got = await self.at_eof(reader)
                self.assertTrue(got.startswith(b'HTTP/1.1 ' + status), got[:40])
        self.assertEqual(up.requests, [])
        self.assertEqual(up.raw, b'')

    async def test_a_refused_request_closes_after_earlier_ones_went_through(self):
        port, up = await self.start()
        reader, writer = await self.client(port)
        writer.write(request(b'fine'))
        await read_response(reader)
        writer.write(request(headers=[b'Transfer-Encoding: chunked']))
        rest = await self.at_eof(reader)
        self.assertTrue(rest.startswith(b'HTTP/1.1 501'))
        self.assertEqual([b for _, b in up.requests], [b'fine'])

    async def test_the_key_and_the_requests_are_never_logged(self):
        with self.assertLogs('tunnel', level='DEBUG') as logs:
            port, _ = await self.start(allow=frozenset([tunnel.normalize('127.0.0.1')]))
            reader, writer = await self.client(port)
            writer.write(request(b'SECRET-BODY-MARKER', headers=[b'x-api-key: client-own-marker']))
            await read_response(reader)
            writer.write(request(headers=[b'Transfer-Encoding: chunked', b'X-Marker: header-marker']))
            await self.at_eof(reader)
            await asyncio.sleep(0.05)
        text = '\n'.join(logs.output)
        for secret in (KEY, 'SECRET-BODY-MARKER', 'client-own-marker', 'header-marker', 'Transfer-Encoding'):
            self.assertNotIn(secret, text)
        self.assertIn('refused a request', text)

    async def test_key_only_for_the_anthropic_map(self):
        ports = await self.tunnel({'api.anthropic.com': 'http', 'openrouter.ai': 'echo'},
                                  ['api.anthropic.com', 'openrouter.ai'], key=KEY)
        reader, writer = await self.client(ports[1])
        sent = request(headers=[b'Authorization: Bearer or-own'])
        writer.write(sent)
        self.assertEqual(await asyncio.wait_for(reader.readexactly(len(sent)), 5), sent)
        self.assertEqual(self.upstreams['openrouter.ai'].raw, sent)
        self.assertNotIn(KEY.encode(), self.upstreams['openrouter.ai'].raw)
        reader, writer = await self.client(ports[0])
        writer.write(request())
        await read_response(reader)
        self.assertIn(KEY.encode(), self.upstreams['api.anthropic.com'].raw)

    async def test_no_anthropic_map_means_no_start(self):
        with self.assertRaisesRegex(ValueError, 'api.anthropic.com'):
            await tunnel.start('127.0.0.1', [(0, 'openrouter.ai', 443)], key=KEY)


class AllowTest(TunnelCase):
    async def test_a_client_not_in_the_list_is_closed_and_logged(self):
        with self.assertLogs('tunnel', level='INFO') as logs:
            (port,) = await self.tunnel({'api.anthropic.com': 'echo'}, ['api.anthropic.com'],
                                        allow=frozenset([tunnel.normalize('192.0.2.7')]))
            reader, writer = await self.client(port)
            writer.write(b'hello')
            self.assertEqual(await self.at_eof(reader), b'')
            await asyncio.sleep(0.05)
        self.assertEqual(self.upstreams['api.anthropic.com'].connections, 0)
        self.assertTrue(any('refused' in l and '127.0.0.1' in l for l in logs.output))

    async def test_an_allowed_client_is_served(self):
        (port,) = await self.tunnel({'api.anthropic.com': 'echo'}, ['api.anthropic.com'],
                                    allow=frozenset([tunnel.normalize('192.0.2.7'), tunnel.normalize('127.0.0.1')]))
        reader, writer = await self.client(port)
        writer.write(b'hello')
        self.assertEqual(await asyncio.wait_for(reader.readexactly(5), 5), b'hello')

    async def test_allow_applies_to_every_map(self):
        ports = await self.tunnel({'a.example': 'echo', 'b.example': 'echo'}, ['a.example', 'b.example'],
                                  allow=frozenset([tunnel.normalize('192.0.2.7')]))
        for port in ports:
            reader, _ = await self.client(port)
            self.assertEqual(await self.at_eof(reader), b'')


class HelperTest(unittest.TestCase):
    def test_rewrite_head(self):
        head, length = tunnel.rewrite_head(b'POST / HTTP/1.1\r\nHost: h\r\nauthorization: x\r\nContent-Length: 12', 'K')
        self.assertEqual(head, b'POST / HTTP/1.1\r\nx-api-key: K\r\nHost: h\r\nContent-Length: 12')
        self.assertEqual(length, 12)
        self.assertEqual(tunnel.rewrite_head(b'GET / HTTP/1.0', 'K')[1], 0)

    def test_allow_addresses(self):
        self.assertEqual(tunnel.parse_allow('::ffff:192.168.1.5'), tunnel.normalize('192.168.1.5'))
        self.assertEqual(tunnel.normalize('fe80::1%en0'), tunnel.normalize('fe80::1'))
        for bad in ('192.168.1.0/24', 'localhost', '', '300.1.1.1'):
            with self.assertRaises(Exception, msg=bad):
                tunnel.parse_allow(bad)

    def test_loopback(self):
        for ok in ('127.0.0.1', '127.5.5.5', '::1', 'localhost', '::ffff:127.0.0.1'):
            self.assertTrue(tunnel.is_loopback(ok), ok)
        for bad in ('0.0.0.0', '192.168.1.5', '', 'example.com', '::'):
            self.assertFalse(tunnel.is_loopback(bad), bad)

    def test_read_key(self):
        with tempfile.TemporaryDirectory() as d:
            def write(data):
                path = os.path.join(d, 'k')
                with open(path, 'wb') as f:
                    f.write(data)
                return path
            self.assertEqual(tunnel.read_key(write(b'  ' + KEY.encode() + b' \r\n\r\n')), KEY)
            for bad in (b'', b' \r\n\t\n', b'two words', b'line1\nline2', b'k\xffy', b'k\x00y'):
                with self.assertRaises(ValueError, msg=bad) as cm:
                    tunnel.read_key(write(bad))
                if len(bad.strip()) > 2:
                    self.assertNotIn(bad.decode('latin-1').strip(), str(cm.exception).replace(d, ''))
            with self.assertRaisesRegex(ValueError, 'cannot read'):
                tunnel.read_key(os.path.join(d, 'missing'))


class CommandLineTest(unittest.TestCase):
    def run_main(self, *args, key=KEY):
        """tunnel.main() with the given arguments and a key file; returns (exit code or None, stderr, run calls)."""
        calls = []
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'key')
            with open(path, 'w', encoding='ascii') as f:
                f.write(key)
            argv = ['tunnel.py'] + [path if a == '@KEY' else a for a in args]
            err = tempfile.TemporaryFile('w+')
            code = None
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(sys, 'stderr', err), \
                    mock.patch.object(tunnel, 'run', lambda *a, **k: calls.append((a, k))), \
                    mock.patch.object(tunnel.asyncio, 'run', lambda c: None), \
                    mock.patch.object(tunnel.logging, 'basicConfig'):
                try:
                    tunnel.main()
                except SystemExit as e:
                    code = e.code
            err.seek(0)
            return code, err.read(), calls

    def test_key_file_needs_allow_unless_loopback(self):
        code, err, calls = self.run_main('--key-file', '@KEY')
        self.assertEqual(code, 2)
        self.assertIn('--allow', err)
        self.assertNotIn(KEY, err)
        self.assertEqual(calls, [])
        code, _, calls = self.run_main('--key-file', '@KEY', '--allow', '192.168.1.5')
        self.assertIsNone(code)
        self.assertEqual(calls[0][0][3], KEY)
        for listen in ('127.0.0.1', '::1', 'localhost'):
            code, _, calls = self.run_main('--key-file', '@KEY', '--listen', listen)
            self.assertIsNone(code, listen)

    def test_empty_key_file_is_a_startup_error(self):
        code, err, calls = self.run_main('--key-file', '@KEY', '--allow', '192.168.1.5', key=' \n')
        self.assertEqual(code, 2)
        self.assertIn('empty', err)
        self.assertEqual(calls, [])

    def test_default_has_no_key_and_no_allow(self):
        code, _, calls = self.run_main()
        self.assertIsNone(code)
        args = calls[0][0]
        self.assertEqual((args[0], args[2], args[3]), ('0.0.0.0', frozenset(), None))

    def test_missing_key_file_is_a_startup_error(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, 'tunnel.py'), '--key-file', '/no/such/key',
                               '--allow', '127.0.0.1'], capture_output=True, text=True, check=False, timeout=20)
        self.assertEqual(proc.returncode, 2)
        self.assertIn('cannot read key file', proc.stderr)


if __name__ == '__main__':
    unittest.main()
