#!/usr/bin/env python3
"""End-to-end tests of XTBridge's protocol against the mock backend.

  python bridge/test_bridge.py
"""

import asyncio
import copy
import json
import logging
import os
import sys
import tempfile
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mockchat  # noqa: E402
import providers  # noqa: E402
import tools  # noqa: E402
import xtbridge  # noqa: E402
from tools import ToolCall, ToolResult, escape, unescape  # noqa: E402


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


class ToolClient(Client):
    """A fake XT that answers tool calls. respond(name, args) -> (status, text)."""

    memories = 'no memories yet'

    def default_respond(self, name, args):
        if name == 'memory_list':
            return 'ok', self.memories
        return 'ok', 'done'

    async def ask(self, text):
        """Client.ask, answering any tool calls on the way."""
        reply, _, verb = await self.ask_tools(text, self.default_respond)
        return reply, verb, self.last_payload

    async def ask_tools(self, text, respond, raw=None):
        """Send MSG; return (reply text, [(name, args)], final verb)."""
        await self.send('MSG ' + text)
        out, calls, call = [], [], None
        while True:
            verb, rest = await self.line()
            if raw is not None:
                raw.append(verb)
            rest = rest.decode('cp437')
            if verb == 'TXT':
                out.append(rest)
            elif verb == 'NL':
                out.append('\n')
            elif verb == 'TOOL':
                tid, name = rest.split(' ', 1)
                call = (tid, name, {})
            elif verb == 'ARG':
                key, _, chunk = rest.partition(' ')
                call[2][key] = call[2].get(key, '') + chunk
            elif verb == 'CALL':
                tid, name, args = call
                args = {k: unescape(v) for k, v in args.items()}
                calls.append((name, args))
                status, result = respond(name, args)
                for i in range(0, len(result), 300):
                    await self.send('OUT ' + escape(result[i:i + 300]))
                await self.send(f'DONE {tid} {status}')
            else:
                self.last_payload = rest
                return ''.join(out), calls, verb


def tour_responder(bad_readback=False):
    """Plausible XT answers for the mock's !tooltour."""
    def respond(name, args):
        if name in mockchat.TOUR_EXPECT and mockchat.TOUR_EXPECT[name] == 'error':
            return 'error', f'unknown tool {name}'
        if name == 'sysinfo':
            return 'ok', 'DOS 7.10\nRAM 640K total'
        if name == 'dir':
            if args['path'].startswith('A:'):
                return 'error', 'drive not ready or disk error'
            return 'ok', 'AUTOEXEC.BAT   312  2026-10-21'
        if name == 'read':
            if 'NOPE' in args['path']:
                return 'error', 'file not found'
            body = 'Hello from somewhere else.' if bad_readback else 'Hello from the tool tour.'
            return 'ok', f'    1 {body}\n    2 Second line.'
        if name == 'delete' and '*' in args['path']:
            return 'error', 'no wildcards'
        if name == 'peek' and args['addr'] == '0000:04F0':
            return 'ok', '0000:04F0  43 58 54  CXT'
        if name == 'run':
            return 'ok', 'exit code 0\nFreeCom version 0.86'
        if name == 'memory_write' and not args['name'].isalnum():
            return 'error', 'bad memory name'
        if name == 'memory_list':
            return 'ok', 'CXTTOUR       27  Tour memory'
        if name == 'memory_read':
            return 'ok', 'Tour memory\nSecond line.'
        return 'ok', 'done'
    return respond


class BridgeTest(unittest.IsolatedAsyncioTestCase):
    token = None
    speed = 0   # no delays

    async def asyncSetUp(self):
        args = types.SimpleNamespace(host='127.0.0.1', port=0, mock=True, speed=self.speed,
                                     token=self.token, config=getattr(self, 'config', None),
                                     convlog=getattr(self, 'convlog', None))
        self.server = await xtbridge.start(args)
        port = self.server.sockets[0].getsockname()[1]
        self.c = ToolClient(*await asyncio.open_connection('127.0.0.1', port))
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


