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
import logging
import os
import time
import tomllib

import prompts
from mockchat import MockChat, Refused
from replay import ReplayChat
from tools import (MAX_ROUNDS, TOOLS_PROMPT, ToolCall, ToolResult, anthropic_tools, memory_prompt,
                   openai_tools, stringify, summary)

log = logging.getLogger('xtbridge')

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


class ConfigError(Exception):
    """The provider configuration (or the prompt file) is unusable."""


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
        raise ConfigError(f'{path}: {e}') from e
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
    """Whether provider p can authenticate: its key variable is set (or none is needed)."""
    env = p.get('api_key_env')
    if p['type'] == 'anthropic':
        return True   # the SDK also finds `ant auth login` profiles
    return not env or bool(os.environ.get(env))


class Backends:
    """Creates chats for (provider, model), sharing one HTTP client per provider."""

    def __init__(self, providers, speed=1.0, prompt=prompts.DEFAULT, prompt_dir=None):
        self.providers = providers
        self.speed = speed
        self.clients = {}
        self.prompt = prompt
        self.prompt_dir = prompt_dir
        self.prompt_text = None   # the last good read, if the file goes away

    def load_prompt(self):
        """Read the prompt file afresh, so edits apply to the next conversation."""
        try:
            self.prompt_text = prompts.load(self.prompt, self.prompt_dir)
        except prompts.PromptError as e:
            if self.prompt_text is None:
                raise ConfigError(str(e)) from e
            log.warning('%s; keeping the prompt read earlier', e)
        return self.prompt_text

    def client(self, key):
        """The HTTP client for provider `key`, created on first use."""
        if key not in self.clients:
            p = self.providers[key]
            if p['type'] == 'anthropic':
                import anthropic   # pylint: disable=import-outside-toplevel  # optional SDK, loaded only if used
                kw = {'base_url': p['base_url']} if p.get('base_url') else {}
                if p.get('api_key_env') and os.environ.get(p['api_key_env']):
                    kw['api_key'] = os.environ[p['api_key_env']]
                self.clients[key] = anthropic.AsyncAnthropic(**kw)
            else:
                import httpx   # pylint: disable=import-outside-toplevel  # loaded only if an OpenAI-style provider is used
                headers = dict(p.get('headers', {}))
                if p.get('api_key_env') and os.environ.get(p['api_key_env']):
                    headers['Authorization'] = 'Bearer ' + os.environ[p['api_key_env']]
                self.clients[key] = httpx.AsyncClient(
                    base_url=p['base_url'].rstrip('/') + '/', headers=headers,
                    timeout=httpx.Timeout(30, read=300))
        return self.clients[key]

    def make(self, key, model=None, history=()):
        """A new chat for provider `key`, seeded with `history` (role, text) pairs."""
        p = dict(self.providers[key], prompt_text=self.load_prompt())
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
            users = [t for r, t in history if r == 'user']
            replies = [t for r, t in history if r == 'assistant']
            chat.history = list(zip(users, replies))
        else:
            chat.messages = [{'role': r, 'content': t} for r, t in history]
        return chat

    def describe(self, chat):
        """One line saying which backend, model and prompt a chat uses."""
        if isinstance(chat, MockChat):
            return 'mock Claude (offline, send !help)'
        prompt = '' if self.prompt == prompts.DEFAULT else f', prompt {self.prompt}'
        return f'{chat.provider}: {chat.model}{prompt}'

    async def close(self):
        """Close every HTTP client."""
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


def _finish_call(calls, tool_id, content, is_error):
    """Record a tool result on the ToolCall it answers (if we saw the call)."""
    if tool_id in calls:
        calls[tool_id].result = ToolResult(_result_status(content, is_error), '')


def _block_parts(block, calls):
    """The transcript parts of one Anthropic content block."""
    kind = _get(block, 'type')
    if kind == 'text':
        return [_get(block, 'text')]
    if kind == 'tool_use':
        calls[_get(block, 'id')] = ToolCall(_get(block, 'name'), stringify(_get(block, 'input')))
        return [calls[_get(block, 'id')]]
    if kind == 'tool_result':
        _finish_call(calls, _get(block, 'tool_use_id'), _get(block, 'content'), _get(block, 'is_error'))
    return []


def _message_parts(m, calls):
    """The text pieces and ToolCalls of one history message. Tool results it
    carries are filed on their calls, which are collected in `calls` by id."""
    content = m.get('content')
    if m['role'] == 'tool':                    # OpenAI tool result
        _finish_call(calls, m.get('tool_call_id'), content, None)
        return []
    parts = []
    if isinstance(content, str):
        parts.append(content)
    else:
        for b in content or []:
            parts.extend(_block_parts(b, calls))
    for tc in m.get('tool_calls') or []:       # OpenAI tool calls
        calls[tc['id']] = ToolCall(tc['function']['name'], _parse_args(tc['function']['arguments']))
        parts.append(calls[tc['id']])
    return [p for p in parts if p]


