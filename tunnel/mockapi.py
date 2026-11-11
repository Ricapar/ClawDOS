"""A fake Anthropic Messages API for testing HARNESS.EXE offline.

  python3 tunnel/mockapi.py [--port 18023] [--speed 1.0] [--tools image/clawdos/TOOLS.JSN] [--thread-ttl 300]

Serves POST /v1/messages over plain HTTP (no tunnel needed) with the real
API's streaming shape: chunked transfer encoding carrying server-sent events
(message_start, content_block_start/delta/stop, message_delta, message_stop,
error). Replies come from mockchat.py's scenarios: ordinary text gets a canned
answer, `!help` lists the test commands. Every reply starts with a thinking
block carrying a long signature, as Claude's do, so the DOS PC always exercises
that path.

Without message threads the conversation arrives whole in every request. With
them (the `thread` field and the message-threads beta, docs/API.md "Threads")
the mock stores each response by its id with the conversation up to it, and a
continue request brings only the messages since; Threads enforces the rules
and answers a refused one with the API's status and error_code. A tool call
ends one response (stop_reason tool_use) and the scenario resumes when the next
request carries the tool_result.

Every request is checked against the rules of Validator (headers, fields, the
thinking object and its beta headers, the cache markers, message structure, tool
pairing, thinking blocks; docs/API.md lists them): a
violation is answered with HTTP 400 and logged on a line starting `VALIDATION:`,
so the boot tests can reject it. Stdlib only.
"""

import argparse
import base64
import json
import os
import re
import select
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mockchat import (CHUNKS_TEXT, DEFAULT_TOOLS, Disconnect, Drop, HttpError, MockAPIError, MockChat,  # noqa: E402
                      Ping, Refused, ToolCall, ToolResult)

__all__ = ['CHUNKS_TEXT', 'make_server', 'main']

MODEL = 'mock'
SIGNATURE = base64.b64encode(bytes(range(256)) * 5).decode()   # 1708 bytes, like a real one
API_VERSION = '2023-06-01'
BODY_TIMEOUT = 10.0     # seconds of silence while the body arrives before the request is called short
# The top-level fields HARNESS.EXE sends (docs/API.md, "The request"); any other is rejected like the real
# API's "Extra inputs are not permitted". A task that adds a request field adds it here, with its rule.
REQUEST_FIELDS = ('model', 'max_tokens', 'stream', 'thinking', 'output_config', 'cache_control', 'tool_choice',
                  'thread', 'system', 'tools', 'messages')
# The anthropic-beta values the thinking object's beta fields need (docs/API.md, "The request").
BETA_PROGRESS = 'thinking-display-updates-2026-08-18'
BETA_BINDING = 'thinking-binding-controls-2026-08-01'
CACHE_TTLS = ('5m', '1h')
BETA_THREADS = 'message-threads-2026-08-12'
THREAD_TTL = 300.0      # seconds a stored response stays continuable after its request
# What a thread holds fixed (docs/API.md, "Threads"); the set of anthropic-beta values is the sixth.
THREAD_FIXED = ('model', 'tool_choice', 'thinking', 'output_config', 'cache_control')
# The thread errors: error_code -> (HTTP status, error.type).
THREAD_ERRORS = {'thread_not_found': (404, 'not_found_error'),
                 'thread_already_continued': (400, 'invalid_request_error'),
                 'thread_continue_in_flight': (400, 'invalid_request_error'),
                 'thread_unsupported_request': (400, 'invalid_request_error'),
                 'thread_fingerprint_mismatch': (400, 'invalid_request_error')}
CONTINUABLE = ('end_turn', 'tool_use')     # the stop reasons the harness continues after


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


THINKING_TYPES = ('thinking', 'redacted_thinking')
BLOCK_TYPES = {'user': ('text', 'image', 'tool_result'),
               'assistant': ('text', 'thinking', 'redacted_thinking', 'tool_use')}
KNOWN_BLOCK_TYPES = set(BLOCK_TYPES['user']) | set(BLOCK_TYPES['assistant'])


def is_text(value):
    """A string with something in it besides whitespace, which is what the API calls text."""
    return isinstance(value, str) and bool(value.strip())


def check_block(where, role, block):
    """Raise BadRequest unless one content block is well formed for a message from `role`."""
    if not isinstance(block, dict):
        raise BadRequest(f'{where}: a content block must be an object')
    kind = block.get('type')
    if kind not in KNOWN_BLOCK_TYPES:
        raise BadRequest(f'{where}.type: unknown block type {kind!r}')
    if kind not in BLOCK_TYPES[role]:
        article = 'an' if role == 'assistant' else 'a'
        raise BadRequest(f'{where}: a {kind} block is not allowed in {article} {role} message')
    if kind == 'text' and not is_text(block.get('text')):
        raise BadRequest(f'{where}.text: text content blocks must contain non-whitespace text')
    if kind == 'thinking' and not (isinstance(block.get('thinking'), str) and is_text(block.get('signature'))):
        raise BadRequest(f'{where}: a thinking block needs a thinking string and a signature')
    if kind == 'redacted_thinking' and not is_text(block.get('data')):
        raise BadRequest(f'{where}.data: a redacted_thinking block needs its data')
    if kind == 'tool_use' and not (is_text(block.get('id')) and is_text(block.get('name'))
                                   and isinstance(block.get('input'), dict)):
        raise BadRequest(f'{where}: a tool_use block needs an id, a name and an input object')
    if kind == 'tool_result':
        check_tool_result(where, block)


