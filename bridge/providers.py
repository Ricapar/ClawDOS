"""Chat backends for XTBridge: Claude's own API, any OpenAI-compatible API
(OpenRouter, Z.ai, OpenAI, Ollama, LM Studio, ...), and the offline mock.

Providers are named profiles. A few are built in; bridge/providers.toml (or
--config FILE) adds more or overrides fields of the built-in ones:

  [openrouter]
  model = "z-ai/glm-4.6"

  [lmstudio]
  type = "openai"
  base_url = "http://192.168.1.20:1234/v1"
  model = "qwen3-8b"

Fields: type ("anthropic", "openai" or "mock"), name (who the assistant is,
for the system prompt), model, base_url, api_key_env (the environment variable
holding the key; keys never go in the file), headers (extra HTTP headers),
extra (extra request-body fields), effort (anthropic only).

Each chat keeps its own history, and can hand it to a different backend as a
plain-text transcript, so switching providers mid-conversation keeps context.
"""

import json
import os
import tomllib

from mockchat import MockChat, Refused
from replay import ReplayChat
from tools import (MAX_ROUNDS, TOOLS_PROMPT, ToolCall, ToolResult, anthropic_tools, openai_tools,
                   stringify, summary)

ROUND_CAP_NOTE = 'Tool limit reached; answer now without tools.'

MAX_TOKENS = 16000
FALLBACK_BETA = 'server-side-fallback-2026-07-01'

BUILTIN = {
    'anthropic': {'type': 'anthropic', 'name': 'Claude', 'model': 'claude-opus-5-5',
                  'api_key_env': 'ANTHROPIC_API_KEY', 'effort': 'low'},
    'openrouter': {'type': 'openai', 'base_url': 'https://openrouter.ai/api/v1',
                   'api_key_env': 'OPENROUTER_API_KEY', 'model': 'anthropic/claude-opus-5.5',
                   'headers': {'X-Title': 'Claude-XT'}},
    'zai': {'type': 'openai', 'name': 'GLM', 'base_url': 'https://api.z.ai/api/paas/v4',
            'api_key_env': 'ZAI_API_KEY', 'model': 'glm-4.6',
            'extra': {'thinking': {'type': 'disabled'}}},   # replies start sooner
    'openai': {'type': 'openai', 'name': 'ChatGPT', 'base_url': 'https://api.openai.com/v1',
               'api_key_env': 'OPENAI_API_KEY', 'model': 'gpt-5'},
    'ollama': {'type': 'openai', 'base_url': 'http://localhost:11434/v1', 'model': 'llama3.2'},
    'mock': {'type': 'mock', 'name': 'Claude', 'model': 'mock'},
}

SYSTEM_PROMPT = """\
You are {name}, chatting with someone through Claude-XT on a 1983 IBM PC XT \
(Intel 8088 at 4.77 MHz, 640 KB RAM, CGA display), connected to you over a \
network bridge. Your replies appear on an 80-column text-mode screen using \
code page 437, so write plain text: no Markdown syntax such as **bold**, # \
headings, tables, or code fences, because it would show up literally. Simple \
lists with "-" are fine. Keep replies conversational and fairly short; the \
screen is small and text draws slowly. Latency-sensitive; begin your visible \
answer immediately."""


class ConfigError(Exception):
    pass


def load_providers(path=None):
    """Built-in providers merged with the config file (if any)."""
    providers = {k: dict(v) for k, v in BUILTIN.items()}
    if path is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'providers.toml')
        if not os.path.exists(path):
            return providers
    try:
        with open(path, 'rb') as f:
            data = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise ConfigError(f'{path}: {e}')
    for key, fields in data.items():
        if not isinstance(fields, dict):
            raise ConfigError(f'{path}: [{key}] should be a table')
        if ':' in key:
            raise ConfigError(f'{path}: provider names cannot contain ":" ({key})')
        providers.setdefault(key.lower(), {}).update(fields)
    for key, p in providers.items():
        if p.get('type') not in ('anthropic', 'openai', 'mock'):
            raise ConfigError(f'provider {key}: type must be anthropic, openai or mock')
        if p['type'] == 'openai' and not p.get('base_url'):
            raise ConfigError(f'provider {key}: base_url is required')
        if not p.get('model'):
            raise ConfigError(f'provider {key}: model is required')
    return providers


