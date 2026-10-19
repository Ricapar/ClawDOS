#!/usr/bin/env python3
"""End-to-end tests of XTBridge's protocol against the mock backend.

  python bridge/test_bridge.py
"""

import asyncio
import json
import logging
import os
import sys
import tempfile
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xtbridge  # noqa: E402


class Client:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer

    async def line(self, timeout=5):
        raw = await asyncio.wait_for(self.reader.readline(), timeout)
        if not raw:
            raise EOFError
        assert raw.endswith(b'\n') and len(raw) <= xtbridge.MAX_TXT + 6, raw[:80]
        verb, _, rest = raw.rstrip(b'\n').partition(b' ')
        return verb.decode(), rest

    async def send(self, text):
        self.writer.write(text.encode('cp437') + b'\n')
        await self.writer.drain()

    async def ask(self, text):
        """Send MSG; return (reply text, final verb, final payload)."""
        await self.send('MSG ' + text)
        out = []
        while True:
            verb, rest = await self.line()
            if verb == 'TXT':
                assert b'\n' not in rest
                out.append(rest.decode('cp437'))
            elif verb == 'NL':
                out.append('\n')
            else:
                return ''.join(out), verb, rest.decode('cp437')


class BridgeTest(unittest.IsolatedAsyncioTestCase):
    token = None
    speed = 0   # no delays

    async def asyncSetUp(self):
        args = types.SimpleNamespace(host='127.0.0.1', port=0, mock=True, speed=self.speed,
                                     token=self.token, config=getattr(self, 'config', None))
        self.server = await xtbridge.start(args)
        port = self.server.sockets[0].getsockname()[1]
        self.c = Client(*await asyncio.open_connection('127.0.0.1', port))
        self.hello = await self.c.line()

    async def asyncTearDown(self):
        self.c.writer.close()
        self.server.close()
        await self.server.wait_closed()


class MockProtocol(BridgeTest):
    async def test_hello_info_ping(self):
        self.assertEqual(self.hello, ('HELLO', b'XTBRIDGE 1'))
        await self.c.send('INFO')
        verb, rest = await self.c.line()
        self.assertEqual(verb, 'INFO')
        self.assertIn(b'mock', rest)
        await self.c.send('PING')
        self.assertEqual(await self.c.line(), ('PONG', b''))

    async def test_small_talk_and_history(self):
        text, verb, _ = await self.c.ask('hello there')
        self.assertEqual(verb, 'END')
        self.assertIn('mock bridge', text)
        text, verb, _ = await self.c.ask('something else')
        self.assertIn('hello there', text)
        text, _, _ = await self.c.ask('!history')
        self.assertIn('2 earlier', text)

    async def test_new_clears_history(self):
        await self.c.ask('hello')
        await self.c.send('NEW')
        self.assertEqual(await self.c.line(), ('OK', b''))
        text, _, _ = await self.c.ask('!history')
        self.assertIn('first message', text)

    async def test_scroll(self):
        text, verb, _ = await self.c.ask('!scroll')
        self.assertEqual(verb, 'END')
        self.assertEqual(text.count('\n'), 60)

    async def test_wide_lines_are_split(self):
        text, verb, _ = await self.c.ask('!wide')
        self.assertEqual(verb, 'END')
        self.assertIn('word169', text)

    async def test_cp437(self):
        text, _, _ = await self.c.ask('!cp437')
        self.assertIn('café', text)
        self.assertIn('╔══╦══╗', text)
        self.assertIn('"curly quotes"', text)
        self.assertIn('->', text)

    async def test_error_refuse_cutoff(self):
        _, verb, msg = await self.c.ask('!error')
        self.assertEqual((verb, msg), ('ERR', 'API error 529: Overloaded (simulated)'))
        _, verb, msg = await self.c.ask('!refuse')
        self.assertEqual(verb, 'ERR')
        text, verb, _ = await self.c.ask('!cutoff')
        self.assertEqual(verb, 'END')
        self.assertTrue(text.endswith('[reply cut off: too long]'))
        text, _, _ = await self.c.ask('!history')
        self.assertIn('1 earlier', text)   # only !cutoff completed

    async def test_drop(self):
        with self.assertRaises(EOFError):
            await self.c.ask('!drop')

    async def test_unknown_and_empty(self):
        text, _, _ = await self.c.ask('!bogus')
        self.assertIn('Unknown test command', text)
        await self.c.send('MSG   ')
        self.assertEqual(await self.c.line(), ('ERR', b'empty message'))
        await self.c.send('FROB')
        self.assertEqual(await self.c.line(), ('ERR', b'unknown command'))


class RealTiming(BridgeTest):
    speed = 1

    async def test_stop_and_busy(self):
        await self.c.send('MSG !stall')
        await self.c.send('MSG again')
        self.assertEqual(await self.c.line(), ('ERR', b'busy'))
        await self.c.send('STOP')
        self.assertEqual(await self.c.line(), ('END', b'stopped'))
        _, verb, _ = await self.c.ask('hi')
        self.assertEqual(verb, 'END')


class Auth(BridgeTest):
    token = 'sekrit'

    async def test_auth(self):
        self.assertEqual(self.hello, ('HELLO', b'XTBRIDGE 1 AUTH'))
        await self.c.send('MSG hi')
        self.assertEqual(await self.c.line(), ('ERR', b'AUTH required'))
        await self.c.send('AUTH wrong')
        self.assertEqual(await self.c.line(), ('ERR', b'bad token'))
        await self.c.send('AUTH sekrit')
        self.assertEqual(await self.c.line(), ('OK', b''))
        _, verb, _ = await self.c.ask('hi')
        self.assertEqual(verb, 'END')