def check_tool_result(where, block):
    """Raise BadRequest unless a tool_result block is well formed."""
    if not is_text(block.get('tool_use_id')):
        raise BadRequest(f'{where}.tool_use_id: required')
    if 'is_error' in block and not isinstance(block['is_error'], bool):
        raise BadRequest(f'{where}.is_error: must be a boolean')
    content = block.get('content', '')
    if isinstance(content, list):
        for k, inner in enumerate(content):
            if not isinstance(inner, dict) or inner.get('type') not in ('text', 'image'):
                raise BadRequest(f'{where}.content.{k}: must be a text or image block')
            if inner['type'] == 'text' and not is_text(inner.get('text')):
                raise BadRequest(f'{where}.content.{k}.text: text content blocks must contain non-whitespace text')
    elif not isinstance(content, str):
        raise BadRequest(f'{where}.content: must be a string or a list of blocks')


def check_message(i, m, expected_ids, seen_ids):
    """Check message i alone (its blocks, and its tool_use / tool_result ids against the message before).
    Returns the tool_use ids an assistant message asks to have answered next."""
    role, content = m['role'], m['content']
    where = f'messages.{i}'
    if isinstance(content, str):
        content = [{'type': 'text', 'text': content}]
        where += '.content'
        blocks = [(where, b) for b in content]
    elif isinstance(content, list) and content:
        blocks = [(f'{where}.content.{j}', b) for j, b in enumerate(content)]
    else:
        raise BadRequest(f'{where}.content: must be a non-empty string or a non-empty array of blocks')
    for path, b in blocks:
        check_block(path, role, b)
    if role == 'assistant':
        ids = [b['id'] for _, b in blocks if b['type'] == 'tool_use']
        for tool_id in ids:
            if tool_id in seen_ids:
                raise BadRequest(f'{where}: tool_use id {tool_id} is used twice')
            seen_ids.add(tool_id)
        return ids               # the next message must answer these
    waiting, text_seen = list(expected_ids), False
    for path, b in blocks:
        if b['type'] != 'tool_result':
            text_seen = True
        elif text_seen:
            raise BadRequest(f'{path}: tool_result blocks must come before any other content in the message')
        elif b['tool_use_id'] not in waiting:
            raise BadRequest(f'{path}: tool_use_id {b["tool_use_id"]} has no tool_use in the message before '
                             f'(or is answered twice)')
        else:
            waiting.remove(b['tool_use_id'])
    if waiting:
        raise BadRequest(f'{where}: tool_use ids {waiting} in the message before have no tool_result')
    return []


def check_messages(messages):
    """Raise BadRequest unless messages is a well-formed, alternating conversation."""
    if not isinstance(messages, list) or not messages:
        raise BadRequest('messages must be a non-empty array')
    expected, seen = [], set()
    for i, m in enumerate(messages):
        role = m.get('role') if isinstance(m, dict) else None
        if role not in ('user', 'assistant'):
            raise BadRequest(f'messages.{i}.role must be user or assistant')
        if role != ('user', 'assistant')[i % 2]:
            raise BadRequest(f'messages.{i}: roles must alternate, starting with user')
        if 'content' not in m:
            raise BadRequest(f'messages.{i}.content is required')
        expected = check_message(i, m, expected, seen)
    if messages[-1]['role'] != 'user':
        raise BadRequest('the last message must be from the user')


def check_tool_choice(choice, tools):
    """Raise BadRequest unless tool_choice has one of the shapes the API accepts."""
    if not isinstance(choice, dict):
        raise BadRequest('tool_choice must be an object')
    kind, extra = choice.get('type'), set(choice) - {'type'}
    allowed = {'auto': {'disable_parallel_tool_use'}, 'any': {'disable_parallel_tool_use'},
               'tool': {'name', 'disable_parallel_tool_use'}, 'none': set()}
    if kind not in allowed:
        raise BadRequest(f'tool_choice.type: must be auto, any, tool or none, not {kind!r}')
    if extra - allowed[kind]:
        raise BadRequest(f'tool_choice: unexpected fields {sorted(extra - allowed[kind])} for type {kind}')
    if 'disable_parallel_tool_use' in choice and not isinstance(choice['disable_parallel_tool_use'], bool):
        raise BadRequest('tool_choice.disable_parallel_tool_use: must be a boolean')
    if kind == 'tool':
        names = [t.get('name') for t in tools or [] if isinstance(t, dict)]
        if choice.get('name') not in names:
            raise BadRequest(f'tool_choice.name: {choice.get("name")!r} is not one of the tools')


def check_system(system):
    """Raise BadRequest unless system is a string or a list of text blocks."""
    if isinstance(system, str):
        return
    if not isinstance(system, list):
        raise BadRequest('system must be a string or an array of text blocks')
    for k, b in enumerate(system):
        if not isinstance(b, dict) or b.get('type') != 'text' or not is_text(b.get('text')):
            raise BadRequest(f'system.{k}: must be a text block with non-whitespace text')


def betas_of(headers):
    """The set of anthropic-beta values in a request's headers (names lowercased)."""
    return {v.strip() for v in headers.get('anthropic-beta', '').split(',') if v.strip()}