def has_key(p):
    env = p.get('api_key_env')
    if p['type'] == 'anthropic':
        return True   # the SDK also finds `ant auth login` profiles
    return not env or bool(os.environ.get(env))


class Backends:
    """Creates chats for (provider, model), sharing one HTTP client per provider."""

    def __init__(self, providers, speed=1.0):
        self.providers = providers
        self.speed = speed
        self.clients = {}

    def client(self, key):
        if key not in self.clients:
            p = self.providers[key]
            if p['type'] == 'anthropic':
                import anthropic
                kw = {'base_url': p['base_url']} if p.get('base_url') else {}
                if p.get('api_key_env') and os.environ.get(p['api_key_env']):
                    kw['api_key'] = os.environ[p['api_key_env']]
                self.clients[key] = anthropic.AsyncAnthropic(**kw)
            else:
                import httpx
                headers = dict(p.get('headers', {}))
                if p.get('api_key_env') and os.environ.get(p['api_key_env']):
                    headers['Authorization'] = 'Bearer ' + os.environ[p['api_key_env']]
                self.clients[key] = httpx.AsyncClient(
                    base_url=p['base_url'].rstrip('/') + '/', headers=headers,
                    timeout=httpx.Timeout(30, read=300))
        return self.clients[key]

    def make(self, key, model=None, transcript=()):
        p = self.providers[key]
        model = model or p['model']
        if p['type'] == 'mock':
            chat = MockChat(self.speed)
        elif p['type'] == 'replay':
            chat = ReplayChat(p['turns'], self.speed)
        elif p['type'] == 'anthropic':
            chat = AnthropicChat(self.client(key), model, p.get('effort', 'low'), p)
        else:
            chat = OpenAIChat(self.client(key), model, p)
        chat.provider, chat.model = key, model
        if isinstance(chat, MockChat):
            users = [t for r, t in transcript if r == 'user']
            replies = [t for r, t in transcript if r == 'assistant']
            chat.history = list(zip(users, replies))
        else:
            chat.messages = [{'role': r, 'content': t} for r, t in transcript]
        return chat

    def describe(self, chat):
        if isinstance(chat, MockChat):
            return 'mock Claude (offline, send !help)'
        return f'{chat.provider}: {chat.model}'

    async def close(self):
        for c in self.clients.values():
            await c.close()
        self.clients.clear()


def _get(block, key):
    """A field of a content block, whether it is a dict or an SDK object."""
    return block.get(key) if isinstance(block, dict) else getattr(block, key, None)


def _result_status(content, is_error):
    for status in ('denied', 'stopped'):
        if content == ToolResult(status, '').model_text():
            return status
    if is_error or (is_error is None and str(content).startswith('Error: ')):
        return 'error'
    return 'ok'


def _parse_args(arguments):
    try:
        return stringify(json.loads(arguments or '{}'))
    except (ValueError, AttributeError):
        return {}


def transcript(chat):
    """A chat's history as alternating (role, text) pairs, for handing to another
    backend. Tool calls become one-line summaries inside the assistant's turn."""
    if isinstance(chat, MockChat):
        return [x for u, a in chat.history for x in (('user', u), ('assistant', a))]
    entries, calls = [], {}

    def finish(tool_id, content, is_error):
        if tool_id in calls:
            calls[tool_id].result = ToolResult(_result_status(content, is_error), '')

    for m in chat.messages:
        role, content = m['role'], m.get('content')
        if role == 'tool':                     # OpenAI tool result
            finish(m.get('tool_call_id'), content, None)
            continue
        parts = []
        if isinstance(content, str):
            parts.append(content)
        else:
            for b in content or []:
                kind = _get(b, 'type')
                if kind == 'text':
                    parts.append(_get(b, 'text'))
                elif kind == 'tool_use':
                    calls[_get(b, 'id')] = ToolCall(_get(b, 'name'), stringify(_get(b, 'input')))
                    parts.append(calls[_get(b, 'id')])
                elif kind == 'tool_result':
                    finish(_get(b, 'tool_use_id'), _get(b, 'content'), _get(b, 'is_error'))
        for tc in m.get('tool_calls') or []:   # OpenAI tool calls
            calls[tc['id']] = ToolCall(tc['function']['name'], _parse_args(tc['function']['arguments']))
            parts.append(calls[tc['id']])
        parts = [p for p in parts if p]
        if not parts:
            continue                           # tool results: part of the assistant's turn
        if entries and entries[-1][0] == role:
            entries[-1][1].extend(parts)
        else:
            entries.append((role, parts))
    return [(role, '\n'.join(summary(p) if isinstance(p, ToolCall) else p for p in parts))
            for role, parts in entries]


