#!/usr/bin/env python3
"""XTBridge: lets a DOS machine chat with Claude (or another LLM) over a plain
TCP connection.

The XT can't do modern TLS, so this runs on a nearby modern machine (a PC, or a
Raspberry Pi tucked inside the XT's case). It speaks a tiny line protocol in
code page 437 to the XT (see docs/PROTOCOL.md) and HTTPS to the model's API.

  python bridge/xtbridge.py                         # Claude, port 8023
  python bridge/xtbridge.py --provider openrouter   # any provider in providers.py
  python bridge/xtbridge.py --provider zai --model glm-4.6
  python bridge/xtbridge.py --mock                  # offline mock of Claude (no key)
  python bridge/xtbridge.py --token SECRET          # require AUTH from clients

From the XT, "/model" lists providers and "/model openrouter:some/model"
switches the current conversation. providers.py has the built-in providers;
bridge/providers.toml adds your own. API keys come from the environment
(ANTHROPIC_API_KEY or an `ant auth login` profile, OPENROUTER_API_KEY,
ZAI_API_KEY, OPENAI_API_KEY).
"""

import argparse
import asyncio
import logging
import unicodedata

import providers
from mockchat import Disconnect, MockAPIError, Refused
from providers import APIError, Backends, ConfigError, transcript

PROTOCOL_VERSION = 1
DEFAULT_PORT = 8023
MAX_LINE = 4096          # longest line accepted from a client
MAX_TXT = 500            # longest payload sent to a client

log = logging.getLogger('xtbridge')


# --- text conversion ---------------------------------------------------------

# Characters CP437 lacks, mapped to close equivalents.
CP437_FALLBACKS = {
    '‘': "'", '’': "'", '‚': "'", '‛': "'",
    '“': '"', '”': '"', '„': '"', '″': '"', '′': "'",
    '–': '-', '—': '--', '―': '--', '−': '-', '‐': '-', '‑': '-',
    '…': '...', ' ': ' ', ' ': ' ', ' ': ' ', ' ': ' ',
    '​': '', '‍': '', '️': '',
    '→': '->', '←': '<-', '⇒': '=>', '↔': '<->',
    '•': '∙', '◦': 'o', '‣': '>', '×': 'x',
    '≤': '<=', '≥': '>=', '≠': '!=', '≈': '~',
    '✓': 'v', '✔': 'v', '✗': 'x', '✘': 'x',
    '©': '(c)', '®': '(R)', '™': '(TM)', '€': 'EUR',
    '\t': '    ',
}


def to_cp437(text):
    """Unicode -> CP437 bytes, approximating anything CP437 can't show."""
    out = bytearray()
    for ch in text:
        try:
            out += ch.encode('cp437')
            continue
        except UnicodeEncodeError:
            pass
        sub = CP437_FALLBACKS.get(ch)
        if sub is None:
            base = unicodedata.normalize('NFKD', ch)
            base = ''.join(c for c in base if not unicodedata.combining(c))
            sub = base if base and base != ch else '?'
        out += sub.encode('cp437', errors='replace')
    return bytes(out)


def from_cp437(data):
    return data.decode('cp437')


# --- protocol ----------------------------------------------------------------