class Tools(BridgeTest):
    async def test_tool_roundtrip(self):
        text, calls, verb = await self.c.ask_tools(
            '!tool read path=C:\AUTOEXEC.BAT lines=5', lambda n, a: ('ok', '1 @ECHO OFF'))
        self.assertEqual(verb, 'END')
        self.assertEqual(calls, [('read', {'path': 'C:\AUTOEXEC.BAT', 'lines': '5'})])
        self.assertIn('read -> ok: 1 @ECHO OFF', text)

    async def test_text_flushed_before_tool(self):
        raw = []
        await self.c.ask_tools('!tool sysinfo', lambda n, a: ('ok', 'DOS'), raw)
        i = raw.index('TOOL')
        self.assertEqual(raw[i - 1], 'NL')

    async def test_quoted_values(self):
        _, calls, _ = await self.c.ask_tools('!tool write path=C:\X.TXT "text=hello world"',
                                             lambda n, a: ('ok', 'wrote'))
        self.assertEqual(calls[0][1]['text'], 'hello world')

    async def test_long_arg_chunks(self):
        raw = []
        _, calls, verb = await self.c.ask_tools('!draw', lambda n, a: ('ok', 'drawn'), raw)
        self.assertEqual(verb, 'END')
        self.assertGreaterEqual(raw.count('ARG'), 2)
        script = calls[0][1]['script']
        self.assertEqual(script, mockchat.DRAW_DEMO)
        self.assertIn('CIRCLE', script)

    async def test_denied_and_error_statuses(self):
        text, _, _ = await self.c.ask_tools('!tool delete path=C:\X', lambda n, a: ('denied', ''))
        self.assertIn('-> denied', text)
        text, _, _ = await self.c.ask_tools('!tool delete path=C:\X', lambda n, a: ('error', 'nope'))
        self.assertIn('-> error: nope', text)

    async def test_stray_done_ignored(self):
        await self.c.send('DONE t9 ok')
        await self.c.send('OUT junk')
        await self.c.send('PING')
        self.assertEqual(await self.c.line(), ('PONG', b''))

    async def test_tooltour_reports(self):
        text, calls, verb = await self.c.ask_tools('!tooltour', tour_responder())
        self.assertEqual(verb, 'END')
        self.assertEqual(len(calls), 22)
        self.assertIn('tool tour: PASS', text)
        text, _, _ = await self.c.ask_tools('!tooltour', tour_responder(bad_readback=True))
        self.assertIn('tool tour: FAIL', text)


class ToolTiming(BridgeTest):
    speed = 1

    async def test_stop_mid_tool_then_continue(self):
        await self.c.send('MSG !tool sysinfo')
        while True:
            verb, rest = await self.c.line()
            if verb == 'CALL':
                break
        await self.c.send('DONE t1 stopped')
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
            elif last in ('use tool', 'bad args'):
                args = ['{"path":', '"C:\\\\A.BAT"}'] if last == 'use tool' else ['{bad']
                events = [{'choices': [{'delta': {'content': 'Reading.'}}]},
                          {'choices': [{'delta': {'tool_calls': [
                              {'index': 0, 'id': 'call_1', 'type': 'function',
                               'function': {'name': 'read', 'arguments': args[0]}}]}}]}]
                events += [{'choices': [{'delta': {'tool_calls': [
                              {'index': 0, 'function': {'arguments': a}}]}}]} for a in args[1:]]
                events.append({'choices': [{'delta': {}, 'finish_reason': 'tool_calls'}]})
            else:
                pieces, finish = self.REPLIES.get(last, (['Fake ', 'says: ', last, '\nbye'], 'stop'))
                events = [{'choices': [{'delta': {'content': p}}]} for p in pieces]
                events.append({'choices': [{'delta': {}, 'finish_reason': finish}]})
                if body.get('stream_options', {}).get('include_usage'):
                    events.append({'choices': [], 'model': 'fake-1-served',
                                   'usage': {'prompt_tokens': 30, 'completion_tokens': 7,
                                             'prompt_tokens_details': {'cached_tokens': 20}}})
            writer.write(b'HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n'
                         b': keep-alive\n\n')
            for e in events:
                writer.write(b'data: ' + json.dumps(e).encode() + b'\n\n')
            writer.write(b'data: [DONE]\n\n')
        await writer.drain()
        writer.close()


