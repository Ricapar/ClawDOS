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


def transcript(chat):
    """A chat's history as (role, text) pairs, for handing to another backend."""
    if isinstance(chat, MockChat):
        return [x for u, a in chat.history for x in (('user', u), ('assistant', a))]
    out = []
    for m in chat.messages:
        c = m['content']
        if not isinstance(c, str):
            c = ''.join(getattr(b, 'text', '') for b in c if getattr(b, 'type', '') == 'text')
        out.append((m['role'], c))
    return out


def system_prompt(p):
    return SYSTEM_PROMPT.format(name=p.get('name', 'a helpful AI assistant'))


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


class AnthropicChat:
    """One conversation with Claude. History is append-only."""

    def __init__(self, client, model, effort, p):
        self.client = client
        self.model = model
        self.effort = effort
        self.system = system_prompt(p)
        self.messages = []

    async def reply(self, text):
        self.messages.append({'role': 'user', 'content': text})
        try:
            async with self.client.beta.messages.stream(
                model=self.model,
                max_tokens=MAX_TOKENS,
                system=self.system,
                messages=self.messages,
                output_config={'effort': self.effort},
                betas=[FALLBACK_BETA],
                fallbacks='default',
            ) as stream:
                async for chunk in stream.text_stream:
                    yield chunk
                final = await stream.get_final_message()
        except BaseException:
            self.messages.pop()  # never sent a reply; drop the dangling user turn
            raise
        if final.stop_reason == 'refusal':
            self.messages.pop()
            raise Refused('Claude declined to answer that.')
        self.messages.append({'role': 'assistant', 'content': echo_blocks(final.content)})
        if final.stop_reason == 'max_tokens':
            yield '\n[reply cut off: too long]'


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

    async def reply(self, text):
        self.messages.append({'role': 'user', 'content': text})
        body = {'model': self.model, 'stream': True, 'max_tokens': MAX_TOKENS,
                'messages': [{'role': 'system', 'content': self.system}] + self.messages,
                **self.extra}
        parts, finish = [], None
        try:
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
                        piece = (choice.get('delta') or {}).get('content')
                        if piece:
                            parts.append(piece)
                            yield piece
                        finish = choice.get('finish_reason') or finish
        except BaseException:
            self.messages.pop()
            raise
        if finish == 'content_filter' and not parts:
            self.messages.pop()
            raise Refused('The model declined to answer that.')
        self.messages.append({'role': 'assistant', 'content': ''.join(parts)})
        if finish == 'length':
            yield '\n[reply cut off: too long]'


def error_message(raw):
    try:
        err = json.loads(raw).get('error', raw)
        if isinstance(err, dict):
            err = err.get('message', err)
        return str(err)[:160]
    except (ValueError, AttributeError):
        return raw.decode('utf-8', 'replace')[:160] or 'no details'
