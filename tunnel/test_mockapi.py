"""Tests for the mock Messages API server (python3 tunnel/test_mockapi.py)."""

import contextlib
import http.client
import io
import json
import os
import socket
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mockapi  # noqa: E402
import mockchat  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sse_events(body):
    """(event, data dict) pairs from a text/event-stream body."""
    out = []
    for block in body.decode('utf-8').split('\n\n'):
        event, data = None, []
        for line in block.split('\n'):
            if line.startswith('event:'):
                event = line[6:].strip()
            elif line.startswith('data:'):
                data.append(line[5:].strip())
        if data:
            out.append((event, json.loads('\n'.join(data))))
    return out


def reply(events):
    """(text, blocks, stop_reason, tool input text) reassembled from events. The blocks are as the DOS PC
    stores them (and the API wants them back): thinking and text filled in, a tool_use with its input."""
    blocks, text, stop, tool_json = [], [], None, []
    for event, d in events:
        if event == 'content_block_start':
            blocks.append(dict(d['content_block']))
        elif event == 'content_block_delta':
            delta = d['delta']
            if delta['type'] == 'text_delta':
                text.append(delta['text'])
                blocks[-1]['text'] += delta['text']
            elif delta['type'] == 'thinking_delta':
                blocks[-1]['thinking'] += delta['thinking']
            elif delta['type'] == 'signature_delta':
                blocks[-1]['signature'] = blocks[-1].get('signature', '') + delta['signature']
            elif delta['type'] == 'input_json_delta':
                tool_json.append(delta['partial_json'])
        elif event == 'content_block_stop' and blocks[d['index']]['type'] == 'tool_use':
            blocks[d['index']]['input'] = json.loads(''.join(tool_json))
        elif event == 'message_delta':
            stop = d['delta'].get('stop_reason')
    return ''.join(text), blocks, stop, ''.join(tool_json)


def tool_result(block, content='ok', is_error=False):
    return {'type': 'tool_result', 'tool_use_id': block['id'], 'content': content, 'is_error': is_error}


def tool_block(blocks):
    return [b for b in blocks if b['type'] == 'tool_use'][0]


class MockApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = mockapi.make_server(0, speed=0)
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def post(self, messages, raw=None, extra=None, headers=None):
        """POST a valid request (headers and fields as HARNESS.EXE sends them) with `extra` fields added
        and `headers` replacing the defaults; returns (status, response headers, body)."""
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=10)
        fields = dict({'model': 'mock', 'max_tokens': 16000, 'stream': True, 'messages': messages}, **(extra or {}))
        body = raw if raw is not None else json.dumps(fields).encode()
        sent = {'Content-Type': 'application/json', 'anthropic-version': '2023-06-01'}
        sent.update(headers or {})
        with contextlib.redirect_stdout(self.log):
            conn.request('POST', '/v1/messages', body=body, headers=sent)
            r = conn.getresponse()
            data = r.read()
        response_headers = dict((k.lower(), v) for k, v in r.getheaders())
        conn.close()
        return r.status, response_headers, data

    def setUp(self):
        self.log = io.StringIO()     # what the server prints during a request: the mock's log

    def turn(self, text, choice=lambda calls: None, extra=None, headers=None):
        """Play a whole turn the way HARNESS.EXE does, echoing each reply back, answering every tool call
        ok; `choice(n)` gives the tool_choice for the request after n tool calls. Returns the replies as
        (text, blocks, stop) and the final messages."""
        messages, replies = [{'role': 'user', 'content': text}], []
        for calls in range(40):
            fields = dict(extra or {})
            if choice(calls):
                fields['tool_choice'] = choice(calls)
            status, _, data = self.post(messages, extra=fields, headers=headers)
            self.assertEqual(status, 200, data)
            said, blocks, stop, _ = reply(sse_events(data))
            replies.append((said, blocks, stop))
            if stop != 'tool_use':
                return replies, messages
            messages += [{'role': 'assistant', 'content': blocks},
                         {'role': 'user', 'content': [tool_result(tool_block(blocks), 'sysinfo says hi')]}]
        self.fail('the turn never ended')

    def ask(self, text):
        status, headers, data = self.post([{'role': 'user', 'content': text}])
        self.assertEqual(status, 200)
        self.assertEqual(headers.get('transfer-encoding'), 'chunked')
        return sse_events(data)

    def test_hello_streams_text_with_a_thinking_block(self):
        events = self.ask('hello')
        self.assertEqual(events[0][0], 'message_start')
        self.assertEqual(events[-1][0], 'message_stop')
        text, blocks, stop, _ = reply(events)
        self.assertEqual(blocks[0]['type'], 'thinking')
        self.assertGreaterEqual(len(blocks[0]['signature']), 1500)
        self.assertIn('Hello from the mock', text)
        self.assertEqual(stop, 'end_turn')
        self.assertTrue(all(d.get('type') for _, d in events))

    def test_tool_call_round_trip(self):
        events = self.ask('!tool read path=C:\\A.TXT')
        text, blocks, stop, tool_json = reply(events)
        self.assertEqual(stop, 'tool_use')
        tool = [b for b in blocks if b['type'] == 'tool_use'][0]
        self.assertEqual(tool['name'], 'read')
        self.assertEqual(tool['input'], {'path': 'C:\\A.TXT'})
        self.assertEqual(json.loads(tool_json), tool['input'])
        self.assertIn('Calling read.', text)
        status, _, data = self.post([{'role': 'user', 'content': '!tool read path=C:\\A.TXT'},
                                     {'role': 'assistant', 'content': blocks},
                                     {'role': 'user', 'content': [tool_result(tool, '    1 hi')]}])
        self.assertEqual(status, 200)
        text2, _, stop2, _ = reply(sse_events(data))
        self.assertIn('read -> ok:     1 hi', text2)
        self.assertEqual(stop2, 'end_turn')

    def test_tool_values_have_the_schema_types(self):
        events = self.ask('!tool read path=C:\\A.TXT start=3 lines=x')
        _, _, _, tool_json = reply(events)
        self.assertEqual(json.loads(tool_json), {'path': 'C:\\A.TXT', 'start': 3, 'lines': 'x'})
        events = self.ask('!tool write path=C:\\A.TXT text=hi append=true')
        self.assertEqual(json.loads(reply(events)[3]), {'path': 'C:\\A.TXT', 'text': 'hi', 'append': True})

    def test_tour_arguments_match_the_schemas(self):
        json_types = {'string': str, 'integer': int, 'boolean': bool}
        types = mockchat.schema_types()
        for name, args, _, _ in mockchat.tour_steps():
            for key, value in args.items():
                kind = types[name].get(key)
                self.assertIsNotNone(kind, f'{name}.{key} is not in TOOLS.JSN')
                self.assertIs(type(value), json_types[kind], f'{name}.{key}={value!r} is not {kind}')

    def test_tooltour_sends_progress_notes_before_its_tool_calls_with_display_updates(self):
        both = {'anthropic-beta': f'{mockapi.BETA_PROGRESS},{mockapi.BETA_BINDING}'}
        thinking = {'type': 'adaptive', 'display': 'updates',
                    'block_binding': {'prefix_mismatch_behavior': 'drop_block'}}
        replies, _ = self.turn('!tooltour', extra={'thinking': thinking}, headers=both)
        self.assertIn('tool tour: ', self.log.getvalue())       # the canned results fail it; the notes are the point
        self.assertNotIn('VALIDATION', self.log.getvalue())
        steps = mockchat.tour_steps()
        notes = [blocks[0]['thinking'] for _, blocks, _ in replies]
        self.assertEqual(len(notes), len(steps) + 1)
        self.assertEqual(notes[0], f'Tour step 1 of {len(steps)}: {steps[0][0]}.')
        self.assertEqual(notes[-1], 'Tour done; the verdict next.')
        for (_, blocks, stop), note in zip(replies, notes):
            self.assertEqual(blocks[0]['type'], 'thinking')     # the note comes first, ahead of the call
            if stop == 'tool_use':
                self.assertIn(f': {tool_block(blocks)["name"]}.', note)

    def test_progress_notes_are_empty_without_display_updates(self):
        replies, _ = self.turn('!tooltour')
        self.assertIn('tool tour: ', self.log.getvalue())
        self.assertEqual({blocks[0]['thinking'] for _, blocks, _ in replies}, {''})

    def test_message_start_carries_input_transformations_with_the_binding_beta(self):
        user = [{'role': 'user', 'content': 'hello'}]
        binding = {'type': 'adaptive', 'block_binding': {'prefix_mismatch_behavior': 'drop_block'}}
        status, _, data = self.post(user, extra={'thinking': binding},
                                    headers={'anthropic-beta': mockapi.BETA_BINDING})
        self.assertEqual(status, 200, data)
        self.assertEqual(sse_events(data)[0][1]['message']['input_transformations'], [])
        status, _, data = self.post([{'role': 'user', 'content': 'hello again'}])
        self.assertNotIn('input_transformations', sse_events(data)[0][1]['message'])

    def test_retry_is_529_then_200(self):
        status, headers, data = self.post([{'role': 'user', 'content': '!retry'}])
        self.assertEqual(status, 529)
        self.assertEqual(headers.get('retry-after'), '1')
        self.assertEqual(headers.get('content-length'), str(len(data)))
        self.assertEqual(json.loads(data)['error']['type'], 'overloaded_error')
        text, _, _, _ = reply(self.ask('!retry'))
        self.assertIn('second attempt', text)

    def test_chunks_arrive_whole(self):
        text, _, stop, _ = reply(self.ask('!chunks'))
        self.assertEqual(text, mockapi.CHUNKS_TEXT)
        self.assertEqual(stop, 'end_turn')

    def test_error_event(self):
        events = self.ask('!error')
        self.assertEqual(events[-1][0], 'error')
        self.assertEqual(events[-1][1]['error']['type'], 'overloaded_error')

    def test_overload_is_an_error_before_any_text_then_a_reply(self):
        events = self.ask('!overload')
        self.assertEqual(events[-1][0], 'error')
        self.assertEqual(events[-1][1]['error']['type'], 'overloaded_error')
        self.assertEqual(reply(events)[0], '')
        text, _, stop, _ = reply(self.ask('!overload'))
        self.assertIn('after the overload', text)
        self.assertEqual(stop, 'end_turn')

    def test_badstream_is_an_invalid_request_error_before_any_text(self):
        events = self.ask('!badstream')
        self.assertEqual(events[-1][0], 'error')
        self.assertEqual(events[-1][1]['error']['type'], 'invalid_request_error')
        self.assertEqual(reply(events)[0], '')

    def test_refusal_and_cutoff(self):
        self.assertEqual(reply(self.ask('!refuse'))[2], 'refusal')
        self.assertEqual(reply(self.ask('!cutoff'))[2], 'max_tokens')

    def test_runrefuse_is_a_full_memory_run_then_a_refusal(self):
        replies, _ = self.turn('!runrefuse')
        call = tool_block(replies[0][1])
        self.assertEqual((call['name'], call['input']), ('run', {'command': 'VER', 'full_memory': True}))
        self.assertEqual([r[2] for r in replies], ['tool_use', 'refusal'])

    def test_bigevent_is_one_huge_delta(self):
        events = self.ask('!bigevent')
        sizes = [len(d['delta']['text']) for e, d in events
                 if e == 'content_block_delta' and d['delta']['type'] == 'text_delta']
        self.assertGreater(max(sizes), 16384)

    def test_bad_requests_are_400(self):
        status, _, data = self.post(None, raw=b'{not json')
        self.assertEqual(status, 400)
        self.assertIn('error', json.loads(data))
        status, _, data = self.post([{'role': 'user', 'content': 'a'}, {'role': 'user', 'content': 'b'}])
        self.assertEqual(status, 400)
        self.assertIn('alternate', json.loads(data)['error']['message'])

    def test_a_violation_is_a_400_in_the_apis_shape_and_a_validation_line_in_the_log(self):
        messages = [{'role': 'user', 'content': 'a'}, {'role': 'assistant', 'content': ' '}]
        status, headers, data = self.post(messages + [{'role': 'user', 'content': 'b'}])
        self.assertEqual(status, 400)
        self.assertEqual(headers['content-length'], str(len(data)))
        error = json.loads(data)
        self.assertEqual(error['type'], 'error')
        self.assertEqual(error['error']['type'], 'invalid_request_error')
        self.assertIn('messages.1.content', error['error']['message'])
        line = [x for x in self.log.getvalue().splitlines() if x.startswith('VALIDATION:')]
        self.assertEqual(len(line), 1)
        self.assertIn('messages.1.content', line[0])
        self.assertIn('user[text] assistant[text] user[text]', line[0])    # the shape of the request

    def test_header_violations_are_400_over_http(self):
        status, _, data = self.post([{'role': 'user', 'content': 'a'}], headers={'anthropic-version': '1999-01-01'})
        self.assertEqual(status, 400)
        self.assertIn('anthropic-version', json.loads(data)['error']['message'])
        status, _, data = self.post([{'role': 'user', 'content': 'a'}], headers={'Content-Type': 'text/plain'})
        self.assertEqual(status, 400)
        self.assertIn('content-type', json.loads(data)['error']['message'])

    def test_tool_choice_none_means_no_tool_call(self):
        replies, _ = self.turn('!tool read path=C:\\A.TXT', choice=lambda calls: {'type': 'none'})
        self.assertEqual([r[2] for r in replies], ['end_turn'])
        self.assertFalse([b for b in replies[0][1] if b['type'] == 'tool_use'])
        self.assertIn('read -> error: tool_choice is none', replies[0][0])
        replies, _ = self.turn('!tool read path=C:\\A.TXT', choice=lambda calls: {'type': 'auto'})
        self.assertEqual([r[2] for r in replies], ['tool_use', 'end_turn'])

    def test_loop_calls_a_tool_until_tool_choice_is_none(self):
        # as HARNESS.EXE does: 25 rounds with tools, the 26th with tool_choice none
        auto = {'type': 'auto', 'disable_parallel_tool_use': True}
        replies, messages = self.turn('!loop', choice=lambda calls: {'type': 'none'} if calls >= 25 else auto)
        self.assertEqual(len(replies), 26)
        self.assertEqual([r[2] for r in replies], ['tool_use'] * 25 + ['end_turn'])
        self.assertIn('after 25 tool calls', replies[-1][0])
        self.assertIn('loop: 25 tool calls, then tool_choice none', self.log.getvalue())

    def test_ping_events_come_between_the_deltas(self):
        events = self.ask('!ping')
        kinds = [e for e, _ in events]
        self.assertGreaterEqual(kinds.count('ping'), 6)
        texts = [i for i, (e, d) in enumerate(events)
                 if e == 'content_block_delta' and d['delta']['type'] == 'text_delta']
        between = kinds[texts[0]:texts[-1]]
        self.assertEqual(between.count('ping'), len(texts) - 1)        # one before each delta after the first
        self.assertEqual(reply(events)[0], 'Ping events between the text deltas. ')

    def test_think_streams_thinking_text_then_a_signature_and_gets_it_back(self):
        events = self.ask('!think')
        deltas = [d['delta']['type'] for e, d in events if e == 'content_block_delta']
        self.assertEqual(deltas[:4], ['thinking_delta'] * 3 + ['signature_delta'])
        text, blocks, _, _ = reply(events)
        self.assertEqual(blocks[0]['type'], 'thinking')
        self.assertEqual(blocks[0]['thinking'], mockchat.THINKING_TEXT)
        self.assertGreaterEqual(len(blocks[0]['signature']), 1500)
        self.assertIn('had text and a signature', text)
        # echoed back whole it is accepted; changed, it is not
        messages = [{'role': 'user', 'content': '!think'}, {'role': 'assistant', 'content': blocks},
                    {'role': 'user', 'content': 'more'}]
        self.assertEqual(self.post(messages)[0], 200)
        blocks[0] = dict(blocks[0], thinking=blocks[0]['thinking'][:-1])
        status, _, data = self.post(messages)
        self.assertEqual(status, 400)
        self.assertIn('did not come back unchanged', json.loads(data)['error']['message'])

    def test_thinktool_escapes_come_back_through_the_validator(self):
        events = self.ask('!thinktool')
        thinking = reply(events)[1][0]['thinking']
        self.assertEqual(thinking, mockchat.THINKING_ESCAPES)
        for char in '"\\\u00e9\u00fc':
            self.assertIn(char, thinking)
        replies, _ = self.turn('!thinktool')
        self.assertEqual([r[2] for r in replies], ['tool_use', 'end_turn'])
        self.assertIn('thinking round trip: PASS', self.log.getvalue())

    def wire(self, header_length, body=None, timeout=0.5):
        """POST `body` (a valid request) over a raw socket claiming Content-Length `header_length`;
        returns (status line, response body) and leaves the mock's log in self.log."""
        body = body or json.dumps({'model': 'mock', 'max_tokens': 99, 'stream': True,
                                   'messages': [{'role': 'user', 'content': 'hello'}]}).encode()
        header_length = len(body) if header_length is None else header_length
        head = (f'POST /v1/messages HTTP/1.1\r\nHost: x\r\nanthropic-version: 2023-06-01\r\n'
                f'Content-Type: application/json\r\nContent-Length: {header_length}\r\n\r\n').encode()
        self.server.body_timeout = timeout
        try:
            with contextlib.redirect_stdout(self.log), socket.create_connection(('127.0.0.1', self.port), 10) as conn:
                conn.sendall(head + body)
                data = b''
                while not data.endswith(b'\r\n0\r\n\r\n'):
                    chunk = conn.recv(65536)
                    if not chunk:
                        break
                    data += chunk
        finally:
            self.server.body_timeout = mockapi.BODY_TIMEOUT
        return data.split(b'\r\n', 1)[0].decode(), data.split(b'\r\n\r\n', 1)[1].decode()

    def test_a_content_length_that_matches_is_answered(self):
        self.assertEqual(self.wire(None)[0], 'HTTP/1.1 200 OK')

    def test_a_content_length_larger_than_the_body_is_a_400_after_the_timeout(self):
        started = time.time()
        status, body = self.wire(5000)
        self.assertLess(time.time() - started, 5)
        self.assertEqual(status, 'HTTP/1.1 400 Bad Request')
        self.assertIn('Content-Length', json.loads(body)['error']['message'])
        self.assertTrue([x for x in self.log.getvalue().splitlines() if x.startswith('VALIDATION: Content-Length')])

    def test_a_content_length_smaller_than_the_body_is_a_400(self):
        status, body = self.wire(40)
        self.assertEqual(status, 'HTTP/1.1 400 Bad Request')
        self.assertIn('Content-Length', json.loads(body)['error']['message'])
        self.assertTrue([x for x in self.log.getvalue().splitlines() if x.startswith('VALIDATION: Content-Length')])

    def test_a_content_length_that_is_not_a_number_is_a_400(self):
        status, body = self.wire('+12')
        self.assertEqual(status, 'HTTP/1.1 400 Bad Request')
        self.assertIn('Content-Length', json.loads(body)['error']['message'])

    def test_redacted_thinking_then_a_tool_call_then_the_check(self):
        events = self.ask('!redacted')
        _, blocks, stop, _ = reply(events)
        self.assertEqual(blocks[0], {'type': 'redacted_thinking', 'data': mockchat.REDACTED_DATA})
        self.assertEqual(stop, 'tool_use')
        replies, _ = self.turn('!redacted')
        self.assertEqual([r[2] for r in replies], ['tool_use', 'end_turn'])
        self.assertIn('redacted thinking round trip: PASS', self.log.getvalue())
        self.assertIn('came back unchanged', replies[-1][0])

    def test_http_errors_come_once_with_retry_after_then_a_normal_reply(self):
        for command, status, kind in (('!400', 400, 'invalid_request_error'), ('!401', 401, 'authentication_error'),
                                      ('!429', 429, 'rate_limit_error')):
            got, headers, data = self.post([{'role': 'user', 'content': command}])
            self.assertEqual(got, status, command)
            self.assertEqual(headers.get('retry-after'), '1')
            self.assertEqual(headers.get('content-length'), str(len(data)))
            self.assertEqual(json.loads(data)['error']['type'], kind)
            text, _, stop, _ = reply(self.ask(command))
            self.assertIn(f'first request got a {status}', text)
            self.assertEqual(stop, 'end_turn')

    def test_busy_is_529_three_times_then_a_reply(self):
        for attempt in (1, 2, 3):
            status, headers, data = self.post([{'role': 'user', 'content': '!busy'}])
            self.assertEqual(status, 529)
            self.assertEqual(headers.get('retry-after'), '1')
            self.assertIn(f'attempt {attempt} of 3', json.loads(data)['error']['message'])
        self.assertIn('after three 529s', reply(self.ask('!busy'))[0])

    def test_help_lists_every_scenario(self):
        text = reply(self.ask('!help'))[0]
        for command in mockchat.SCENARIOS:
            self.assertIn(command, text)
        for command in ('!loop', '!ping', '!think', '!redacted', '!400', '!401', '!429', '!busy'):
            self.assertIn(command, mockchat.SCENARIOS)
            self.assertTrue(hasattr(mockchat.MockChat, '_cmd_' + command[1:]))

    def test_history_is_read_from_the_request(self):
        status, _, data = self.post([{'role': 'user', 'content': 'first'},
                                     {'role': 'assistant', 'content': [{'type': 'text', 'text': 'ok'}]},
                                     {'role': 'user', 'content': '!history'}])
        self.assertEqual(status, 200)
        text, _, _, _ = reply(sse_events(data))
        self.assertIn('1 earlier message', text)
        self.assertIn('first', text)

    def bench(self, read_result):
        """Run !bench to the end, answering every read with read_result; the calls, the final reply's
        text, stop reason and text deltas."""
        messages = [{'role': 'user', 'content': '!bench'}]
        calls, deltas = [], []
        for _ in range(30):
            status, _, data = self.post(messages)
            self.assertEqual(status, 200)
            events = sse_events(data)
            text, blocks, stop, tool_json = reply(events)
            if stop != 'tool_use':
                deltas = [d['delta']['text'] for e, d in events
                          if e == 'content_block_delta' and d['delta']['type'] == 'text_delta']
                break
            tool = tool_block(blocks)
            calls.append((tool['name'], json.loads(tool_json)))
            messages += [{'role': 'assistant', 'content': blocks},
                         {'role': 'user', 'content': [tool_result(tool, read_result if tool['name'] == 'read'
                                                                  else 'VER\nFreeDOS kernel')]}]
        return calls, text, stop, deltas

    def test_bench_is_twenty_reads_a_full_memory_run_and_a_long_reply(self):
        with open(os.path.join(ROOT, 'image', 'tests', 'bench', 'BENCH.TXT'), encoding='ascii') as f:
            lines = f.read().splitlines()
        whole = ''.join(f'{n:5} {line}\n' for n, line in enumerate(lines[:59], 1)) + mockchat.BENCH_READ_END + '\n'
        self.assertLessEqual(len(whole), 4096)
        calls, text, stop, deltas = self.bench(whole)
        self.assertEqual(calls, [('read', {'path': mockchat.BENCH_FILE})] * 20
                         + [('run', {'command': 'VER', 'full_memory': True})])
        self.assertEqual(stop, 'end_turn')
        self.assertEqual(text, mockchat.BENCH_REPLY)
        self.assertTrue(3900 <= len(text) <= 4300)
        self.assertEqual(len({len(d) for d in deltas[:-1]}), 1)    # fixed-size pieces: no randomness
        self.assertIn('bench: PASS', self.log.getvalue())

    def test_bench_fails_on_a_cut_read_result(self):
        self.bench('    1 Bench line 01: the quick brown fox jumps over the lazy dog 0\n')
        self.assertIn('bench: FAIL call 1 read', self.log.getvalue())