class FakeBackendTest(BridgeTest):
    """A session with an OpenAI-compatible fake backend available as 'fake'."""

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


class Providers(FakeBackendTest):
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

    async def test_openai_tool_call(self):
        await self.c.ask('/model fake')
        text, calls, verb = await self.c.ask_tools('use tool', lambda n, a: ('ok', '1 @ECHO OFF'))
        self.assertEqual(verb, 'END')
        self.assertEqual(calls, [('memory_list', {}), ('read', {'path': 'C:\\A.BAT'})])
        body = self.fake.requests[-1][1]
        self.assertEqual([m['role'] for m in body['messages']], ['system', 'user', 'assistant', 'tool'])
        self.assertEqual(len(body['tools']), 17)
        self.assertEqual(body['messages'][2]['tool_calls'][0]['function']['name'], 'read')
        self.assertEqual(body['messages'][3], {'role': 'tool', 'tool_call_id': 'call_1',
                                               'content': '1 @ECHO OFF'})
        self.assertIn('Fake says: 1 @ECHO OFF', text)

    async def test_openai_bad_json_args(self):
        await self.c.ask('/model fake')
        _, calls, verb = await self.c.ask_tools('bad args', lambda n, a: ('ok', 'x'))
        self.assertEqual((calls, verb), ([('memory_list', {})], 'END'))
        self.assertTrue(self.fake.requests[-1][1]['messages'][-1]['content'].startswith('Error:'))

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


class DotEnv(unittest.TestCase):
    def test_load_env(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, '.env')
            with open(path, 'w', encoding='utf-8') as f:
                f.write('# comment\nCXT_ENV_A=plain\nexport CXT_ENV_B="quoted"\nCXT_ENV_C=from-file\njunk\n')
            os.environ['CXT_ENV_C'] = 'from-env'
            try:
                self.assertEqual(xtbridge.load_env(path), ['CXT_ENV_A', 'CXT_ENV_B'])
                self.assertEqual(os.environ['CXT_ENV_A'], 'plain')
                self.assertEqual(os.environ['CXT_ENV_B'], 'quoted')
                self.assertEqual(os.environ['CXT_ENV_C'], 'from-env')
            finally:
                for k in ('CXT_ENV_A', 'CXT_ENV_B', 'CXT_ENV_C'):
                    os.environ.pop(k, None)
        self.assertEqual(xtbridge.load_env(os.path.join(d, 'missing')), [])


class Replay(unittest.TestCase):
    LOG = """2026-10-24 22:45:23.660 I ('1.2.3.4', 2063) connected (connection #1, 1 open)
2026-10-24 22:46:05.000 I ('1.2.3.4', 2063) >> play something
2026-10-24 22:46:06.000 D ('1.2.3.4', 2063) > TXT Here
2026-10-24 22:46:06.500 D ('1.2.3.4', 2063) > NL
2026-10-24 22:46:07.000 D ('1.2.3.4', 2063) > TOOL t1 play
2026-10-24 22:46:07.000 D ('1.2.3.4', 2063) > ARG music T120 C\\nD
2026-10-24 22:46:07.000 D ('1.2.3.4', 2063) > ARG music  E
2026-10-24 22:46:07.000 D ('1.2.3.4', 2063) > CALL
2026-10-24 22:46:27.000 D ('1.2.3.4', 2063) < DONE t1 ok
2026-10-24 22:46:29.000 D ('1.2.3.4', 2063) > TXT done
2026-10-24 22:46:29.000 D ('1.2.3.4', 2063) > END
"""

    def test_parse(self):
        import replay
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'x.log')
            with open(path, 'w', encoding='utf-8') as f:
                f.write(self.LOG)
            turns = replay.parse_log(path)
        self.assertEqual(len(turns), 1)
        user, items = turns[0]
        self.assertEqual(user, 'play something')
        self.assertEqual([i for _, i in items[:2]], ['Here', '\n'])
        delay, call = items[2]
        self.assertEqual((call.name, call.args), ('play', {'music': 'T120 C\nD E'}))
        self.assertEqual(delay, 0.5)
        self.assertEqual(items[3], (2.0, 'done'))   # counted from DONE, not from CALL