def check_thinking_config(thinking, betas):
    """Raise BadRequest unless the thinking object has a shape the API accepts with these beta values:
    display "updates" and block_binding each need their beta header, as on the real API."""
    if not isinstance(thinking, dict):
        raise BadRequest('thinking must be an object')
    if thinking.get('type') != 'adaptive':
        raise BadRequest(f'thinking.type: must be adaptive, not {thinking.get("type")!r}')
    extra = sorted(set(thinking) - {'type', 'display', 'block_binding'})
    if extra:
        raise BadRequest(f'thinking.{extra[0]}: Extra inputs are not permitted')
    if 'display' in thinking:
        if thinking['display'] != 'updates':
            raise BadRequest(f'thinking.display: must be updates, not {thinking["display"]!r}')
        if BETA_PROGRESS not in betas:
            raise BadRequest(f'thinking.display: "updates" needs the anthropic-beta header {BETA_PROGRESS}')
    if 'block_binding' in thinking:
        if BETA_BINDING not in betas:
            raise BadRequest(f'thinking.block_binding: needs the anthropic-beta header {BETA_BINDING}')
        binding = thinking['block_binding']
        if not isinstance(binding, dict) or set(binding) != {'prefix_mismatch_behavior'} \
                or binding['prefix_mismatch_behavior'] != 'drop_block':
            raise BadRequest('thinking.block_binding: must be {"prefix_mismatch_behavior":"drop_block"}')


def cache_ttl(where, marker):
    """The ttl of a cache_control marker ('5m' when it has none); BadRequest unless it is well formed."""
    if not isinstance(marker, dict) or marker.get('type') != 'ephemeral' or set(marker) - {'type', 'ttl'}:
        raise BadRequest(f'{where}: must be {{"type":"ephemeral"}}, with an optional ttl')
    ttl = marker.get('ttl', '5m')
    if ttl not in CACHE_TTLS:
        raise BadRequest(f'{where}.ttl: must be one of {", ".join(CACHE_TTLS)}, not {ttl!r}')
    return ttl


def check_cache_markers(req):
    """The top-level cache_control and the system blocks' markers are well formed and agree on their ttl."""
    ttls = []
    if 'cache_control' in req:
        ttls.append(('cache_control', cache_ttl('cache_control', req['cache_control'])))
    system = req.get('system')
    if isinstance(system, list):
        for k, b in enumerate(system):
            if isinstance(b, dict) and 'cache_control' in b:
                where = f'system.{k}.cache_control'
                ttls.append((where, cache_ttl(where, b['cache_control'])))
    for where, ttl in ttls[1:]:
        if ttl != ttls[0][1]:
            raise BadRequest(f'{where}: ttl {ttl} differs from the {ttls[0][1]} of {ttls[0][0]} '
                             f'(the markers of a request must carry the same ttl)')


def settings_of(req):
    """'thinking updates+drop_block, cache 1h': a request's thinking object and top-level cache marker, for the log."""
    thinking = req.get('thinking')
    if isinstance(thinking, dict):
        parts = [p for p, on in (('updates', thinking.get('display') == 'updates'),
                                 ('drop_block', 'block_binding' in thinking)) if on]
        kinds = '+'.join(parts) or str(thinking.get('type'))
    else:
        kinds = 'none'
    marker = req.get('cache_control')
    cache = marker.get('ttl', '5m') if isinstance(marker, dict) else 'off'
    return f'thinking {kinds}, cache {cache}'


def shape_of(messages):
    """'user[text] assistant[thinking,text,tool_use] ...': the block types of each message, for the log."""
    out = []
    for m in messages if isinstance(messages, list) else []:
        role = m.get('role') if isinstance(m, dict) else None
        content = m.get('content') if isinstance(m, dict) else None
        if isinstance(content, str):
            kinds = 'text'
        elif isinstance(content, list):
            kinds = ','.join(str(b.get('type')) if isinstance(b, dict) else '?' for b in content)
        else:
            kinds = '?'
        out.append(f'{role or "?"}[{kinds}]')
    return ' '.join(out)


def same_json(a, b):
    return json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)