SIG = 'sig-' * 500
THINKING = {'type': 'thinking', 'thinking': 'hm', 'signature': SIG}
REDACTED = {'type': 'redacted_thinking', 'data': 'opaque' * 50}
CALL = {'type': 'tool_use', 'id': 'toolu_1', 'name': 'sysinfo', 'input': {}}
USER = {'role': 'user', 'content': 'go'}


def assistant(*blocks):
    return {'role': 'assistant', 'content': list(blocks)}


def results(*ids, after=()):
    return {'role': 'user', 'content': [{'type': 'tool_result', 'tool_use_id': i, 'content': 'x'} for i in ids]
            + list(after)}


class ValidatorTest(unittest.TestCase):
    """The strict request check, rule by rule: each has a request it accepts and one it rejects."""

    def setUp(self):
        self.v = mockapi.Validator(mockapi.DEFAULT_TOOLS)
        with open(mockapi.DEFAULT_TOOLS, encoding='utf-8') as f:
            self.tools = json.load(f)

    def fresh(self):
        """A conversation of its own, so what one check changes does not warn in the next."""
        self.count = getattr(self, 'count', 0) + 1
        return [{'role': 'user', 'content': f'go {self.count}'}]

    def request(self, messages=None, **fields):
        """A valid request with `fields` changed (None removes one): (headers, body)."""
        req = {'model': 'm', 'max_tokens': 100, 'stream': True,
               'messages': self.fresh() if messages is None else messages}
        for k, v in fields.items():
            if v is None:
                req.pop(k, None)
            else:
                req[k] = v
        body = json.dumps(req).encode()
        return {'Anthropic-Version': '2023-06-01', 'Content-Type': 'application/json',
                'Content-Length': str(len(body))}, body

    def ok(self, messages=None, **fields):
        return self.v.check(*self.request(messages, **fields))

    def bad(self, fragment, messages=None, **fields):
        with self.assertRaises(mockapi.BadRequest) as cm:
            self.v.check(*self.request(messages, **fields))
        self.assertIn(fragment, str(cm.exception))

    def test_headers(self):
        headers, body = self.request()
        self.v.check(dict(headers, **{'Content-Type': 'application/json; charset=utf-8'}), body)
        for name, value, fragment in (('Anthropic-Version', '2024-01-01', 'anthropic-version'),
                                      ('Content-Type', 'text/plain', 'content-type'),
                                      ('Content-Length', str(len(body) + 1), 'Content-Length')):
            with self.assertRaises(mockapi.BadRequest) as cm:
                self.v.check(dict(headers, **{name: value}), body)
            self.assertIn(fragment, str(cm.exception))
        for name in ('Anthropic-Version', 'Content-Type', 'Content-Length'):
            with self.assertRaises(mockapi.BadRequest):
                self.v.check({k: v for k, v in headers.items() if k != name}, body)

    def test_body_must_be_json_with_the_required_fields(self):
        self.ok()
        headers, _ = self.request()
        for body in (b'{not json', b'[]', json.dumps({'model': 'm'}).encode() + b' x'):
            with self.assertRaises(mockapi.BadRequest):
                self.v.check(dict(headers, **{'Content-Length': str(len(body))}), body)
        self.bad('model', model=None)
        self.bad('max_tokens', max_tokens=None)
        self.bad('max_tokens', max_tokens='100')
        self.bad('max_tokens', max_tokens=0)
        headers, body = self.request()
        body = json.dumps({k: v for k, v in json.loads(body).items() if k != 'messages'}).encode()
        with self.assertRaises(mockapi.BadRequest) as cm:
            self.v.check(dict(headers, **{'Content-Length': str(len(body))}), body)
        self.assertIn('messages', str(cm.exception))

    def test_stream_must_be_true(self):
        self.ok(stream=True)
        self.bad('stream', stream=False)
        self.bad('stream', stream=None)

    def test_tools_must_equal_tools_jsn_when_present(self):
        self.ok(tools=None)
        self.ok(tools=self.tools)
        self.bad('tools', tools=self.tools[:-1])
        self.bad('tools', tools=[dict(self.tools[0], description='changed')] + self.tools[1:])

    def test_tool_choice_shapes(self):
        for choice in ({'type': 'auto'}, {'type': 'auto', 'disable_parallel_tool_use': True}, {'type': 'any'},
                       {'type': 'tool', 'name': 'sysinfo'}, {'type': 'none'}):
            self.ok(tools=self.tools, tool_choice=choice)
        for choice in ('auto', {'type': 'sometimes'}, {'type': 'none', 'disable_parallel_tool_use': True},
                       {'type': 'auto', 'disable_parallel_tool_use': 'yes'}, {'type': 'tool'},
                       {'type': 'tool', 'name': 'nonesuch'}, {'type': 'auto', 'extra': 1}):
            self.bad('tool_choice', tools=self.tools, tool_choice=choice)

    def test_system_is_a_string_or_text_blocks(self):
        self.ok(system='be brief')
        self.ok(system=[{'type': 'text', 'text': 'be brief', 'cache_control': {'type': 'ephemeral'}}])
        self.bad('system', system=[{'type': 'text', 'text': ''}])
        self.bad('system', system=7)

    def test_first_message_is_the_users_and_roles_alternate(self):
        self.ok([USER, assistant({'type': 'text', 'text': 'a'}), USER])
        self.bad('alternate', [assistant({'type': 'text', 'text': 'a'}), USER])
        self.bad('alternate', [USER, USER])
        said = {'type': 'text', 'text': 'a'}
        self.bad('alternate', [USER, assistant(said), assistant(said)])
        self.bad('last message', [USER, assistant({'type': 'text', 'text': 'a'})])
        self.bad('messages', [])
        self.bad('role', [{'role': 'system', 'content': 'x'}])

    def test_no_empty_text_blocks(self):
        self.ok([{'role': 'user', 'content': [{'type': 'text', 'text': 'x'}]}])
        for content in ('', '  \n', [{'type': 'text', 'text': ''}], [{'type': 'text', 'text': ' '}], []):
            self.bad('messages.0', [{'role': 'user', 'content': content}])
        self.bad('messages.2.content.1.text', [USER, assistant({'type': 'text', 'text': 'a'}), {
            'role': 'user', 'content': [{'type': 'text', 'text': 'b'}, {'type': 'text', 'text': ''}]}])

    def test_block_types_are_known_and_belong_to_their_role(self):
        conv = self.ok([USER])[1]
        self.v.record(conv, THINKING, 'a', None)
        self.v.record(conv, REDACTED, 'z', None)
        self.ok([USER, assistant(THINKING, {'type': 'text', 'text': 'a'}), USER])
        self.bad('unknown block type', [{'role': 'user', 'content': [{'type': 'video'}]}])
        self.bad('not allowed in a user message', [{'role': 'user', 'content': [THINKING]}])
        self.bad('not allowed in an assistant', [USER, assistant({'type': 'tool_result', 'tool_use_id': 'x'}), USER])
        self.bad('thinking block needs', [USER, assistant({'type': 'thinking', 'thinking': 'x'}), USER])
        self.bad('redacted_thinking block needs', [USER, assistant({'type': 'redacted_thinking'}), USER])

    def test_every_tool_use_is_answered_by_exactly_one_tool_result(self):
        self.ok([USER, assistant(CALL), results('toolu_1')])
        self.bad('no tool_result', [USER, assistant(CALL), USER])
        self.bad('toolu_2', [USER, assistant(CALL, dict(CALL, id='toolu_2')), results('toolu_1')])
        self.bad('no tool_use', [USER, assistant(CALL), results('toolu_1', 'toolu_9')])
        self.bad('answered twice', [USER, assistant(CALL), results('toolu_1', 'toolu_1')])
        self.bad('no tool_use', [USER, assistant({'type': 'text', 'text': 'a'}), results('toolu_1')])
        self.bad('no tool_use', [results('toolu_1')])
        self.bad('used twice', [USER, assistant(CALL), results('toolu_1'), assistant(CALL), results('toolu_1')])
        self.ok([USER, assistant(CALL), results('toolu_1'), assistant(dict(CALL, id='toolu_2')), results('toolu_2')])

    def test_tool_results_come_before_text_in_their_message(self):
        note = {'type': 'text', 'text': 'Tool limit reached; answer now without tools.'}
        self.ok([USER, assistant(CALL), results('toolu_1', after=[note])])
        late = {'role': 'user', 'content': [note, {'type': 'tool_result', 'tool_use_id': 'toolu_1', 'content': 'x'}]}
        self.bad('before any other content', [USER, assistant(CALL), late])

    def test_thinking_blocks_come_back_byte_identical_and_in_place(self):
        v = self.v
        conv = self.ok([USER])[1]
        v.record(conv, THINKING, 'a', None)
        v.record(conv, REDACTED, None, 'toolu_1')
        self.ok([USER, assistant(REDACTED, CALL), results('toolu_1'),
                 assistant(THINKING, {'type': 'text', 'text': 'a'}), USER])
        self.bad('did not come back', [USER, assistant(dict(REDACTED, data='opaque'), CALL), results('toolu_1')])
        self.bad('did not come back', [USER, assistant(CALL), results('toolu_1')])             # dropped
        self.bad('did not come back', [USER, assistant(CALL, REDACTED), results('toolu_1')])   # moved
        self.bad('did not come back', [USER, assistant(REDACTED, REDACTED, CALL), results('toolu_1')])
        self.bad('did not come back', [USER, assistant(dict(THINKING, signature=SIG[:-1]),
                                                       {'type': 'text', 'text': 'a'}), USER])
        self.bad('did not come back', [USER, assistant({'type': 'text', 'text': 'a'}), USER])
        other = {'type': 'text', 'text': 'b'}
        self.bad('not a reply', [USER, assistant(dict(THINKING, thinking='other'), other), USER])

    def test_system_and_tools_changing_within_a_conversation_are_rejected(self):
        said = {'type': 'text', 'text': 'a'}

        def deeper():              # the messages of a request deeper into the conversation begun below
            return [USER, assistant(THINKING, said), USER]

        self.v.record(self.ok([USER], system='one', tools=self.tools)[1], THINKING, 'a', None)
        self.ok(deeper(), system='one', tools=self.tools)
        self.bad('system: changed within a conversation', deeper(), system='two', tools=self.tools)
        self.bad('tools: changed within a conversation', deeper(), system='one')
        self.ok(deeper(), system='one', tools=self.tools)      # a rejected request changed nothing
        self.ok([{'role': 'user', 'content': 'other'}], system='three')    # another conversation: its own

    def test_a_request_of_one_message_starts_the_conversation_anew(self):
        said = {'type': 'text', 'text': 'a'}
        self.v.record(self.ok([USER], system='one')[1], THINKING, 'a', None)
        log = io.StringIO()
        with contextlib.redirect_stdout(log):
            self.ok([USER], system='two')                    # a new conversation that starts the same way
            self.assertEqual(log.getvalue(), '')
        self.bad('not a reply', [USER, assistant(THINKING, said), USER])    # the old replies are forgotten

    def test_a_conversation_the_mock_did_not_see_begin_is_not_held_to_the_thinking_rule(self):
        # the mock was restarted mid-conversation: its first sight of it is a request deeper in
        self.ok([USER, assistant(THINKING, {'type': 'text', 'text': 'old'}), USER])
        self.ok([USER, assistant({'type': 'text', 'text': 'old'}), USER])

    def test_an_assistant_message_must_be_a_reply_the_mock_sent_with_its_thinking_block(self):
        conv = self.ok([USER])[1]
        self.v.record(conv, THINKING, 'a', None)
        said = {'type': 'text', 'text': 'a'}
        self.ok([USER, assistant(THINKING, said), USER])
        self.bad('did not come back', [USER, assistant(said), USER])                    # thinking dropped
        self.bad('not a reply', [USER, assistant(THINKING, {'type': 'text', 'text': 'b'}), USER])
        self.bad('not a reply', [USER, assistant({'type': 'text', 'text': 'b'}), USER])  # altered, no thinking
        self.bad('did not come back', [USER, {'role': 'assistant', 'content': 'a'}, USER])   # a plain string

    def test_unknown_top_level_fields_are_rejected(self):
        self.ok(output_config={'effort': 'low'}, cache_control={'type': 'ephemeral'}, thinking={'type': 'adaptive'})
        self.bad('Extra inputs are not permitted', fallbacks=['other-model'])

    def with_betas(self, betas, messages=None, **fields):
        """check() a request carrying the anthropic-beta header `betas`."""
        headers, body = self.request(messages, **fields)
        return self.v.check(dict(headers, **{'anthropic-beta': betas}), body)

    def test_the_thinking_object_and_its_beta_headers(self):
        both = f'{mockapi.BETA_PROGRESS}, {mockapi.BETA_BINDING}'
        updates = {'type': 'adaptive', 'display': 'updates'}
        binding = {'type': 'adaptive', 'block_binding': {'prefix_mismatch_behavior': 'drop_block'}}
        self.ok(thinking={'type': 'adaptive'})
        self.with_betas(mockapi.BETA_PROGRESS, thinking=updates)
        self.with_betas(mockapi.BETA_BINDING, thinking=binding)
        self.with_betas('other-beta,' + both, thinking=dict(updates, **binding))
        self.bad('needs the anthropic-beta header thinking-display-updates', thinking=updates)
        self.bad('needs the anthropic-beta header thinking-binding-controls', thinking=binding)
        with self.assertRaises(mockapi.BadRequest) as cm:
            self.with_betas(mockapi.BETA_PROGRESS, thinking=dict(updates, **binding))
        self.assertIn('thinking.block_binding', str(cm.exception))
        for wrong, fragment in (({'type': 'enabled'}, 'thinking.type'),
                                (dict(updates, display='full'), 'thinking.display'),
                                (dict(binding, block_binding={'prefix_mismatch_behavior': 'error'}),
                                 'thinking.block_binding'),
                                (dict(binding, budget_tokens=5), 'Extra inputs'),
                                ('adaptive', 'must be an object')):
            with self.assertRaises(mockapi.BadRequest) as cm:
                self.with_betas(both, thinking=wrong)
            self.assertIn(fragment, str(cm.exception))

    def test_the_cache_markers_agree_on_their_ttl(self):
        hour = {'type': 'ephemeral', 'ttl': '1h'}
        five = {'type': 'ephemeral'}

        def system(marker):
            return [{'type': 'text', 'text': 'be brief', 'cache_control': marker}]

        self.ok(cache_control=five, system=system(five))
        self.ok(cache_control=hour, system=system(hour))
        self.ok(cache_control={'type': 'ephemeral', 'ttl': '5m'}, system=system(five))   # 5m is the default
        self.ok(cache_control=hour)
        self.ok(system=system(hour))
        self.bad('system.0.cache_control: ttl 5m differs from the 1h', cache_control=hour, system=system(five))
        self.bad('ttl 1h differs from the 5m', cache_control=five, system=system(hour))
        self.bad('cache_control.ttl: must be one of', cache_control={'type': 'ephemeral', 'ttl': '2h'})
        self.bad('must be {"type":"ephemeral"}', cache_control={'type': 'persistent'})

    def test_thinking_and_the_cache_marker_stay_put_within_a_conversation(self):
        said = {'type': 'text', 'text': 'a'}
        binding = {'type': 'adaptive', 'block_binding': {'prefix_mismatch_behavior': 'drop_block'}}

        def deeper():
            return [USER, assistant(THINKING, said), USER]

        conv = self.with_betas(mockapi.BETA_BINDING, [USER], thinking=binding, cache_control={'type': 'ephemeral'})[1]
        self.v.record(conv, THINKING, 'a', None)
        self.with_betas(mockapi.BETA_BINDING, deeper(), thinking=binding, cache_control={'type': 'ephemeral'})
        self.bad('thinking: changed within a conversation', deeper(), cache_control={'type': 'ephemeral'})
        with self.assertRaises(mockapi.BadRequest) as cm:
            self.with_betas(mockapi.BETA_BINDING, deeper(), thinking=binding,
                            cache_control={'type': 'ephemeral', 'ttl': '1h'})
        self.assertIn('cache_control: changed within a conversation', str(cm.exception))

    def test_the_tools_file_must_be_readable(self):
        with self.assertRaises(OSError):
            mockapi.Validator('/nonesuch/TOOLS.JSN')