def system_prompt(p):
    return SYSTEM_PROMPT.format(name=p.get('name', 'a helpful AI assistant')) + '\n\n' + TOOLS_PROMPT


# --- Claude (native API) -------------------------------------------------------

def echo_blocks(content):
    """Assistant content to send back next turn. After a mid-output fallback,
    model-internal blocks that precede the last fallback marker must be dropped."""
    last = max((i for i, b in enumerate(content) if b.type == 'fallback'), default=-1)
    keep = []
    for i, b in enumerate(content):
        if i < last and b.type not in ('text', 'fallback'):
            continue
        keep.append(b)
    return keep


def tool_result(tool_use_id, result):
    return {'type': 'tool_result', 'tool_use_id': tool_use_id,
            'content': result.model_text(), 'is_error': result.status != 'ok'}


class AnthropicChat:
    """One conversation with Claude, including rounds of tool use on the XT.

    reply() yields text chunks and ToolCalls; whoever iterates sets each call's
    result before asking for more. The history always stays valid for the API:
    if a reply is cut short, tool calls left unanswered get an "interrupted"
    result, and a reply that produced nothing is forgotten."""

    def __init__(self, client, model, effort, p):
        self.client = client
        self.model = model
        self.effort = effort
        self.system = system_prompt(p)
        self.messages = []

    def _add_user(self, text):
        """Append the user's text; after tool results it joins that message,
        since roles must alternate. Returns (index before, merged)."""
        start = len(self.messages)
        last = self.messages[-1] if self.messages else None
        if last and last['role'] == 'user' and isinstance(last['content'], list):
            last['content'].append({'type': 'text', 'text': text})
            return start, True
        self.messages.append({'role': 'user', 'content': text})
        return start, False

    def _settle(self, start, merged, results):
        if not any(m['role'] == 'assistant' for m in self.messages[start:]):
            if merged:
                self.messages[start - 1]['content'].pop()
            else:
                del self.messages[start:]
            return
        last = self.messages[-1]
        if last['role'] != 'assistant':
            return
        ids = [_get(b, 'id') for b in last['content'] if _get(b, 'type') == 'tool_use']
        if ids:
            answered = {r['tool_use_id'] for r in results}
            missing = [tool_result(i, ToolResult('stopped', '')) for i in ids if i not in answered]
            self.messages.append({'role': 'user', 'content': list(results) + missing})

    async def reply(self, text):
        start, merged = self._add_user(text)
        results = []
        try:
            for round in range(MAX_ROUNDS + 1):
                extra = {}
                if round == MAX_ROUNDS:
                    extra['tool_choice'] = {'type': 'none'}
                    self.messages[-1]['content'].append({'type': 'text', 'text': ROUND_CAP_NOTE})
                async with self.client.beta.messages.stream(
                    model=self.model,
                    max_tokens=MAX_TOKENS,
                    system=self.system,
                    messages=self.messages,
                    tools=anthropic_tools(),
                    output_config={'effort': self.effort},
                    betas=[FALLBACK_BETA],
                    fallbacks='default',
                    **extra,
                ) as stream:
                    async for chunk in stream.text_stream:
                        yield chunk
                    final = await stream.get_final_message()
                if final.stop_reason == 'refusal':
                    raise Refused('Claude declined to answer that.')
                kept = echo_blocks(final.content)
                self.messages.append({'role': 'assistant', 'content': kept})
                if final.stop_reason == 'max_tokens':
                    yield '\n[reply cut off: too long]'
                if final.stop_reason != 'tool_use':
                    # A reply cut off mid-call leaves a tool_use that must be answered.
                    cut = [tool_result(_get(b, 'id'), ToolResult('error', 'reply cut off before the call was complete'))
                           for b in kept if _get(b, 'type') == 'tool_use']
                    if cut:
                        self.messages.append({'role': 'user', 'content': cut})
                    return
                for block in final.content:
                    if block.type == 'tool_use':
                        call = ToolCall(block.name, stringify(block.input))
                        yield call
                        results.append(tool_result(block.id, call.result))
                self.messages.append({'role': 'user', 'content': results})
                results = []
        except BaseException:
            self._settle(start, merged, results)
            raise


# --- OpenAI-compatible chat completions ---------------------------------------