class ConvLogTest(BridgeTest):
    """The JSONL conversation log (bridge/convlog.py)."""

    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.convlog = self.tmp.name
        await super().asyncSetUp()

    async def asyncTearDown(self):
        await super().asyncTearDown()
        self.tmp.cleanup()

    async def events(self):
        """Hang up, then read back the one log file."""
        self.c.writer.close()
        files = []
        for _ in range(50):                      # until the session has logged its end
            await asyncio.sleep(0.02)
            files = [os.path.join(d, f) for d, _, fs in os.walk(self.tmp.name) for f in fs]
            if files and 'session_end' in open(files[0], encoding='utf-8').read():
                break
        self.assertEqual(len(files), 1)
        with open(files[0], encoding='utf-8') as f:
            return [json.loads(line) for line in f]

    async def test_tool_turn(self):
        await self.c.ask_tools('!tool sysinfo', lambda n, a: ('ok', 'DOS 7.10'))
        await self.c.send('NEW')
        await self.c.line()
        ev = await self.events()
        for e in ev:
            self.assertIn('t', e)
        kinds = [e['type'] for e in ev]
        self.assertEqual(kinds[0], 'session_start')
        self.assertEqual(kinds[-1], 'session_end')
        self.assertEqual(ev[0]['provider'], 'mock')
        self.assertEqual(ev[kinds.index('user')]['text'], '!tool sysinfo')
        call = ev[kinds.index('tool_call')]
        self.assertEqual((call['name'], call['args']), ('sysinfo', {}))
        result = ev[kinds.index('tool_result')]
        self.assertEqual((result['id'], result['status'], result['text']), (call['id'], 'ok', 'DOS 7.10'))
        self.assertIsInstance(result['ms'], int)
        self.assertIn('api_call', kinds)
        self.assertIn('Calling sysinfo.', ''.join(e['text'] for e in ev if e['type'] == 'assistant'))
        end = ev[kinds.index('turn_end')]
        self.assertEqual(end['outcome'], 'ok')
        self.assertEqual(set(end['usage']), {'input', 'output', 'cache_read', 'cache_write'})
        self.assertIn('new_chat', kinds)
        self.assertLess(kinds.index('turn_end'), kinds.index('new_chat'))

    def test_path(self):
        import convlog
        log = convlog.open_log(self.tmp.name, ('172.16.0.229', 2063), 3)
        log.close()
        day, name = os.path.relpath(log.path, self.tmp.name).split(os.sep)
        self.assertRegex(day, r'^\d{4}-\d{2}-\d{2}$')
        self.assertRegex(name, r'^\d{6}-172\.16\.0\.229-c3\.jsonl$')


class ApiCallReports(unittest.IsolatedAsyncioTestCase):
    """Backends report each API round to chat.on_api_call."""

    async def test_anthropic_usage(self):
        usage = {'input_tokens': 120, 'output_tokens': 15, 'cache_read_input_tokens': 100,
                 'cache_creation_input_tokens': 4}
        fake = FakeAnthropic([TOOL_ROUND + (usage,), DONE_ROUND])
        chat = providers.AnthropicChat(fake, 'claude-test', 'low', {'name': 'Claude'})
        seen = []
        chat.on_api_call = seen.append
        await drive(chat, 'show me')
        self.assertEqual([r['round'] for r in seen], [0, 1])
        self.assertEqual(seen[0]['stop_reason'], 'tool_use')
        self.assertEqual(seen[0]['model'], 'claude-test-served')
        self.assertEqual(seen[0]['usage'], {'input': 120, 'output': 15, 'cache_read': 100, 'cache_write': 4})
        self.assertEqual(seen[1]['usage'], {'input': 0, 'output': 0, 'cache_read': 0, 'cache_write': 0})
        self.assertIsInstance(seen[0]['ms'], int)

    async def test_openai_usage(self):
        import httpx
        fake = FakeOpenAI()
        port = await fake.start()
        async with httpx.AsyncClient(base_url=f'http://127.0.0.1:{port}/v1/') as client:
            chat = providers.OpenAIChat(client, 'fake-1', {'name': 'Fakey'})
            seen = []
            chat.on_api_call = seen.append
            await drive(chat, 'hello')
        fake.server.close()
        self.assertTrue(fake.requests[0][1]['stream_options']['include_usage'])
        self.assertEqual(len(seen), 1)
        self.assertEqual((seen[0]['model'], seen[0]['stop_reason']), ('fake-1-served', 'stop'))
        self.assertEqual(seen[0]['usage'], {'input': 30, 'output': 7, 'cache_read': 20, 'cache_write': 0})


