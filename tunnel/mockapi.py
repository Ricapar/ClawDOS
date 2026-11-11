"""A fake Anthropic Messages API for testing HARNESS.EXE offline.

  python3 tunnel/mockapi.py [--port 18023] [--speed 1.0] [--tools image/clawdos/TOOLS.JSN]

Serves POST /v1/messages over plain HTTP (no tunnel needed) with the real
API's streaming shape: chunked transfer encoding carrying server-sent events
(message_start, content_block_start/delta/stop, message_delta, message_stop,
error). Replies come from mockchat.py's scenarios: ordinary text gets a canned
answer, `!help` lists the test commands. Every reply starts with a thinking
block carrying a long signature, as Claude's do, so the DOS PC always exercises
that path.

The API is stateless, so the conversation arrives whole in every request. A
tool call ends one response (stop_reason tool_use) and the scenario resumes
when the next request carries the tool_result. Stdlib only.
"""

import argparse
import base64
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mockchat import (CHUNKS_TEXT, Disconnect, MockAPIError, MockChat, Refused, RetryOnce,  # noqa: E402
                      ToolCall, ToolResult)

__all__ = ['CHUNKS_TEXT', 'make_server', 'main']

MODEL = 'mock'
SIGNATURE = base64.b64encode(bytes(range(256)) * 5).decode()   # 1708 bytes, like a real one
DEFAULT_TOOLS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                             'image', 'clawdos', 'TOOLS.JSN')


class BadRequest(Exception):
    """The request body is not something the API would accept."""


def text_of(content):
    """The text of a message's content: a string, or the text blocks of a list."""
    if isinstance(content, str):
        return content
    return ''.join(b.get('text', '') for b in content if isinstance(b, dict) and b.get('type') == 'text')


def tool_results(content):
    """The tool_result blocks of a user message's content, or []."""
    if not isinstance(content, list):
        return []
    return [b for b in content if isinstance(b, dict) and b.get('type') == 'tool_result']


def validate(messages):
    """Raise BadRequest unless messages is a well-formed, alternating conversation."""
    if not isinstance(messages, list) or not messages:
        raise BadRequest('messages must be a non-empty array')
    last = None
    for i, m in enumerate(messages):
        role = m.get('role') if isinstance(m, dict) else None
        if role not in ('user', 'assistant'):
            raise BadRequest(f'messages.{i}.role must be user or assistant')
        if role == last:
            raise BadRequest(f'messages.{i}: roles must alternate')
        if 'content' not in m:
            raise BadRequest(f'messages.{i}.content is required')
        last = role
    if last != 'user':
        raise BadRequest('the last message must be from the user')