class Validator:
    """Holds what the request check needs to remember between requests: for each
    conversation (keyed by its first message), what the mock sent in it (the replies, to
    check thinking blocks come back) and its system and tools (to check they stay put).
    A request of one message starts a conversation anew; one that arrives for a conversation
    this mock never saw start (it was restarted meanwhile) is not held to the thinking rule."""

    def __init__(self, tools_path):
        self.tools_path = tools_path
        with open(tools_path, encoding='utf-8') as f:       # unreadable: the mock must not start
            self.tools = json.load(f)
        self.conversations = {}
        self.lock = threading.Lock()
        self.threads = Threads()

    def conversation(self, messages):
        """The record of the conversation these messages belong to."""
        key = json.dumps(messages[0], sort_keys=True)
        if len(messages) == 1 or key not in self.conversations:
            self.conversations[key] = {'replies': [], 'seen': False, 'known': len(messages) == 1}
        return self.conversations[key]

    def check(self, headers, body):
        """Return (request, conversation, ThreadUse) or raise BadRequest or ThreadError. `headers` maps
        names to values. The request is the whole one: a continue's stored conversation and new messages."""
        headers = {k.lower(): v for k, v in headers.items()}
        if headers.get('anthropic-version') != API_VERSION:
            raise BadRequest(f'anthropic-version header: must be {API_VERSION}')
        if headers.get('content-type', '').split(';')[0].strip().lower() != 'application/json':
            raise BadRequest('content-type header: must be application/json')
        if headers.get('content-length') != str(len(body)):
            raise BadRequest(f'Content-Length {headers.get("content-length")!r} does not match '
                             f'the {len(body)} body bytes')
        try:
            req = json.loads(body.decode('utf-8'))
        except (ValueError, UnicodeDecodeError) as e:
            raise BadRequest(f'invalid JSON (a Content-Length shorter than the body cuts it): {e}') from e
        if not isinstance(req, dict):
            raise BadRequest('the body must be an object')
        req, use = self.threads.resolve(req, betas_of(headers))
        self.check_fields(req)
        if 'thinking' in req:
            check_thinking_config(req['thinking'], betas_of(headers))
        check_cache_markers(req)
        with self.lock:
            conv = self.conversation(req['messages'])
            self.check_thinking(conv, req['messages'])
            self.check_stable(conv, req)
        return req, conv, use

    def check_fields(self, req):
        """The request's top-level fields and its messages."""
        extra = sorted(set(req) - set(REQUEST_FIELDS))
        if extra:
            raise BadRequest(f'{extra[0]}: Extra inputs are not permitted')
        if not is_text(req.get('model')):
            raise BadRequest('model: required')
        if not isinstance(req.get('max_tokens'), int) or isinstance(req['max_tokens'], bool) or req['max_tokens'] < 1:
            raise BadRequest('max_tokens: required, a positive integer')
        if 'messages' not in req:
            raise BadRequest('messages: required')
        if req.get('stream') is not True:
            raise BadRequest('stream: must be true (the DOS PC reads server-sent events)')
        if 'tools' in req and not same_json(req['tools'], self.tools):
            raise BadRequest(f'tools: differ from {os.path.basename(self.tools_path)}')
        if 'tool_choice' in req:
            check_tool_choice(req['tool_choice'], req.get('tools'))
        if 'system' in req:
            check_system(req['system'])
        check_messages(req['messages'])

    def record(self, conv, thinking, text, tool_id):
        """Remember a complete reply the mock sent (its thinking block, text and tool call id)."""
        with self.lock:
            conv['replies'].append({'thinking': thinking, 'text': text, 'tool_id': tool_id})

    @staticmethod
    def sent_reply(replies, blocks):
        """The reply the mock sent that this assistant message repeats: by tool call id, else by text."""
        ids = [b['id'] for b in blocks if b['type'] == 'tool_use']
        text = text_of(blocks)
        for r in reversed(replies):
            if (ids and r['tool_id'] == ids[0]) or (not ids and r['tool_id'] is None and (r['text'] or '') == text):
                return r
        return None

    def check_thinking(self, conv, messages):
        """Every thinking block the mock sent comes back byte-identical and in place, and every assistant
        message is one the mock sent. Skipped for a conversation the mock did not see begin."""
        if not conv['known']:
            return
        replies = conv['replies']
        for i, m in enumerate(messages):
            if m['role'] != 'assistant':
                continue
            blocks = m['content'] if isinstance(m['content'], list) else [{'type': 'text', 'text': m['content']}]
            got = [(j, b) for j, b in enumerate(blocks) if b['type'] in THINKING_TYPES]
            reply = self.sent_reply(replies, blocks)
            if reply is None:
                raise BadRequest(f'messages.{i}: not a reply this mock sent in this conversation')
            if len(got) != 1 or got[0][0] != 0 or not same_json(got[0][1], reply['thinking']):
                raise BadRequest(f'messages.{i}.content: the {reply["thinking"]["type"]} block the mock sent '
                                 f'did not come back unchanged as the first block')

    STABLE = {'system': 'thinking blocks are bound to the system prompt and tools they were made under',
              'tools': 'thinking blocks are bound to the system prompt and tools they were made under',
              'thinking': 'the thinking configuration is chosen when the conversation starts',
              'cache_control': 'the cache markers are chosen when the conversation starts'}

    def check_stable(self, conv, req):
        """Within a conversation system and tools must not change: the API drops or rejects thinking
        blocks bound to the old ones, and the prompt cache misses from the first byte. The thinking
        configuration and the top-level cache marker are held to the same rule (a message thread
        refuses a request whose fixed fields differ)."""
        now = {field: req.get(field) for field in self.STABLE}
        if not conv['seen']:
            conv.update(now, seen=True)
            return
        for field, why in self.STABLE.items():
            if not same_json(now[field], conv[field]):
                raise BadRequest(f'{field}: changed within a conversation ({why})')


class ThreadError(Exception):
    """A thread request the API refuses before the model runs, answered with its status and error_code.
    `bug` says what the client did wrong when a correct one never meets this refusal (logged as
    VALIDATION:, so a boot test fails); None when it is part of normal use (an expired thread, a
    continue resent after a dropped link)."""

    def __init__(self, code, message, bug=None):
        super().__init__(message)
        self.code, self.bug = code, bug
        self.status, self.kind = THREAD_ERRORS[code]


class StoredThread:
    """A response the mock stored for continuing: the whole conversation up to and including it, the
    system and tools it was made under, the fixed fields and when its request came. `state` is ready,
    inflight (a continue of it is being answered) or continued; `dropped` says a continue of it lost its
    link, after which a resend may meet thread_already_continued."""

    def __init__(self, messages, use, when):
        self.messages, self.system, self.tools, self.fixed = messages, use.system, use.tools, use.fixed
        self.when, self.state, self.dropped = when, 'ready', False