class Prompts(unittest.IsolatedAsyncioTestCase):
    """System prompt files (bridge/prompts/*.txt, bridge/prompts.py)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.write('default', 'Be brief.')
        self.write('tutor', 'You are {name}, a patient tutor.')

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, text):
        with open(os.path.join(self.dir, name + '.txt'), 'w', encoding='utf-8') as f:
            f.write(text + '\n')

    def test_names_and_load(self):
        import prompts
        self.assertEqual(prompts.names(self.dir), ['default', 'tutor'])
        self.assertEqual(prompts.load('tutor', self.dir), 'You are {name}, a patient tutor.')
        with self.assertRaises(prompts.PromptError) as cm:
            prompts.load('nope', self.dir)
        self.assertIn('default, tutor', str(cm.exception))

    def test_composed_prompt(self):
        system = providers.system_prompt({'name': 'Claude', 'prompt_text': 'Be {name}-like.'})
        self.assertTrue(system.startswith('Be Claude-like.\n\n'))   # the file is the prompt...
        self.assertIn('real IBM PC XT', system)                     # ...then the tools add-on
        self.assertNotIn('code page 437', system)                   # nothing else built in
        self.assertFalse(hasattr(providers, 'SYSTEM_PROMPT'))
        default = providers.system_prompt({'name': 'Claude'})        # no text given: default.txt
        self.assertTrue(default.startswith('You are Claude, chatting'))
        self.assertIn('code page 437', default)

    def test_backends_reload_each_conversation(self):
        table = providers.load_providers()
        b = providers.Backends(table, prompt='tutor', prompt_dir=self.dir)
        chat = b.make('anthropic')
        self.assertIn('Claude, a patient tutor.', chat.system)
        self.write('tutor', 'Answer in rhyme.')
        self.assertIn('Answer in rhyme.', b.make('anthropic').system)
        self.assertEqual(b.describe(chat), 'anthropic: claude-opus-5-5, prompt tutor')
        self.assertEqual(providers.Backends(table, prompt_dir=self.dir).describe(chat),
                         'anthropic: claude-opus-5-5')

    async def test_start_rejects_unknown_prompt(self):
        args = types.SimpleNamespace(host='127.0.0.1', port=0, mock=True, speed=0, token=None,
                                     config=None, prompt='nope', prompt_dir=self.dir)
        with self.assertRaises(providers.ConfigError):
            await xtbridge.start(args)


class Memories(FakeBackendTest):
    """The bridge lists the XT's memories at the start of each conversation on a
    backend with a system prompt, and adds them to it."""

    INDEX = 'TRAINS        41  Rich likes model trains'

    async def system_sent(self):
        head, body = self.fake.requests[-1]
        return body['messages'][0]['content']

    async def test_first_message_lists_memories(self):
        await self.c.ask('/model fake')
        seen = []

        def respond(name, args):
            seen.append(name)
            return 'ok', self.INDEX
        text, calls, verb = await self.c.ask_tools('hello', respond)
        self.assertEqual(verb, 'END')
        self.assertEqual(seen, ['memory_list'])
        system = await self.system_sent()
        self.assertTrue(system.startswith('You are Fakey'))
        self.assertIn('memory_write', system)
        self.assertTrue(system.endswith(self.INDEX))
        # not again for the second message, but again after NEW
        await self.c.ask_tools('again', respond)
        self.assertEqual(seen, ['memory_list'])
        self.assertTrue((await self.system_sent()).endswith(self.INDEX))
        await self.c.send('NEW')
        await self.c.line()
        await self.c.ask_tools('fresh', respond)
        self.assertEqual(seen, ['memory_list', 'memory_list'])

    async def test_failed_list_is_harmless(self):
        await self.c.ask('/model fake')
        text, calls, verb = await self.c.ask_tools('hello', lambda n, a: ('error', 'unknown tool memory_list'))
        self.assertEqual((text, verb), ('Fake says: hello\nbye', 'END'))
        self.assertIn('(none)', await self.system_sent())

    async def test_mock_does_not_list(self):
        text, calls, verb = await self.c.ask_tools('hello', lambda n, a: ('ok', 'x'))
        self.assertEqual((calls, verb), ([], 'END'))


class FakeAnthropic:
    """Duck-typed AsyncAnthropic: each stream() call plays the next scripted round,
    (texts, content blocks, stop_reason) or an exception, and records its kwargs."""

    def __init__(self, rounds, repeat_last=False):
        self.rounds = list(rounds)
        self.repeat_last = repeat_last
        self.requests = []
        self.beta = types.SimpleNamespace(messages=types.SimpleNamespace(stream=self.stream))

    def stream(self, **kw):
        self.requests.append(copy.deepcopy(kw))
        r = self.rounds[0] if self.repeat_last and len(self.rounds) == 1 else self.rounds.pop(0)
        fake = self

        class Stream:
            async def __aenter__(self):
                if isinstance(r, Exception):
                    raise r
                return self

            async def __aexit__(self, *exc):
                return False

            @property
            async def text_stream(self):
                for t in r[0]:
                    yield t

            async def get_final_message(self):
                usage = types.SimpleNamespace(**r[3]) if len(r) > 3 else None
                return types.SimpleNamespace(content=r[1], stop_reason=r[2], usage=usage,
                                             model='claude-test-served')
        del fake
        return Stream()


def text_block(t):
    return types.SimpleNamespace(type='text', text=t)


def tool_block(id, name, **args):
    return types.SimpleNamespace(type='tool_use', id=id, name=name, input=args)


TOOL_ROUND = (['Looking.'], [text_block('Looking.'), tool_block('toolu_1', 'read', path='C:\\A.BAT')],
              'tool_use')
DONE_ROUND = (['done'], [text_block('done')], 'end_turn')


async def drive(chat, text, status='ok', stop_at_tool=False):
    """Run chat.reply, answering each ToolCall; returns the yielded items."""
    items = []
    gen = chat.reply(text)
    async for item in gen:
        items.append(item)
        if isinstance(item, ToolCall):
            if stop_at_tool:
                await gen.aclose()
                break
            item.result = ToolResult(status, f'result of {item.name}')
    return items


class OpenAISettle(unittest.IsolatedAsyncioTestCase):
    async def test_cancel_after_first_of_two_calls(self):
        chat = providers.OpenAIChat(None, 'm', {'name': 'X'})

        async def fake_stream(body, parts, calls):
            calls[0] = {'id': 'a', 'name': 'sysinfo', 'arguments': '{}'}
            calls[1] = {'id': 'b', 'name': 'dir', 'arguments': '{"path": "C:\\\\"}'}
            chat._finish = 'tool_calls'
            return
            yield
        chat._stream = fake_stream
        gen = chat.reply('go')
        first = await gen.__anext__()
        first.result = ToolResult('ok', 'DOS')
        second = await gen.__anext__()
        self.assertEqual(second.name, 'dir')
        await gen.aclose()                     # Esc while the second call runs
        roles = [(m['role'], m.get('tool_call_id')) for m in chat.messages]
        self.assertEqual(roles, [('user', None), ('assistant', None), ('tool', 'a'), ('tool', 'b')])
        self.assertEqual(chat.messages[-1]['content'], 'Interrupted by the user.')


class AnthropicTools(unittest.IsolatedAsyncioTestCase):
    def chat(self, rounds, **kw):
        self.fake = FakeAnthropic(rounds, **kw)
        return providers.AnthropicChat(self.fake, 'claude-test', 'low', {'name': 'Claude'})

    async def test_tool_round(self):
        chat = self.chat([TOOL_ROUND, DONE_ROUND])
        items = await drive(chat, 'show me')
        self.assertEqual(items[0], 'Looking.')
        self.assertIsInstance(items[1], ToolCall)
        self.assertEqual((items[1].name, items[1].args), ('read', {'path': 'C:\\A.BAT'}))
        self.assertEqual(items[2:], ['done'])
        req = self.fake.requests[1]
        self.assertEqual(len(req['tools']), 17)
        msgs = req['messages']
        self.assertEqual([m['role'] for m in msgs], ['user', 'assistant', 'user'])
        self.assertEqual(msgs[2]['content'], [{'type': 'tool_result', 'tool_use_id': 'toolu_1',
                                               'content': 'result of read', 'is_error': False}])
        self.assertIn('real IBM PC XT', req['system'])

    async def test_parallel_tool_uses(self):
        both = ([], [tool_block('a', 'sysinfo'), tool_block('b', 'dir', path='C:\\')], 'tool_use')
        chat = self.chat([both, DONE_ROUND])
        items = await drive(chat, 'go', status='denied')
        self.assertEqual([i.name for i in items if isinstance(i, ToolCall)], ['sysinfo', 'dir'])
        results = self.fake.requests[1]['messages'][2]['content']
        self.assertEqual([r['tool_use_id'] for r in results], ['a', 'b'])
        self.assertTrue(all(r['is_error'] for r in results))
        self.assertEqual(results[0]['content'], 'The user denied this tool call.')

    async def test_round_cap(self):
        chat = self.chat([TOOL_ROUND], repeat_last=True)
        await drive(chat, 'loop forever')
        self.assertEqual(len(self.fake.requests), tools.MAX_ROUNDS + 1)
        last = self.fake.requests[-1]
        self.assertEqual(last['tool_choice'], {'type': 'none'})
        self.assertNotIn('tool_choice', self.fake.requests[0])
        self.assertEqual(last['messages'][-1]['content'][-1],
                         {'type': 'text', 'text': 'Tool limit reached; answer now without tools.'})

    async def test_settle_after_cancel(self):
        chat = self.chat([TOOL_ROUND, DONE_ROUND])
        await drive(chat, 'show me', stop_at_tool=True)
        last = chat.messages[-1]
        self.assertEqual(last['role'], 'user')
        self.assertEqual(last['content'], [{'type': 'tool_result', 'tool_use_id': 'toolu_1',
                                            'content': 'Interrupted by the user.', 'is_error': True}])
        await drive(chat, 'next')
        sent = self.fake.requests[-1]['messages'][-1]['content']
        self.assertEqual(sent[0]['type'], 'tool_result')
        self.assertEqual(sent[-1], {'type': 'text', 'text': 'next'})

    async def test_error_before_any_reply_pops_user(self):
        chat = self.chat([RuntimeError('boom')])
        with self.assertRaises(RuntimeError):
            await drive(chat, 'hi')
        self.assertEqual(chat.messages, [])

    async def test_error_after_merge_removes_only_text(self):
        chat = self.chat([TOOL_ROUND, RuntimeError('boom')])
        await drive(chat, 'show me', stop_at_tool=True)
        before = copy.deepcopy(chat.messages)
        with self.assertRaises(RuntimeError):
            await drive(chat, 'again')
        self.assertEqual(chat.messages, before)

    async def test_max_tokens_mid_tool_use_keeps_history_valid(self):
        cut = (['Drawing.'], [text_block('Drawing.'), tool_block('toolu_9', 'draw', script='SCR')],
               'max_tokens')
        chat = self.chat([cut, DONE_ROUND])
        items = await drive(chat, 'draw a lot')
        self.assertFalse(any(isinstance(i, ToolCall) for i in items))
        self.assertEqual(chat.messages[-1]['role'], 'user')
        self.assertEqual(chat.messages[-1]['content'][0]['tool_use_id'], 'toolu_9')
        self.assertTrue(chat.messages[-1]['content'][0]['is_error'])
        await drive(chat, 'next')
        self.assertEqual(self.fake.requests[-1]['messages'][-1]['content'][-1],
                         {'type': 'text', 'text': 'next'})

    async def test_transcript_flattens_tools(self):
        chat = self.chat([TOOL_ROUND, DONE_ROUND])
        await drive(chat, 'show me')
        self.assertEqual(providers.transcript(chat),
                         [('user', 'show me'), ('assistant', 'Looking.\n[read C:\\A.BAT -> ok]\ndone')])


if __name__ == '__main__':
    logging.disable(logging.CRITICAL)
    unittest.main()