def transcript(chat):
    """A chat's history as alternating (role, text) pairs, for handing to another
    backend. Tool calls become one-line summaries inside the assistant's turn."""
    if isinstance(chat, MockChat):
        return [x for u, a in chat.history for x in (('user', u), ('assistant', a))]
    entries, calls = [], {}
    for m in chat.messages:
        parts = _message_parts(m, calls)
        if not parts:
            continue                           # tool results: part of the assistant's turn
        if entries and entries[-1][0] == m['role']:
            entries[-1][1].extend(parts)
        else:
            entries.append((m['role'], parts))
    return [(role, '\n'.join(summary(p) if isinstance(p, ToolCall) else p for p in parts))
            for role, parts in entries]


def system_prompt(p):
    """The prompt file's text (Backends puts it in p['prompt_text']; else
    prompts/default.txt) with {name} filled in, then the add-ons: the tools."""
    name = p.get('name', 'a helpful AI assistant')
    text = p.get('prompt_text') or prompts.load(prompts.DEFAULT)
    return text.replace('{name}', name) + '\n\n' + TOOLS_PROMPT


def uses_system_prompt(chat):
    """Whether the chat builds its own system prompt (the mock backends do not)."""
    return hasattr(chat, 'base_system')


def set_memories(chat, index):
    """Put the memories add-on (memory_list's output; None: not listed) after
    the chat's system prompt."""
    if uses_system_prompt(chat):
        chat.system = chat.base_system + ('' if index is None else '\n\n' + memory_prompt(index))


def report(chat, index, started, first, **fields):
    """Tell chat.on_api_call (set by the bridge's session, if any) about one API
    round: model, stop_reason and usage in fields; started/first are monotonic
    times of the request and of its first text (None if it had none)."""
    callback = getattr(chat, 'on_api_call', None)
    if callback:
        now = time.monotonic()
        callback({'round': index, **fields, 'ms': int((now - started) * 1000),
                  'ttft_ms': int((first - started) * 1000) if first else None})


def anthropic_usage(message):
    """Token counts from an Anthropic message, in the form report() takes."""
    u = getattr(message, 'usage', None)
    return {'input': getattr(u, 'input_tokens', 0) or 0,
            'output': getattr(u, 'output_tokens', 0) or 0,
            'cache_read': getattr(u, 'cache_read_input_tokens', 0) or 0,
            'cache_write': getattr(u, 'cache_creation_input_tokens', 0) or 0}


def openai_usage(u):
    """Token counts from an OpenAI-style usage object, in the form report() takes."""
    u = u or {}
    return {'input': u.get('prompt_tokens', 0) or 0,
            'output': u.get('completion_tokens', 0) or 0,
            'cache_read': (u.get('prompt_tokens_details') or {}).get('cached_tokens', 0) or 0,
            'cache_write': 0}


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
    """An Anthropic tool_result block for a finished ToolCall."""
    return {'type': 'tool_result', 'tool_use_id': tool_use_id,
            'content': result.model_text(), 'is_error': result.status != 'ok'}


class AnthropicChat:   # pylint: disable=too-few-public-methods  # duck-typed chat: reply() is the interface
    """One conversation with Claude, including rounds of tool use on the XT.

    reply() yields text chunks and ToolCalls; whoever iterates sets each call's
    result before asking for more. The history always stays valid for the API:
    if a reply is cut short, tool calls left unanswered get an "interrupted"
    result, and a reply that produced nothing is forgotten."""

    def __init__(self, client, model, effort, p):
        self.client = client
        self.model = model
        self.effort = effort
        self.system = self.base_system = system_prompt(p)
        self.messages = []
        self.provider = None   # set by Backends.make

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

    def _answer_cut_calls(self, kept):
        """A reply cut off mid-call leaves a tool_use that must be answered."""
        cut = [tool_result(_get(b, 'id'), ToolResult('error', 'reply cut off before the call was complete'))
               for b in kept if _get(b, 'type') == 'tool_use']
        if cut:
            self.messages.append({'role': 'user', 'content': cut})

    async def reply(self, text):
        """Send the user's text; yields text chunks and ToolCalls (see the class)."""
        start, merged = self._add_user(text)
        results = []
        try:
            for rnd in range(MAX_ROUNDS + 1):
                extra = {}
                if rnd == MAX_ROUNDS:
                    extra['tool_choice'] = {'type': 'none'}
                    self.messages[-1]['content'].append({'type': 'text', 'text': ROUND_CAP_NOTE})
                started, first = time.monotonic(), None
                async with self.client.beta.messages.stream(
                    model=self.model,
                    max_tokens=MAX_TOKENS,
                    # Prompt caching: a breakpoint on the system prompt caches the
                    # tools and system together; the top-level one follows the
                    # growing conversation, so each round reads what the last wrote.
                    system=[{'type': 'text', 'text': self.system, 'cache_control': {'type': 'ephemeral'}}],
                    cache_control={'type': 'ephemeral'},
                    messages=self.messages,
                    tools=anthropic_tools(),
                    output_config={'effort': self.effort},
                    betas=[FALLBACK_BETA],
                    fallbacks='default',
                    **extra,
                ) as stream:
                    async for chunk in stream.text_stream:
                        first = first or time.monotonic()
                        yield chunk
                    final = await stream.get_final_message()
                report(self, rnd, started, first, model=getattr(final, 'model', None) or self.model,
                       stop_reason=final.stop_reason, usage=anthropic_usage(final))
                if final.stop_reason == 'refusal':
                    raise Refused('Claude declined to answer that.')
                kept = echo_blocks(final.content)
                self.messages.append({'role': 'assistant', 'content': kept})
                if final.stop_reason == 'max_tokens':
                    yield '\n[reply cut off: too long]'
                if final.stop_reason != 'tool_use':
                    self._answer_cut_calls(kept)
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
    """The API answered with an error (or an error event mid-stream)."""