class ThreadUse:
    """What one request does with threads: its kind (create, continue, or None without a thread), the
    stored response it continues, how many messages it sent, and what its reply will be stored with."""

    def __init__(self, kind=None, prev=None, sent=0, req=None, fixed=None):
        self.kind, self.prev, self.sent, self.fixed = kind, prev, sent, fixed
        self.system = req.get('system') if req else None
        self.tools = req.get('tools') if req else None
        self.when = time.time()

    def describe(self):
        if self.kind == 'continue':
            return f'thread continue ({self.sent} sent)'
        return 'thread create' if self.kind == 'create' else 'no thread'


def fixed_fields(req, betas):
    """The fields a thread holds fixed, as one comparable dict."""
    fields = {f: req.get(f) for f in THREAD_FIXED}
    fields['anthropic-beta'] = sorted(betas)
    return fields


class Threads:
    """The mock's message threads: each response stored by its id with the conversation up to it, for
    THREAD_TTL seconds after its request (`ttl`, settable). resolve() turns a continue request back into
    the whole request the model sees, so the Validator checks it as it checks any other, after the
    thread rules (constraints in docs/API.md, "Threads"). Tests force refusals through the switches:
    lose_next (forget every thread before the next continue), fail_next (refuse that many thread
    requests with thread_not_found), unsupported (refuse every thread request until one comes without);
    expire_all() ages every stored thread past its lifetime. After a refusal the client meets in normal
    use, the next request must replay the whole conversation: the log says `thread replay after CODE:
    PASS` or FAIL."""

    def __init__(self, ttl=THREAD_TTL):
        self.ttl = ttl
        self.stored = {}            # response id -> StoredThread
        self.unstored = {}          # response id -> why the API would not store it (refusal, max_tokens)
        self.lock = threading.Lock()
        self.lose_next = False
        self.fail_next = 0
        self.unsupported = None     # None, 'asked' (one was refused), 'replayed' (one came without a thread)
        self.replay = None          # (code, the whole conversation expected or None) after a refusal

    def expire_all(self):
        with self.lock:
            for rec in self.stored.values():
                rec.when -= self.ttl + 1

    def resolve(self, req, betas):
        """(the request as the model sees it, ThreadUse), or raise BadRequest or ThreadError."""
        messages = req.get('messages')
        if 'thread' in req and (not isinstance(messages, list) or not messages):
            raise BadRequest('messages must be a non-empty array')
        with self.lock:
            replay, self.replay = self.replay, None
            thread = req.get('thread')
            if thread is None:
                if self.unsupported == 'asked':
                    self.unsupported = 'replayed'
                if replay:
                    self.check_replay(replay, None, messages if isinstance(messages, list) else [])
                return req, ThreadUse()
            kind = self.check_shape(thread, betas)
            fixed = fixed_fields(req, betas)
            if replay:
                self.check_replay(replay, kind, messages)
            self.refuse_on_demand()
            if kind == 'create':
                return {k: v for k, v in req.items() if k != 'thread'}, ThreadUse('create', req=req, fixed=fixed)
            for field in ('system', 'tools'):
                if field in req:
                    raise BadRequest(f'{field}: must be absent on a continue request (the thread has it)')
            rec = self.previous(thread['previous_message_id'], req['messages'])
            self.check_continue(rec, fixed, req['messages'])
            rec.state = 'inflight'
            whole = {k: v for k, v in req.items() if k != 'thread'}
            whole['messages'] = rec.messages + req['messages']
            for field, value in (('system', rec.system), ('tools', rec.tools)):
                if value is not None:
                    whole[field] = value
            return whole, ThreadUse('continue', rec, len(req['messages']), whole, rec.fixed)

    @staticmethod
    def check_shape(thread, betas):
        """The thread object's kind; BadRequest unless it is a well-formed create or continue."""
        if BETA_THREADS not in betas:
            raise BadRequest(f'thread: needs the anthropic-beta header {BETA_THREADS}')
        if not isinstance(thread, dict) or thread.get('type') not in ('create', 'continue'):
            raise BadRequest('thread.type: must be create or continue')
        want = {'type'} if thread['type'] == 'create' else {'type', 'previous_message_id'}
        if set(thread) != want or (thread['type'] == 'continue' and not is_text(thread['previous_message_id'])):
            raise BadRequest(f'thread: a {thread["type"]} carries exactly {sorted(want)}')
        return thread['type']

    def refuse_on_demand(self):
        """The refusals a test asked for (unsupported, fail_next)."""
        if self.unsupported == 'replayed':
            raise ThreadError('thread_unsupported_request', 'Threads are not supported here (simulated)',
                              bug='a thread request after thread_unsupported_request')
        if self.unsupported:
            self.unsupported = 'asked'
            self.replay = ('thread_unsupported_request', None)
            raise ThreadError('thread_unsupported_request', 'Threads are not supported here (simulated)')
        if self.fail_next:
            self.fail_next -= 1
            if self.fail_next:      # the replay meets the second: the client ends the turn, nothing to check after
                self.replay = ('thread_not_found', None)
            raise ThreadError('thread_not_found', 'Thread not found (simulated by !threadfail)')

    def previous(self, prev_id, messages):
        """The stored response a continue names; ThreadError when there is none to continue."""
        if self.lose_next:
            self.lose_next = False
            rec = self.stored.get(prev_id)
            self.stored.clear()
            self.replay = ('thread_not_found', rec.messages + messages if rec else None)
            raise ThreadError('thread_not_found', 'Thread not found (simulated: the mock forgot it)')
        rec = self.stored.get(prev_id)
        if rec is not None and time.time() - rec.when > self.ttl:
            del self.stored[prev_id]
            rec = None
        if rec is None:
            why = self.unstored.get(prev_id)
            if why is None:
                self.replay = ('thread_not_found', None)
            raise ThreadError('thread_not_found', f'Thread {prev_id} not found (expired or never stored)',
                              bug=f'a continue of {prev_id}, a {why} reply (the API does not store those)'
                              if why else None)
        return rec

    def check_continue(self, rec, fixed, messages):
        """The rules a continue of a stored response must meet."""
        if rec.state == 'inflight':
            self.replay = ('thread_continue_in_flight', None)
            raise ThreadError('thread_continue_in_flight', 'A continue of this response is in flight')
        if rec.state == 'continued':
            self.replay = ('thread_already_continued', None) if rec.dropped else None
            raise ThreadError('thread_already_continued', 'This response has already been continued',
                              bug=None if rec.dropped else 'a response continued twice')
        for field, value in fixed.items():
            if not same_json(value, rec.fixed[field]):
                raise ThreadError('thread_fingerprint_mismatch', f'{field} differs from the thread\'s',
                                  bug=f'{field} changed within a thread: {rec.fixed[field]!r} -> {value!r}')
        for i, m in enumerate(messages):
            if m.get('role') == 'assistant' and any(same_json(m, old) for old in rec.messages):
                raise BadRequest(f'messages.{i}: already in the thread (a continue sends only what is new)')
        if rec.dropped:
            print('thread resend: PASS (the continue whose link dropped came again)', flush=True)

    @staticmethod
    def check_replay(replay, kind, messages):
        """The request after a refusal: the whole conversation as a create (without a thread after
        thread_unsupported_request), the same as the refused request would have had."""
        code, expected = replay
        want = None if code == 'thread_unsupported_request' else 'create'
        ok = kind == want and len(messages) > 1 and (expected is None or same_json(messages, expected))
        got = f'{kind or "no thread"} of {len(messages)} messages'
        print(f'thread replay after {code}: {"PASS" if ok else "FAIL"} ({got})', flush=True)

    def done(self, use, msg_id, stop, messages):
        """A reply was sent to its end (message_stop): the response continued is used up, and this one is
        stored, unless the API would not store it."""
        if use.kind is None:
            return
        with self.lock:
            if use.prev is not None:
                use.prev.state = 'continued'
            if stop in CONTINUABLE:
                self.stored[msg_id] = StoredThread(messages, use, use.when)
            else:
                self.unstored[msg_id] = stop

    def broken(self, use, burn=False, dropped=False):
        """A continue's reply never reached message_stop: an error before the model ran leaves the response
        continuable; a dropped link too, unless `burn` (then a resend meets thread_already_continued)."""
        with self.lock:
            if use.prev is None:
                return
            if dropped:
                use.prev.dropped = True
            if use.prev.state == 'inflight':
                use.prev.state = 'continued' if burn else 'ready'


