#!/usr/bin/env python3
"""End-to-end tests of XTBridge's protocol against the mock backend.

  python bridge/test_bridge.py
"""

import asyncio
import logging
import os
import sys
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
                                     token=self.token)
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


if __name__ == '__main__':
    logging.disable(logging.CRITICAL)
    unittest.main()