class OpenAIChat:   # pylint: disable=too-few-public-methods,too-many-instance-attributes
    # (duck-typed chat: reply() is the interface; _finish, _usage, _served, _first: the last round's state)
    """One conversation over /chat/completions with streaming (SSE)."""

    def __init__(self, client, model, p):
        self.client = client
        self.model = model
        self.system = self.base_system = system_prompt(p)
        self.extra = p.get('extra', {})
        self.stream_usage = p.get('stream_usage', True)   # false for servers that reject stream_options
        self.messages = []
        self.provider = None   # set by Backends.make
        self._finish = self._usage = self._served = self._first = None   # of the last round

    def _settle(self, start):
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

    @staticmethod
    def _assistant_message(parts, calls):
        """The assistant message for a round's streamed text and tool calls."""
        message = {'role': 'assistant', 'content': ''.join(parts) or None}
        if calls:
            message['tool_calls'] = [{'id': c['id'], 'type': 'function',
                                      'function': {'name': c['name'], 'arguments': c['arguments']}}
                                     for _, c in sorted(calls.items())]
        else:
            message['content'] = message['content'] or ''
        return message

    async def _run_tools(self, message):
        """Yield each of the message's tool calls for the caller to run, and
        append the tool messages that answer them."""
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

    async def reply(self, text):
        """Send the user's text; yields text chunks and ToolCalls, like AnthropicChat."""
        start = len(self.messages)
        self.messages.append({'role': 'user', 'content': text})
        try:
            for rnd in range(MAX_ROUNDS + 1):
                body = {'model': self.model, 'stream': True, 'max_tokens': MAX_TOKENS,
                        'messages': [{'role': 'system', 'content': self.system}] + self.messages,
                        'tools': openai_tools(), **self.extra}
                if self.stream_usage:
                    body['stream_options'] = {'include_usage': True}
                if rnd == MAX_ROUNDS:
                    body['tool_choice'] = 'none'
                parts, calls = [], {}
                started = time.monotonic()
                async for piece in self._stream(body, parts, calls):
                    yield piece
                finish = self._finish
                report(self, rnd, started, self._first, model=self._served or self.model,
                       stop_reason=finish, usage=openai_usage(self._usage))
                if finish == 'content_filter' and not parts and not calls:
                    raise Refused('The model declined to answer that.')
                message = self._assistant_message(parts, calls)
                self.messages.append(message)
                if finish == 'length':
                    yield '\n[reply cut off: too long]'
                if not calls:
                    return
                async for call in self._run_tools(message):
                    yield call
        except BaseException:
            self._settle(start)
            raise

    async def _stream(self, body, parts, calls):
        """POST one streaming request; yields text, fills parts and calls
        (index -> id/name/arguments), and leaves the finish reason in _finish."""
        self._finish = None
        self._usage = self._served = self._first = None
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
                self._usage = event.get('usage') or self._usage
                self._served = event.get('model') or self._served
                if 'error' in event:   # OpenRouter reports mid-stream failures this way
                    raise APIError(error_message(data.encode()))
                for choice in event.get('choices', []):
                    delta = choice.get('delta') or {}
                    piece = delta.get('content')
                    if piece:
                        self._first = self._first or time.monotonic()
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
    """A short human-readable message from an API error body (bytes or str)."""
    try:
        err = json.loads(raw).get('error', raw)
        if isinstance(err, dict):
            err = err.get('message', err)
        return str(err)[:160]
    except (ValueError, AttributeError):
        return raw.decode('utf-8', 'replace')[:160] or 'no details'