class Pending:
    """A scenario generator parked at a tool call, waiting for its result."""

    def __init__(self, chat, gen, call):
        self.chat, self.gen, self.call = chat, gen, call


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    rbufsize = 0                # unbuffered: read_body can tell from the socket whether bytes are waiting
    binding = False             # this request carries the thinking-binding beta (message_start shows it)

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
        body = b''
        try:
            body = self.read_body()
            req, conv, use = self.server.validator.check(self.headers, body)
        except BadRequest as e:
            shape = shape_of(self.try_messages(body))
            print(f'VALIDATION: {e} [messages: {shape}]', flush=True)
            self.send_error_json(400, 'invalid_request_error', str(e))
            return
        except ThreadError as e:
            print(f'thread error: {e.status} {e.code}: {e} ({len(body)} bytes)', flush=True)
            if e.bug:
                print(f'VALIDATION: {e.code}: {e.bug}', flush=True)
            self.send_json(e.status, {'type': 'error', 'error': {'type': e.kind, 'message': str(e),
                                                                 'details': {'error_code': e.code}}})
            return
        messages = req['messages']
        print(f'request: {len(messages)} messages, last role {messages[-1]["role"]}, {settings_of(req)}, '
              f'{use.describe()}, {len(body)} bytes', flush=True)
        self.answer(req, conv, use, len(body))

    def drain(self):
        """Read what the client has sent and we will not use: closing with it unread would reset the
        connection over our answer."""
        while select.select([self.connection], [], [], 0.05)[0] and self.connection.recv(65536):
            pass

    def read_body(self):
        """The request body, exactly as long as Content-Length says. Anything else is a BadRequest and the
        connection is closed, since the next request could no longer be told from the body's leftovers:
        a length that is missing or not a number; a body that stops short (nothing for BODY_TIMEOUT s, or
        the client closing); bytes waiting after the body."""
        text = self.headers.get('Content-Length', '')
        if not re.fullmatch(r'[0-9]{1,9}', text):
            self.close_connection = True
            self.drain()
            raise BadRequest(f'Content-Length {text!r} is missing or not a number')
        length, body = int(text), b''
        while len(body) < length:
            if not select.select([self.connection], [], [], self.server.body_timeout)[0]:
                self.close_connection = True
                raise BadRequest(f'Content-Length says {length} bytes but only {len(body)} arrived '
                                 f'in {self.server.body_timeout:g} s')
            chunk = self.rfile.read(length - len(body))
            if not chunk:
                self.close_connection = True
                raise BadRequest(f'Content-Length says {length} bytes but the client closed after {len(body)}')
            body += chunk
        # no wait: bytes past the body sent with it are already here, and a request whose length is
        # right must not be held up (the benchmark's ttft-up would count it)
        if select.select([self.connection], [], [], 0)[0] and self.connection.recv(1, socket.MSG_PEEK):
            self.close_connection = True
            self.drain()
            raise BadRequest(f'Content-Length says {length} bytes but more of the body follows')
        return body

    @staticmethod
    def try_messages(body):
        """The messages of a request body that may not be valid JSON, for the log; [] when it has none."""
        try:
            return json.loads(body.decode('utf-8')).get('messages')
        except (AttributeError, ValueError, UnicodeDecodeError):
            return []

    def answer(self, req, conv, use, length):
        server = self.server
        messages = req['messages']
        last = messages[-1]
        results = tool_results(last['content'])
        pending = server.pending
        self.resumed = None         # the call whose result this request brings (Drop parks it again)
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
            thought = getattr(pending.chat, 'thinking_text', None)
            if thought and len(messages) >= 2:
                back = [b.get('thinking') for b in messages[-2].get('content', [])
                        if isinstance(b, dict) and b.get('type') == 'thinking']
                print(f'thinking round trip: {"PASS" if thought in back else "FAIL"} '
                      f'({len(thought)} characters sent, {[len(x or "") for x in back]} back)', flush=True)
            data = getattr(pending.chat, 'redacted_data', None)
            if data and len(messages) >= 2:
                back = [b.get('data') for b in messages[-2].get('content', [])
                        if isinstance(b, dict) and b.get('type') == 'redacted_thinking']
                print(f'redacted thinking round trip: {"PASS" if data in back else "FAIL"} '
                      f'({len(data)} bytes sent, {[len(x or "") for x in back]} back)', flush=True)
            print(f'tool {pending.call.name} -> {status}: {content[:100]!r}', flush=True)
            chat, gen = pending.chat, pending.gen
            self.resumed = pending.call
            server.pending = None
        else:
            if results:
                print('tool result with nothing pending; treating it as a new message', flush=True)
            chat = MockChat(server.speed, state=server.state, tools_path=server.tools_path,
                            threads=server.validator.threads)
            chat.history = [(text_of(messages[i]['content']), text_of(messages[i + 1]['content']))
                            for i in range(0, len(messages) - 1, 2)]
            text = text_of(last['content'])
            if results and not text:
                text = '(tool result)'
            command = text.split()[0].lower() if text.startswith('!') else None
            if command == '!tooltour':
                self.check_tools()
            gen = chat.respond(command, text)
        choice = req.get('tool_choice')
        chat.no_tools = isinstance(choice, dict) and choice.get('type') == 'none'
        thinking = req.get('thinking') or {}
        chat.progress = thinking.get('display') == 'updates'
        self.binding = BETA_BINDING in betas_of({k.lower(): v for k, v in self.headers.items()})
        chat.thread_kind = use.describe().split(' (')[0]
        chat.requests.append((chat.thread_kind, length, json.dumps(choice, sort_keys=True)))
        self.stream_reply(chat, gen, conv, use, messages)

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

    def stream_reply(self, chat, gen, conv, use, messages):
        """Run the scenario generator into one streamed message. A reply sent to its end is stored for its
        thread (unless the API would not store it); one that is not leaves a continued response as it
        was, a dropped link aside."""
        threads = self.server.validator.threads
        try:
            self.stream_message(chat, gen, conv, use, messages)
        finally:
            threads.broken(use)

    def stream_message(self, chat, gen, conv, use, messages):
        """stream_reply's work."""
        server = self.server
        threads = server.validator.threads
        burn = False
        try:
            first = next(gen)
        except StopIteration:
            first = ''
        except HttpError as e:
            self.send_error_json(e.status, e.kind, str(e), [('retry-after', str(e.retry_after))]
                                 if e.retry_after is not None else ())
            return
        except Refused:
            self.start_stream()
            msg_id = self.message_start()
            self.event('message_delta', {'delta': {'stop_reason': 'refusal', 'stop_sequence': None},
                                         'usage': {'output_tokens': 0}})
            threads.done(use, msg_id, 'refusal', None)
            self.event('message_stop', {})
            self.end_stream()
            return
        except (MockAPIError, Disconnect) as e:
            first = e                 # handled below, after message_start

        self.start_stream()
        self.chunk_every = chat.chunk_every
        msg_id = self.message_start()
        thinking = self.thinking_block(chat)
        index = 1
        text_open = False
        texts = []
        tool_id = None
        call = None
        out_tokens = 0
        stop = 'end_turn'
        item = first
        try:
            while True:
                if isinstance(item, Exception):
                    raise item
                if isinstance(item, Drop):
                    if self.resumed is not None:      # the resend brings the result again, and goes on from here
                        server.pending = Pending(chat, gen, self.resumed)
                    burn = item.burn
                    raise Disconnect()
                if isinstance(item, Ping):
                    self.event('ping', {})
                elif isinstance(item, ToolCall) and chat.no_tools:
                    # tool_choice none: the model never calls a tool, so the scenario is told the call failed
                    item.result = ToolResult('error', 'tool_choice is none: no tool call was made')
                elif isinstance(item, ToolCall):
                    if text_open:
                        self.event('content_block_stop', {'index': index})
                        index += 1
                        text_open = False
                    item.id = tool_id = f'toolu_mock_{server.next_id()}'
                    call = item
                    self.tool_use_block(index, item)
                    index += 1
                    server.pending = Pending(chat, gen, item)
                    stop = 'tool_use'
                    break
                elif item:
                    if not text_open:
                        self.event('content_block_start',
                                   {'index': index, 'content_block': {'type': 'text', 'text': ''}})
                        text_open = True
                    self.event('content_block_delta', {'index': index, 'delta': {'type': 'text_delta', 'text': item}})
                    texts.append(item)
                    out_tokens += max(1, len(item) // 4)
                    if item.endswith('[reply cut off: too long]'):
                        stop = 'max_tokens'
                try:
                    item = next(gen)
                except StopIteration:
                    break
        except Disconnect:
            threads.broken(use, burn, dropped=True)
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
        # recorded before the final events, so the client's next request finds it
        server.validator.record(conv, thinking, ''.join(texts) if texts else None, tool_id)
        content = [thinking] + ([{'type': 'text', 'text': ''.join(texts)}] if texts else [])
        if call is not None:
            content.append({'type': 'tool_use', 'id': call.id, 'name': call.name, 'input': call.args})
        threads.done(use, msg_id, stop, messages + [{'role': 'assistant', 'content': content}])
        self.event('message_delta', {'delta': {'stop_reason': stop, 'stop_sequence': None},
                                     'usage': {'output_tokens': out_tokens}})
        self.event('message_stop', {})
        self.end_stream()

    def message_start(self):
        """Send message_start; returns the message's id."""
        msg_id = f'msg_mock_{self.server.next_id()}'
        message = {'id': msg_id, 'type': 'message', 'role': 'assistant', 'model': MODEL,
                   'content': [], 'stop_reason': None, 'stop_sequence': None,
                   'usage': {'input_tokens': 1200, 'cache_creation_input_tokens': 0,
                             'cache_read_input_tokens': 1000, 'output_tokens': 1}}
        if self.binding:
            message['input_transformations'] = []     # what the binding beta adds; the client ignores it
        self.event('message_start', {'message': message})
        return msg_id

    def thinking_block(self, chat):
        """Send the reply's first block and return it as the DOS PC should echo it back: a redacted_thinking
        block (!redacted), else a thinking block (text from !think, the signature from !bigsig or a default)."""
        data = getattr(chat, 'redacted_data', None)
        if data:
            block = {'type': 'redacted_thinking', 'data': data}
            self.event('content_block_start', {'index': 0, 'content_block': block})
            self.event('content_block_stop', {'index': 0})
            return block
        text = getattr(chat, 'thinking_text', '')
        if not text and chat.progress:      # display "updates": the scenario's progress note, if any
            text = chat.progress_note
        signature = getattr(chat, 'signature', None) or SIGNATURE
        self.event('content_block_start',
                   {'index': 0, 'content_block': {'type': 'thinking', 'thinking': '', 'signature': ''}})
        third = -(-len(text) // 3)
        for i in range(0, len(text), third or 1):
            self.event('content_block_delta', {'index': 0, 'delta': {'type': 'thinking_delta',
                                                                     'thinking': text[i:i + third]}})
        self.event('content_block_delta', {'index': 0, 'delta': {'type': 'signature_delta', 'signature': signature}})
        self.event('content_block_stop', {'index': 0})
        return {'type': 'thinking', 'thinking': text, 'signature': signature}

    def tool_use_block(self, index, call):
        self.event('content_block_start', {'index': index, 'content_block': {
            'type': 'tool_use', 'id': call.id, 'name': call.name, 'input': {}}})
        text = json.dumps(call.args)
        cut = len(text) // 2
        for piece in (text[:cut], text[cut:]):
            self.event('content_block_delta',
                       {'index': index, 'delta': {'type': 'input_json_delta', 'partial_json': piece}})
        self.event('content_block_stop', {'index': index})


class MockServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr, speed, tools_path, thread_ttl=THREAD_TTL):
        super().__init__(addr, Handler)
        self.speed = speed
        self.tools_path = tools_path
        self.validator = Validator(tools_path)
        self.validator.threads.ttl = thread_ttl
        self.body_timeout = BODY_TIMEOUT
        self.state = {}             # what already happened (!drop, !retry)
        self.pending = None         # Pending tool call
        self._ids = 0
        self._lock = threading.Lock()

    def next_id(self):
        with self._lock:
            self._ids += 1
            return self._ids


def make_server(port, speed=1.0, tools_path=DEFAULT_TOOLS, host='0.0.0.0', thread_ttl=THREAD_TTL):
    return MockServer((host, port), speed, tools_path, thread_ttl)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--port', type=int, default=18023)
    ap.add_argument('--speed', type=float, default=1.0, help='scale the delays (0: none)')
    ap.add_argument('--tools', default=DEFAULT_TOOLS, help='TOOLS.JSN, to check requests and the tool tour against')
    ap.add_argument('--thread-ttl', type=float, default=THREAD_TTL,
                    help='seconds a message thread stays continuable after its last request')
    a = ap.parse_args()
    try:
        server = make_server(a.port, a.speed, a.tools, thread_ttl=a.thread_ttl)
    except (OSError, ValueError) as e:
        sys.exit(f'mockapi: cannot start ({a.tools} must be readable JSON): {e}')
    print(f'mock Messages API on port {a.port} (speed {a.speed})', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