class Pending:
    """A scenario generator parked at a tool call, waiting for its result."""

    def __init__(self, chat, gen, call):
        self.chat, self.gen, self.call = chat, gen, call


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):   # quieter default logging, to stdout
        print(f'{self.address_string()} {fmt % args}', flush=True)

    # --- plumbing -----------------------------------------------------------------

    def send_json(self, status, obj, extra=()):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, status, kind, message, extra=()):
        self.send_json(status, {'type': 'error', 'error': {'type': kind, 'message': message}}, extra)

    def start_stream(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream; charset=utf-8')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Transfer-Encoding', 'chunked')
        self.end_headers()
        self.chunk_every = 0
        self.held = b''

    def chunk(self, data):
        """Write data as HTTP chunks: one chunk, or pieces of chunk_every bytes."""
        if self.chunk_every:
            self.held += data
            while len(self.held) >= self.chunk_every:
                piece, self.held = self.held[:self.chunk_every], self.held[self.chunk_every:]
                self.wfile.write(f'{len(piece):x}\r\n'.encode() + piece + b'\r\n')
        else:
            self.wfile.write(f'{len(data):x}\r\n'.encode() + data + b'\r\n')
        self.wfile.flush()

    def end_stream(self):
        if self.held:
            self.wfile.write(f'{len(self.held):x}\r\n'.encode() + self.held + b'\r\n')
            self.held = b''
        self.wfile.write(b'0\r\n\r\n')
        self.wfile.flush()

    def event(self, kind, obj):
        obj = dict(obj, type=kind)
        self.chunk(f'event: {kind}\ndata: {json.dumps(obj, ensure_ascii=False)}\n\n'.encode('utf-8'))

    # --- the API ------------------------------------------------------------------

    def do_POST(self):
        if self.path.split('?')[0] != '/v1/messages':
            self.send_error_json(404, 'not_found_error', f'Not found: {self.path}')
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            body = self.rfile.read(length)
            try:
                req = json.loads(body.decode('utf-8'))
            except (ValueError, UnicodeDecodeError) as e:
                raise BadRequest(f'invalid JSON: {e}') from e
            if not isinstance(req, dict):
                raise BadRequest('the body must be an object')
            messages = req.get('messages')
            validate(messages)
        except BadRequest as e:
            self.send_error_json(400, 'invalid_request_error', str(e))
            return
        print(f'request: {len(messages)} messages, last role {messages[-1]["role"]}', flush=True)
        self.answer(messages)

    def answer(self, messages):
        server = self.server
        last = messages[-1]
        results = tool_results(last['content'])
        pending = server.pending
        if results and pending is not None:
            r = results[0]
            content = r.get('content', '')
            if isinstance(content, list):
                content = text_of(content)
            status = 'error' if r.get('is_error') else 'ok'
            for s, marker in (('denied', 'The user denied this tool call.'), ('stopped', 'Interrupted by the user.')):
                if content == marker:
                    status = s
            if status == 'error' and content.startswith('Error: '):
                content = content[7:]
            pending.call.result = ToolResult(status, content)
            want = getattr(pending.chat, 'signature', None)
            if want and len(messages) >= 2:     # !bigsig: did the long signature come back whole?
                sigs = [b.get('signature') for b in messages[-2].get('content', [])
                        if isinstance(b, dict) and b.get('type') == 'thinking']
                print(f'big signature round trip: {"PASS" if want in sigs else "FAIL"} '
                      f'({len(want)} bytes sent, {[len(x or "") for x in sigs]} back)', flush=True)
            print(f'tool {pending.call.name} -> {status}: {content[:100]!r}', flush=True)
            chat, gen = pending.chat, pending.gen
            server.pending = None
        else:
            if results:
                print('tool result with nothing pending; treating it as a new message', flush=True)
            chat = MockChat(server.speed, state=server.state)
            chat.history = [(text_of(messages[i]['content']), text_of(messages[i + 1]['content']))
                            for i in range(0, len(messages) - 1, 2)]
            text = text_of(last['content'])
            if results and not text:
                text = '(tool result)'
            command = text.split()[0].lower() if text.startswith('!') else None
            if command == '!tooltour':
                self.check_tools()
            gen = chat.respond(command, text)
        self.stream_reply(chat, gen)

    def check_tools(self):
        """Warn when the tour names a tool TOOLS.JSN does not define."""
        try:
            with open(self.server.tools_path, encoding='utf-8') as f:
                names = {t['name'] for t in json.load(f)}
        except (OSError, ValueError, KeyError) as e:
            print(f'tools file {self.server.tools_path}: {e}', flush=True)
            return
        from mockchat import tour_steps
        missing = sorted({name for name, _, _, _ in tour_steps()} - names)
        if missing:
            print(f'tool tour: tools missing from TOOLS.JSN: {", ".join(missing)}', flush=True)

    def stream_reply(self, chat, gen):
        """Run the scenario generator into one streamed message."""
        server = self.server
        try:
            first = next(gen)
        except StopIteration:
            first = ''
        except RetryOnce:
            self.send_error_json(529, 'overloaded_error', 'Overloaded (simulated; try again)',
                                 [('retry-after', '1')])
            return
        except Refused:
            self.start_stream()
            self.message_start()
            self.event('message_delta', {'delta': {'stop_reason': 'refusal', 'stop_sequence': None},
                                         'usage': {'output_tokens': 0}})
            self.event('message_stop', {})
            self.end_stream()
            return
        except (MockAPIError, Disconnect) as e:
            first = e                 # handled below, after message_start

        self.start_stream()
        self.chunk_every = chat.chunk_every
        self.message_start()
        self.thinking_block(getattr(chat, 'signature', None) or SIGNATURE)
        index = 1
        text_open = False
        out_tokens = 0
        stop = 'end_turn'
        item = first
        try:
            while True:
                if isinstance(item, Exception):
                    raise item
                if isinstance(item, ToolCall):
                    if text_open:
                        self.event('content_block_stop', {'index': index})
                        index += 1
                        text_open = False
                    item.id = f'toolu_mock_{server.next_id()}'
                    self.tool_use_block(index, item)
                    index += 1
                    server.pending = Pending(chat, gen, item)
                    stop = 'tool_use'
                    break
                if item:
                    if not text_open:
                        self.event('content_block_start', {'index': index, 'content_block': {'type': 'text', 'text': ''}})
                        text_open = True
                    self.event('content_block_delta', {'index': index, 'delta': {'type': 'text_delta', 'text': item}})
                    out_tokens += max(1, len(item) // 4)
                    if item.endswith('[reply cut off: too long]'):
                        stop = 'max_tokens'
                try:
                    item = next(gen)
                except StopIteration:
                    break
        except Disconnect:
            self.wfile.flush()
            self.close_connection = True
            self.connection.close()
            return
        except MockAPIError as e:
            self.event('error', {'error': {'type': e.kind, 'message': str(e)}})
            self.end_stream()
            return
        if text_open:
            self.event('content_block_stop', {'index': index})
        self.event('message_delta', {'delta': {'stop_reason': stop, 'stop_sequence': None},
                                     'usage': {'output_tokens': out_tokens}})
        self.event('message_stop', {})
        self.end_stream()

    def message_start(self):
        self.event('message_start', {'message': {
            'id': f'msg_mock_{self.server.next_id()}', 'type': 'message', 'role': 'assistant', 'model': MODEL,
            'content': [], 'stop_reason': None, 'stop_sequence': None,
            'usage': {'input_tokens': 1200, 'cache_creation_input_tokens': 0,
                      'cache_read_input_tokens': 1000, 'output_tokens': 1}}})

    def thinking_block(self, signature=SIGNATURE):
        self.event('content_block_start', {'index': 0, 'content_block': {'type': 'thinking', 'thinking': '', 'signature': ''}})
        self.event('content_block_delta', {'index': 0, 'delta': {'type': 'signature_delta', 'signature': signature}})
        self.event('content_block_stop', {'index': 0})

    def tool_use_block(self, index, call):
        self.event('content_block_start', {'index': index, 'content_block': {
            'type': 'tool_use', 'id': call.id, 'name': call.name, 'input': {}}})
        text = json.dumps(call.args)
        cut = len(text) // 2
        for piece in (text[:cut], text[cut:]):
            self.event('content_block_delta', {'index': index, 'delta': {'type': 'input_json_delta', 'partial_json': piece}})
        self.event('content_block_stop', {'index': index})


class MockServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr, speed, tools_path):
        super().__init__(addr, Handler)
        self.speed = speed
        self.tools_path = tools_path
        self.state = {}             # what already happened (!drop, !retry)
        self.pending = None         # Pending tool call
        self._ids = 0
        self._lock = threading.Lock()

    def next_id(self):
        with self._lock:
            self._ids += 1
            return self._ids


def make_server(port, speed=1.0, tools_path=DEFAULT_TOOLS, host='0.0.0.0'):
    return MockServer((host, port), speed, tools_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--port', type=int, default=18023)
    ap.add_argument('--speed', type=float, default=1.0, help='scale the delays (0: none)')
    ap.add_argument('--tools', default=DEFAULT_TOOLS, help='TOOLS.JSN, to check the tool tour against')
    a = ap.parse_args()
    server = make_server(a.port, a.speed, a.tools)
    print(f'mock Messages API on port {a.port} (speed {a.speed})', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
