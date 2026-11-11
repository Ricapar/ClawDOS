"""Tests for the mock Messages API server (python3 tunnel/test_mockapi.py)."""

import http.client
import json
import os
import sys
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mockapi  # noqa: E402
import mockchat  # noqa: E402


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
    """(text, blocks, stop_reason, tool input text) reassembled from events."""
    blocks, text, stop, tool_json = [], [], None, []
    for event, d in events:
        if event == 'content_block_start':
            blocks.append(dict(d['content_block']))
        elif event == 'content_block_delta':
            delta = d['delta']
            if delta['type'] == 'text_delta':
                text.append(delta['text'])
            elif delta['type'] == 'signature_delta':
                blocks[-1]['signature'] = blocks[-1].get('signature', '') + delta['signature']
            elif delta['type'] == 'input_json_delta':
                tool_json.append(delta['partial_json'])
        elif event == 'message_delta':
            stop = d['delta'].get('stop_reason')
    return ''.join(text), blocks, stop, ''.join(tool_json)


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

    def post(self, messages, raw=None):
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=10)
        body = raw if raw is not None else json.dumps({'model': 'mock', 'max_tokens': 16000, 'stream': True,
                                                        'messages': messages}).encode()
        conn.request('POST', '/v1/messages', body=body, headers={'Content-Type': 'application/json'})
        r = conn.getresponse()
        data = r.read()
        headers = dict((k.lower(), v) for k, v in r.getheaders())
        conn.close()
        return r.status, headers, data

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
        self.assertEqual(json.loads(tool_json), {'path': 'C:\\A.TXT'})
        self.assertIn('Calling read.', text)
        assistant = [{'type': 'text', 'text': text},
                     {'type': 'tool_use', 'id': tool['id'], 'name': 'read', 'input': json.loads(tool_json)}]
        result = [{'type': 'tool_result', 'tool_use_id': tool['id'], 'content': '    1 hi', 'is_error': False}]
        status, _, data = self.post([{'role': 'user', 'content': '!tool read path=C:\\A.TXT'},
                                     {'role': 'assistant', 'content': assistant},
                                     {'role': 'user', 'content': result}])
        self.assertEqual(status, 200)
        text2, _, stop2, _ = reply(sse_events(data))
        self.assertIn('read -> ok:     1 hi', text2)
        self.assertEqual(stop2, 'end_turn')

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

    def test_refusal_and_cutoff(self):
        self.assertEqual(reply(self.ask('!refuse'))[2], 'refusal')
        self.assertEqual(reply(self.ask('!cutoff'))[2], 'max_tokens')

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

    def test_history_is_read_from_the_request(self):
        status, _, data = self.post([{'role': 'user', 'content': 'first'},
                                     {'role': 'assistant', 'content': [{'type': 'text', 'text': 'ok'}]},
                                     {'role': 'user', 'content': '!history'}])
        self.assertEqual(status, 200)
        text, _, _, _ = reply(sse_events(data))
        self.assertIn('1 earlier message', text)
        self.assertIn('first', text)

    def test_bench_is_twenty_reads_a_full_memory_run_and_a_long_reply(self):
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
            tool = [b for b in blocks if b['type'] == 'tool_use'][0]
            args = json.loads(tool_json)
            calls.append((tool['name'], args))
            result = [{'type': 'tool_result', 'tool_use_id': tool['id'], 'content': 'x' * 4000, 'is_error': False}]
            messages += [{'role': 'assistant', 'content': [{'type': 'text', 'text': text},
                                                           {'type': 'tool_use', 'id': tool['id'],
                                                            'name': tool['name'], 'input': args}]},
                         {'role': 'user', 'content': result}]
        self.assertEqual(calls, [('read', {'path': mockchat.BENCH_FILE})] * 20
                         + [('run', {'command': 'VER', 'full_memory': True})])
        self.assertEqual(stop, 'end_turn')
        self.assertEqual(text, mockchat.BENCH_REPLY)
        self.assertTrue(3900 <= len(text) <= 4300)
        self.assertEqual(len({len(d) for d in deltas[:-1]}), 1)    # fixed-size pieces: no randomness
        self.assertGreater(len(json.dumps(messages)), 80 * 1024)


if __name__ == '__main__':
    unittest.main()