class APIError(Exception):
    pass


class OpenAIChat:
    """One conversation over /chat/completions with streaming (SSE)."""

    def __init__(self, client, model, p):
        self.client = client
        self.model = model
        self.system = system_prompt(p)
        self.extra = p.get('extra', {})
        self.messages = []

    def _settle(self, start, answered):
        """After a failed or interrupted reply: forget it if nothing came back,
        otherwise answer any tool calls left open."""
        if not any(m['role'] == 'assistant' for m in self.messages[start:]):
            del self.messages[start:]
            return
        # The last assistant message may already be followed by some answers.
        last = next(m for m in reversed(self.messages) if m['role'] == 'assistant')
        answered = {m.get('tool_call_id') for m in self.messages if m['role'] == 'tool'}
        for tc in last.get('tool_calls') or []:
            if tc['id'] not in answered:
                self.messages.append({'role': 'tool', 'tool_call_id': tc['id'],
                                      'content': ToolResult('stopped', '').model_text()})

    async def reply(self, text):
        start = len(self.messages)
        self.messages.append({'role': 'user', 'content': text})
        answered = set()
        try:
            for round in range(MAX_ROUNDS + 1):
                body = {'model': self.model, 'stream': True, 'max_tokens': MAX_TOKENS,
                        'messages': [{'role': 'system', 'content': self.system}] + self.messages,
                        'tools': openai_tools(), **self.extra}
                if round == MAX_ROUNDS:
                    body['tool_choice'] = 'none'
                parts, calls = [], {}
                async for piece in self._stream(body, parts, calls):
                    yield piece
                finish = self._finish
                if finish == 'content_filter' and not parts and not calls:
                    raise Refused('The model declined to answer that.')
                message = {'role': 'assistant', 'content': ''.join(parts) or None}
                if calls:
                    message['tool_calls'] = [{'id': c['id'], 'type': 'function',
                                              'function': {'name': c['name'], 'arguments': c['arguments']}}
                                             for _, c in sorted(calls.items())]
                else:
                    message['content'] = message['content'] or ''
                self.messages.append(message)
                if finish == 'length':
                    yield '\n[reply cut off: too long]'
                if not calls:
                    return
                answered = set()
                for tc in message['tool_calls']:
                    try:
                        args = json.loads(tc['function']['arguments'] or '{}')
                        if not isinstance(args, dict):
                            raise ValueError
                    except ValueError:
                        result = ToolResult('error', 'arguments were not valid JSON')
                    else:
                        call = ToolCall(tc['function']['name'], stringify(args))
                        yield call
                        result = call.result
                    self.messages.append({'role': 'tool', 'tool_call_id': tc['id'],
                                          'content': result.model_text()})
                    answered.add(tc['id'])
        except BaseException:
            self._settle(start, answered)
            raise

    async def _stream(self, body, parts, calls):
        """POST one streaming request; yields text, fills parts and calls
        (index -> id/name/arguments), and leaves the finish reason in _finish."""
        self._finish = None
        async with self.client.stream('POST', 'chat/completions', json=body) as r:
            if r.status_code != 200:
                raise APIError(f'API error {r.status_code}: {error_message(await r.aread())}')
            async for line in r.aiter_lines():
                if not line.startswith('data:'):
                    continue   # blank separators, ": keep-alive" comments
                data = line[5:].strip()
                if data == '[DONE]':
                    break
                event = json.loads(data)
                if 'error' in event:   # OpenRouter reports mid-stream failures this way
                    raise APIError(error_message(data.encode()))
                for choice in event.get('choices', []):
                    delta = choice.get('delta') or {}
                    piece = delta.get('content')
                    if piece:
                        parts.append(piece)
                        yield piece
                    for tc in delta.get('tool_calls') or []:
                        c = calls.setdefault(tc.get('index', 0), {'id': '', 'name': '', 'arguments': ''})
                        c['id'] = tc.get('id') or c['id']
                        fn = tc.get('function') or {}
                        c['name'] = fn.get('name') or c['name']
                        c['arguments'] += fn.get('arguments') or ''
                    self._finish = choice.get('finish_reason') or self._finish


def error_message(raw):
    try:
        err = json.loads(raw).get('error', raw)
        if isinstance(err, dict):
            err = err.get('message', err)
        return str(err)[:160]
    except (ValueError, AttributeError):
        return raw.decode('utf-8', 'replace')[:160] or 'no details'