class FakeOpenAI:
    """Just enough of an OpenAI-compatible /chat/completions server: streams the
    reply chosen by the last user message, and records each request body."""

    REPLIES = {
        'cut': (['Too long'], 'length'),
        'filtered': ([], 'content_filter'),
    }

    def __init__(self):
        self.requests = []

    async def start(self):
        self.server = await asyncio.start_server(self.handle, '127.0.0.1', 0)
        return self.server.sockets[0].getsockname()[1]

    async def handle(self, reader, writer):
        head = await reader.readuntil(b'\r\n\r\n')
        length = int(next(l.split(b':')[1] for l in head.split(b'\r\n')
                          if l.lower().startswith(b'content-length')))
        body = json.loads(await reader.readexactly(length))
        self.requests.append((head, body))
        last = body['messages'][-1]['content']
        if last == 'bad key':
            payload = json.dumps({'error': {'message': 'Invalid API key'}}).encode()
            writer.write(b'HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n'
                         b'Content-Length: %d\r\nConnection: close\r\n\r\n' % len(payload) + payload)
        else:
            if last == 'midstream':
                events = [{'choices': [{'delta': {'content': 'Half'}}]},
                          {'error': {'message': 'Provider fell over'}}]
            else:
                pieces, finish = self.REPLIES.get(last, (['Fake ', 'says: ', last, '\nbye'], 'stop'))
                events = [{'choices': [{'delta': {'content': p}}]} for p in pieces]
                events.append({'choices': [{'delta': {}, 'finish_reason': finish}]})
            writer.write(b'HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n'
                         b': keep-alive\n\n')
            for e in events:
                writer.write(b'data: ' + json.dumps(e).encode() + b'\n\n')
            writer.write(b'data: [DONE]\n\n')
        await writer.drain()
        writer.close()


class Providers(BridgeTest):
    async def asyncSetUp(self):
        self.fake = FakeOpenAI()
        port = await self.fake.start()
        self.tmp = tempfile.TemporaryDirectory()
        self.config = os.path.join(self.tmp.name, 'providers.toml')
        with open(self.config, 'w') as f:
            f.write(f'[fake]\ntype = "openai"\nname = "Fakey"\nbase_url = "http://127.0.0.1:{port}/v1"\n'
                    f'model = "fake-1"\napi_key_env = "CXT_TEST_KEY"\nextra = {{ temperature = 0.5 }}\n')
        os.environ['CXT_TEST_KEY'] = 'k123'
        await super().asyncSetUp()

    async def asyncTearDown(self):
        await super().asyncTearDown()
        self.fake.server.close()
        self.tmp.cleanup()

    async def test_list_and_switch_keeps_history(self):
        await self.c.ask('hello there')
        text, verb, _ = await self.c.ask('/model')
        self.assertEqual(verb, 'END')
        self.assertIn('Now using mock', text)
        self.assertIn('fake        fake-1', text)
        text, _, _ = await self.c.ask('/model fake')
        self.assertIn('Switched to fake: fake-1, keeping 1 earlier', text)
        await self.c.send('INFO')
        self.assertEqual(await self.c.line(), ('INFO', b'fake: fake-1'))

        text, verb, _ = await self.c.ask('ping')
        self.assertEqual((text, verb), ('Fake says: ping\nbye', 'END'))
        head, body = self.fake.requests[-1]
        self.assertIn(b'authorization: bearer k123', head.lower())
        self.assertEqual(body['model'], 'fake-1')
        self.assertEqual(body['temperature'], 0.5)
        self.assertTrue(body['stream'])
        self.assertIn('You are Fakey', body['messages'][0]['content'])
        self.assertEqual([m['role'] for m in body['messages']], ['system', 'user', 'assistant', 'user'])
        self.assertEqual(body['messages'][1]['content'], 'hello there')

        await self.c.ask('/model other-model')       # model only: stays on this provider
        await self.c.ask('again')
        self.assertEqual(self.fake.requests[-1][1]['model'], 'other-model')
        self.assertEqual(len(self.fake.requests[-1][1]['messages']), 6)

        await self.c.ask('/model mock')               # and back, history intact
        text, _, _ = await self.c.ask('!history')
        self.assertIn('3 earlier', text)

    async def test_switch_errors(self):
        text, _, _ = await self.c.ask('/model nope:x')
        self.assertIn('Unknown provider', text)
        os.environ.pop('CXT_TEST_KEY')
        text, _, _ = await self.c.ask('/model fake')
        self.assertIn('set CXT_TEST_KEY', text)

    async def test_openai_failures(self):
        await self.c.ask('/model fake:fake-2')
        text, verb, _ = await self.c.ask('cut')
        self.assertEqual((text, verb), ('Too long\n[reply cut off: too long]', 'END'))
        self.assertEqual(await self.c.ask('bad key'), ('', 'ERR', 'API error 401: Invalid API key'))
        _, verb, msg = await self.c.ask('midstream')
        self.assertEqual((verb, msg), ('ERR', 'Provider fell over'))
        _, verb, msg = await self.c.ask('filtered')
        self.assertEqual((verb, msg), ('ERR', 'The model declined to answer that.'))
        await self.c.ask('ok')   # only completed replies are remembered
        roles = [m['role'] for m in self.fake.requests[-1][1]['messages']]
        self.assertEqual(roles, ['system', 'user', 'assistant', 'user'])


if __name__ == '__main__':
    logging.disable(logging.CRITICAL)
    unittest.main()