class Session:
    def __init__(self, reader, writer, args, backends):
        self.reader = reader
        self.writer = writer
        self.args = args
        self.backends = backends
        self.chat = backends.make(args.provider, args.model)
        self.task = None
        self.authed = not args.token
        self.peer = writer.get_extra_info('peername')

    async def send(self, verb, payload=b''):
        # Keep lines short for the XT's receive buffer; TXT can be split freely.
        while verb == 'TXT' and len(payload) > MAX_TXT:
            self.writer.write(b'TXT ' + payload[:MAX_TXT] + b'\n')
            payload = payload[MAX_TXT:]
        line = verb.encode('ascii') + (b' ' + payload[:MAX_TXT] if payload else b'') + b'\n'
        self.writer.write(line)
        await self.writer.drain()

    async def run(self):
        log.info('%s connected', self.peer)
        await self.send('HELLO', f'XTBRIDGE {PROTOCOL_VERSION}{" AUTH" if self.args.token else ""}'.encode())
        try:
            while True:
                try:
                    raw = await self.reader.readuntil(b'\n')
                except asyncio.LimitOverrunError:
                    await self.send('ERR', b'line too long')
                    await self.reader.read(MAX_LINE)
                    continue
                verb, _, rest = raw.rstrip(b'\r\n').partition(b' ')
                if not await self.command(verb.decode('ascii', 'replace').upper(), rest):
                    break
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            if self.task:
                self.task.cancel()
            self.writer.close()
            log.info('%s disconnected', self.peer)

    async def command(self, verb, rest):
        if verb == 'QUIT':
            await self.send('BYE')
            return False
        if verb == 'PING':
            await self.send('PONG')
            return True
        if verb == 'AUTH':
            self.authed = not self.args.token or from_cp437(rest) == self.args.token
            await self.send('OK' if self.authed else 'ERR', b'' if self.authed else b'bad token')
            return True
        if not self.authed:
            await self.send('ERR', b'AUTH required')
            return True
        if verb == 'INFO':
            await self.send('INFO', to_cp437(self.backends.describe(self.chat)))
        elif verb == 'NEW':
            if self.task:
                self.task.cancel()
            self.chat = self.backends.make(self.chat.provider, self.chat.model)
            await self.send('OK')
        elif verb == 'STOP':
            if self.task:
                self.task.cancel()
        elif verb == 'MSG':
            text = from_cp437(rest).strip()
            if self.task and not self.task.done():
                await self.send('ERR', b'busy')
            elif not text:
                await self.send('ERR', b'empty message')
            elif text.split()[0].lower() in ('/model', '/provider'):
                await self.say(self.switch(text.split(None, 1)[1:]))
            else:
                self.task = asyncio.create_task(self.answer(text))
        else:
            await self.send('ERR', b'unknown command')
        return True

    async def say(self, text):
        """A whole reply written by the bridge itself."""
        for i, line in enumerate(text.split('\n')):
            if i:
                await self.send('NL')
            if line:
                await self.send('TXT', to_cp437(line))
        await self.send('END')

    def switch(self, arg):
        """/model [provider | provider:model | model]: list or switch backends,
        keeping the conversation so far."""
        table = self.backends.providers
        if not arg:
            lines = [f'Now using {self.backends.describe(self.chat)}. Providers:']
            for key, p in table.items():
                note = '' if providers.has_key(p) else f'  (no {p["api_key_env"]})'
                lines.append(f'  {key:<11} {p["model"]}{note}')
            lines.append('Switch with /model provider, /model provider:model, or /model model.')
            return '\n'.join(lines)
        key, sep, model = arg[0].strip().partition(':')
        if key.lower() in table:
            key = key.lower()
        elif sep:
            return f'Unknown provider "{key}". Send /model for the list.'
        else:   # a model name on the current provider
            key, model = self.chat.provider, arg[0].strip()
        if not providers.has_key(table[key]):
            return f'{key} needs an API key: set {table[key]["api_key_env"]} where the bridge runs.'
        history = transcript(self.chat)
        self.chat = self.backends.make(key, model or None, history)
        log.info('%s switched to %s', self.peer, self.backends.describe(self.chat))
        kept = f', keeping {len(history) // 2} earlier exchange(s)' if history else ''
        return f'Switched to {self.backends.describe(self.chat)}{kept}.'

    async def answer(self, text):
        log.info('%s >> %s', self.peer, text)
        pending = ''
        try:
            async for chunk in self.chat.reply(text):
                pending += chunk
                *lines, pending = pending.split('\n')
                for line in lines:
                    if line:
                        await self.send('TXT', to_cp437(line))
                    await self.send('NL')
                if pending:
                    await self.send('TXT', to_cp437(pending))
                    pending = ''
            await self.send('END')
        except asyncio.CancelledError:
            await self.send('END', b'stopped')
        except Refused as e:
            await self.send('ERR', to_cp437(str(e)))
        except Disconnect:
            log.info('%s dropped on purpose (mock)', self.peer)
            self.writer.close()
        except (MockAPIError, APIError) as e:
            log.info('%s API error: %s', self.peer, e)
            await self.send('ERR', to_cp437(str(e)))
        except Exception as e:  # report API trouble to the XT instead of dropping the link
            log.exception('reply failed')
            await self.send('ERR', to_cp437(describe_error(e)))


def describe_error(e):
    try:
        import httpx
        if isinstance(e, httpx.TimeoutException):
            return 'The model API timed out.'
        if isinstance(e, httpx.TransportError):
            return 'Bridge could not reach the model API.'
    except ImportError:
        pass
    try:
        import anthropic
    except ImportError:
        return f'{type(e).__name__}: {e}'[:200]
    if isinstance(e, anthropic.AuthenticationError):
        return 'Bridge has no valid Anthropic API key.'
    if isinstance(e, anthropic.RateLimitError):
        return 'Rate limited by the API; try again shortly.'
    if isinstance(e, anthropic.APIStatusError):
        return f'API error {e.status_code}: {e.message}'[:200]
    if isinstance(e, anthropic.APIConnectionError):
        return 'Bridge could not reach the Claude API.'
    return f'{type(e).__name__}: {e}'[:200]


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', default='0.0.0.0')
    ap.add_argument('--port', type=int, default=DEFAULT_PORT)
    ap.add_argument('--provider', default='anthropic',
                    help='backend new conversations start on (see providers.py)')
    ap.add_argument('--model', help="model to use instead of the provider's default")
    ap.add_argument('--effort', choices=['low', 'medium', 'high', 'xhigh', 'max'],
                    help='anthropic only: reasoning effort (default low)')
    ap.add_argument('--config', help='provider config file (default: bridge/providers.toml if present)')
    ap.add_argument('--token', help='shared secret clients must send with AUTH')
    ap.add_argument('--mock', '--echo', action='store_true',
                    help='same as --provider mock: offline mock of Claude; send !help for test commands')
    ap.add_argument('--speed', type=float, default=1.0,
                    help='mock only: scale delays (2 = twice as fast, 0 = no delays)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format='%(asctime)s %(message)s', datefmt='%H:%M:%S')
    try:
        server = await start(args)
    except ConfigError as e:
        raise SystemExit(f'xtbridge: {e}')
    log.info('XTBridge on %s:%d, backend: %s', args.host, args.port, args.backend_name)
    async with server:
        await server.serve_forever()


async def start(args):
    """Start listening; returns the asyncio server. Sets args.backend_name."""
    table = providers.load_providers(getattr(args, 'config', None))
    args.provider = 'mock' if args.mock else (getattr(args, 'provider', None) or 'anthropic')
    args.model = getattr(args, 'model', None)
    if args.provider not in table:
        raise ConfigError(f'unknown provider {args.provider} (have: {", ".join(table)})')
    p = table[args.provider]
    if getattr(args, 'effort', None):
        p['effort'] = args.effort
    if not providers.has_key(p):
        raise ConfigError(f'{args.provider} needs an API key in {p["api_key_env"]}')
    backends = Backends(table, getattr(args, 'speed', 1.0))
    args.backend_name = backends.describe(backends.make(args.provider, args.model))

    async def handle(reader, writer):
        await Session(reader, writer, args, backends).run()

    return await asyncio.start_server(handle, args.host, args.port, limit=MAX_LINE)


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