THREADS = {'anthropic-beta': mockapi.BETA_THREADS}
CREATE = {'type': 'create'}


def message_id(events):
    return events[0][1]['message']['id']


class ThreadTest(unittest.TestCase):
    """Message threads: what the mock stores, what a continue must look like and the error_code of each
    refusal; a refusal a correct client never meets is also a VALIDATION: line."""

    @classmethod
    def setUpClass(cls):
        cls.server = mockapi.make_server(0, speed=0)
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self):
        self.log = io.StringIO()
        self.threads = self.server.validator.threads
        self.threads.ttl = mockapi.THREAD_TTL

    def post(self, messages, thread, extra=None, headers=None, system=True):
        """POST a request as HARNESS.EXE sends it in a thread (`thread` None: without one); `system`
        adds system and tools. Returns (status, events or the error body); a dropped link is (None, None)."""
        fields = {'model': 'mock', 'max_tokens': 16000, 'stream': True, 'messages': messages,
                  'tool_choice': {'type': 'auto', 'disable_parallel_tool_use': True}}
        if thread is not None:
            fields['thread'] = thread
        if system:
            fields['system'] = 'be brief'
            fields['tools'] = self.server.validator.tools
        fields.update(extra or {})
        sent = {'Content-Type': 'application/json', 'anthropic-version': '2023-06-01'}
        sent.update(THREADS if headers is None else headers)
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=10)
        try:
            with contextlib.redirect_stdout(self.log):
                conn.request('POST', '/v1/messages', body=json.dumps(fields).encode(), headers=sent)
                r = conn.getresponse()
                try:
                    data = r.read()
                except http.client.IncompleteRead:
                    return None, None
        finally:
            conn.close()
        return r.status, sse_events(data) if r.status == 200 else json.loads(data)

    def create(self, text):
        """A thread's first request: (response id, the conversation so far with the reply, the reply)."""
        messages = [{'role': 'user', 'content': text}]
        status, events = self.post(messages, CREATE)
        self.assertEqual(status, 200, events)
        said, blocks, stop, _ = reply(events)
        return message_id(events), messages + [{'role': 'assistant', 'content': blocks}], (said, blocks, stop)

    def cont(self, prev, new, extra=None, headers=None, system=False):
        return self.post(new, {'type': 'continue', 'previous_message_id': prev}, extra, headers, system)

    def refused(self, got, status, code):
        self.assertEqual(got[0], status, got[1])
        self.assertEqual(got[1]['error']['details']['error_code'], code)

    def answer(self, conversation):
        """The tool_result message for the call that ends the conversation."""
        return [{'role': 'user', 'content': [tool_result(tool_block(conversation[-1]['content']), 'sysinfo says hi')]}]

    def test_a_continue_brings_only_the_new_messages(self):
        prev, conv, _ = self.create('!tool sysinfo')
        new = self.answer(conv)
        status, events = self.cont(prev, new)
        self.assertEqual(status, 200, events)
        said, _, stop, _ = reply(events)
        self.assertIn('sysinfo -> ok: sysinfo says hi', said)
        self.assertEqual(stop, 'end_turn')
        self.assertIn('thread continue (1 sent)', self.log.getvalue())
        self.assertIn('3 messages, last role user', self.log.getvalue())
        self.assertRegex(self.log.getvalue(), r'thread create, \d+ bytes')
        # the next turn continues from the reply just sent
        status, _ = self.cont(message_id(events), [{'role': 'user', 'content': 'and then?'}])
        self.assertEqual(status, 200)
        self.assertNotIn('VALIDATION', self.log.getvalue())

    def test_a_continue_may_change_max_tokens_but_not_a_fixed_field(self):
        changes = (('model', 'other'), ('tool_choice', {'type': 'none'}), ('thinking', {'type': 'adaptive'}),
                   ('output_config', {'effort': 'high'}), ('cache_control', {'type': 'ephemeral'}))
        for field, value in changes:
            prev, conv, _ = self.create('!tool sysinfo')
            self.log = io.StringIO()
            self.refused(self.cont(prev, self.answer(conv), {field: value}), 400, 'thread_fingerprint_mismatch')
            self.assertIn(f'VALIDATION: thread_fingerprint_mismatch: {field} changed', self.log.getvalue())
        prev, conv, _ = self.create('!tool sysinfo')
        betas = {'anthropic-beta': mockapi.BETA_THREADS + ',' + mockapi.BETA_PROGRESS}
        self.refused(self.cont(prev, self.answer(conv), headers=betas), 400, 'thread_fingerprint_mismatch')
        prev, conv, _ = self.create('!tool sysinfo')
        self.assertEqual(self.cont(prev, self.answer(conv), {'max_tokens': 500})[0], 200)

    def test_system_and_tools_must_be_absent_on_a_continue(self):
        prev, conv, _ = self.create('!tool sysinfo')
        for extra in ({'system': 'be brief'}, {'tools': self.server.validator.tools}, {'system': None}):
            self.log = io.StringIO()
            status, body = self.cont(prev, self.answer(conv), extra)
            self.assertEqual(status, 400)
            self.assertIn('must be absent on a continue request', body['error']['message'])
            self.assertIn('VALIDATION:', self.log.getvalue())
        self.assertEqual(self.cont(prev, self.answer(conv))[0], 200)    # a rejected request used nothing up

    def test_a_thread_needs_its_beta_and_a_well_formed_thread_object(self):
        status, body = self.post([{'role': 'user', 'content': 'hi'}], CREATE, headers={})
        self.assertEqual(status, 400)
        self.assertIn(mockapi.BETA_THREADS, body['error']['message'])
        for thread in ({'type': 'fork'}, {'type': 'create', 'previous_message_id': 'x'}, {'type': 'continue'},
                       'create'):
            self.assertEqual(self.post([{'role': 'user', 'content': 'hi'}], thread)[0], 400, thread)
        self.assertEqual(self.post([{'role': 'user', 'content': 'hi'}], None, headers={})[0], 200)

    def test_an_unknown_or_expired_thread_is_not_found(self):
        self.refused(self.cont('msg_nonesuch', [{'role': 'user', 'content': 'hi'}]), 404, 'thread_not_found')
        prev, conv, _ = self.create('!tool sysinfo')
        self.threads.expire_all()
        self.refused(self.cont(prev, self.answer(conv)), 404, 'thread_not_found')
        self.threads.ttl = 0.0
        prev, conv, _ = self.create('!tool sysinfo')
        time.sleep(0.01)
        self.refused(self.cont(prev, self.answer(conv)), 404, 'thread_not_found')
        self.assertNotIn('VALIDATION', self.log.getvalue())

    def test_a_response_is_continued_once(self):
        prev, conv, _ = self.create('!tool sysinfo')
        self.assertEqual(self.cont(prev, self.answer(conv))[0], 200)
        self.refused(self.cont(prev, self.answer(conv)), 400, 'thread_already_continued')
        self.assertIn('VALIDATION: thread_already_continued: a response continued twice', self.log.getvalue())

    def test_a_continue_in_flight(self):
        prev, conv, _ = self.create('!tool sysinfo')
        self.threads.stored[prev].state = 'inflight'
        self.refused(self.cont(prev, self.answer(conv)), 400, 'thread_continue_in_flight')
        self.threads.stored[prev].state = 'ready'

    def test_refusals_and_cut_replies_are_not_stored(self):
        for text in ('!refuse', '!cutoff'):
            self.log = io.StringIO()
            prev, _, (_, _, stop) = self.create(text)
            self.assertIn(stop, ('refusal', 'max_tokens'))
            self.refused(self.cont(prev, [{'role': 'user', 'content': 'go on'}]), 404, 'thread_not_found')
            self.assertIn(f'VALIDATION: thread_not_found: a continue of {prev}, a {stop} reply', self.log.getvalue())

    def test_a_continue_never_resends_a_message_the_thread_has(self):
        prev, conv, _ = self.create('!tool sysinfo')
        status, body = self.cont(prev, conv[:1] + [conv[1]] + self.answer(conv))
        self.assertEqual(status, 400)
        self.assertIn('already in the thread', body['error']['message'])
        status, body = self.cont(prev, conv + self.answer(conv))
        self.assertEqual(status, 400)
        self.assertIn('VALIDATION:', self.log.getvalue())

    def test_an_error_before_the_model_runs_leaves_the_response_continuable(self):
        prev, conv, _ = self.create('!tool sysinfo')
        use = mockapi.ThreadUse('continue', self.threads.stored[prev])
        self.threads.stored[prev].state = 'inflight'
        self.threads.broken(use)
        self.assertEqual(self.threads.stored[prev].state, 'ready')
        self.assertEqual(self.cont(prev, self.answer(conv))[0], 200)

    def test_a_dropped_continue_is_resent(self):
        prev, conv, _ = self.create('!threaddrop')
        self.assertEqual(self.cont(prev, self.answer(conv)), (None, None))        # the link drops
        status, events = self.cont(prev, self.answer(conv))
        self.assertEqual(status, 200)
        self.assertIn('The reply after the dropped continue: sysinfo -> ok', reply(events)[0])
        self.assertIn('thread resend: PASS', self.log.getvalue())
        self.assertNotIn('VALIDATION', self.log.getvalue())

    def test_a_dropped_continue_that_used_its_response_up_is_replayed(self):
        prev, conv, _ = self.create('!threaddrop burn')
        self.assertEqual(self.cont(prev, self.answer(conv)), (None, None))
        self.refused(self.cont(prev, self.answer(conv)), 400, 'thread_already_continued')
        status, events = self.post(conv + self.answer(conv), CREATE)
        self.assertEqual(status, 200, events)
        self.assertIn('The reply after the dropped continue', reply(events)[0])
        self.assertIn('thread replay after thread_already_continued: PASS (create of 3 messages)', self.log.getvalue())
        self.assertNotIn('VALIDATION', self.log.getvalue())

    def test_a_lost_thread_is_replayed_with_the_whole_conversation(self):
        prev, conv, _ = self.create('!threadlost')
        self.refused(self.cont(prev, self.answer(conv)), 404, 'thread_not_found')
        status, events = self.post(conv + self.answer(conv), CREATE)
        self.assertEqual(status, 200)
        self.assertIn('thread replay after thread_not_found: PASS', self.log.getvalue())
        self.assertIn('threadlost sysinfo: the result came by thread create', self.log.getvalue())
        prev, conv, _ = self.create('!threadlost')
        self.cont(prev, self.answer(conv))
        self.cont(prev, self.answer(conv))           # a client that continues where it should replay
        self.assertIn('thread replay after thread_not_found: FAIL (continue of 1 messages)', self.log.getvalue())

    def test_threadfail_refuses_the_continue_and_its_replay(self):
        prev, conv, _ = self.create('!threadfail')
        self.refused(self.cont(prev, self.answer(conv)), 404, 'thread_not_found')
        self.refused(self.post(conv + self.answer(conv), CREATE), 404, 'thread_not_found')
        self.assertIn('thread replay after thread_not_found: PASS', self.log.getvalue())
        self.assertEqual(self.post([{'role': 'user', 'content': 'hi'}], CREATE)[0], 200)
        self.assertNotIn('FAIL', self.log.getvalue())

    def test_unsupported_threads_are_left_off_for_the_rest_of_the_conversation(self):
        prev, conv, _ = self.create('!nothreads')
        self.refused(self.cont(prev, self.answer(conv)), 400, 'thread_unsupported_request')
        conv += self.answer(conv)
        status, events = self.post(conv, None)
        self.assertEqual(status, 200)
        self.assertIn('thread replay after thread_unsupported_request: PASS (no thread', self.log.getvalue())
        conv.append({'role': 'assistant', 'content': reply(events)[1]})
        self.refused(self.post(conv + self.answer(conv), CREATE), 400, 'thread_unsupported_request')
        self.assertIn('VALIDATION: thread_unsupported_request: a thread request after', self.log.getvalue())
        status, events = self.post(conv + self.answer(conv), None)
        self.assertEqual(status, 200)
        self.assertIn('thread unsupported: PASS', self.log.getvalue())
        self.assertEqual(self.post([{'role': 'user', 'content': 'hi'}], CREATE)[0], 200)   # switched back on

    def test_the_tool_tour_continues_with_small_requests(self):
        messages = [{'role': 'user', 'content': '!tooltour'}]
        status, events = self.post(messages, CREATE)
        for _ in range(40):
            self.assertEqual(status, 200, events)
            _, blocks, stop, _ = reply(events)
            if stop != 'tool_use':
                break
            new = [{'role': 'user', 'content': [tool_result(tool_block(blocks), 'ok')]}]
            status, events = self.cont(message_id(events), new)
        self.assertRegex(self.log.getvalue(), r'tool tour threads: PASS \(a create of \d+ bytes, 25 continues, the largest \d+ bytes, 0 new')
        self.assertNotIn('VALIDATION', self.log.getvalue())

    def test_note_opens_the_reply_after_its_call_with_the_note(self):
        betas = {'anthropic-beta': mockapi.BETA_THREADS + ',' + mockapi.BETA_PROGRESS}
        updates = {'thinking': {'type': 'adaptive', 'display': 'updates'}}
        messages = [{'role': 'user', 'content': '!note'}]
        status, events = self.post(messages, CREATE, updates, betas)
        said, blocks, stop, _ = reply(events)
        self.assertEqual((said, stop), ('Text before the note.', 'tool_use'))
        status, events = self.cont(message_id(events), [{'role': 'user', 'content': [
            tool_result(tool_block(blocks))]}], updates, betas)
        said, blocks, stop, _ = reply(events)
        self.assertEqual((blocks[0]['thinking'], said, stop), (mockchat.NOTE_TEXT, 'Text after the note.', 'end_turn'))


if __name__ == '__main__':
    unittest.main()
