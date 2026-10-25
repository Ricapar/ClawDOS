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
  python bridge/xtbridge.py --replay bridge/xtbridge.log   # play a logged session back

From the XT, "/model" lists providers and "/model openrouter:some/model"
switches the current conversation. providers.py has the built-in providers;
bridge/providers.toml adds your own. API keys come from the environment
(ANTHROPIC_API_KEY or an `ant auth login` profile, OPENROUTER_API_KEY,
ZAI_API_KEY, OPENAI_API_KEY) or a .env file in the repo root.
"""

import argparse
import asyncio
import logging
import logging.handlers
import os

import providers
import replay
from cp437 import from_cp437, to_cp437
from mockchat import Disconnect, MockAPIError, Refused
from providers import APIError, Backends, ConfigError, transcript
from tools import STATUSES, ToolCall, ToolResult, call_lines, unescape

PROTOCOL_VERSION = 1
DEFAULT_PORT = 8023
MAX_LINE = 4096          # longest line accepted from a client
MAX_TXT = 500            # longest payload sent to a client
TRACE_TXT = 120          # TXT/OUT lines are cut to this in the wire trace
DEFAULT_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'xtbridge.log')

log = logging.getLogger('xtbridge')
sessions = set()         # connected clients, for spotting stale ones
connections = 0          # clients ever accepted


def trace(peer, direction, raw):
    """Log one protocol line (bytes, without the newline) at DEBUG."""
    if not log.isEnabledFor(logging.DEBUG):
        return
    text = raw.decode('cp437', 'replace')
    if text[:4] in ('TXT ', 'OUT ') and len(text) > TRACE_TXT:
        text = f'{text[:TRACE_TXT]}... ({len(raw)} bytes)'
    log.debug('%s %s %s', peer, direction, text)


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
        self.tools_sent = 0
        self.tool = None             # (id, future, OUT chunks) of the call in progress

    async def send(self, verb, payload=b''):
        # Keep lines short for the XT's receive buffer; TXT can be split freely.
        while verb == 'TXT' and len(payload) > MAX_TXT:
            trace(self.peer, '>', b'TXT ' + payload[:MAX_TXT])
            self.writer.write(b'TXT ' + payload[:MAX_TXT] + b'\n')
            payload = payload[MAX_TXT:]
        line = verb.encode('ascii') + (b' ' + payload[:MAX_TXT] if payload else b'')
        trace(self.peer, '>', line)
        self.writer.write(line + b'\n')
        await self.writer.drain()

    async def run(self):
        global connections
        connections += 1
        sessions.add(self)
        log.info('%s connected (connection #%d, %d open)', self.peer, connections, len(sessions))
        if len(sessions) > 1:
            log.info('%s other open connections: %s', self.peer,
                     ', '.join(str(s.peer) for s in sessions if s is not self))
        why = 'closed by bridge'
        try:
            await self.send('HELLO', f'XTBRIDGE {PROTOCOL_VERSION}{" AUTH" if self.args.token else ""}'.encode())
            while True:
                try:
                    raw = await self.reader.readuntil(b'\n')
                except asyncio.LimitOverrunError:
                    log.info('%s line over %d bytes', self.peer, MAX_LINE)
                    await self.send('ERR', b'line too long')
                    await self.reader.read(MAX_LINE)
                    continue
                trace(self.peer, '<', raw.rstrip(b'\r\n'))
                verb, _, rest = raw.rstrip(b'\r\n').partition(b' ')
                if not await self.command(verb.decode('ascii', 'replace').upper(), rest):
                    why = 'QUIT'
                    break
        except asyncio.IncompleteReadError as e:
            why = f'closed by client ({len(e.partial)} bytes of a partial line)' if e.partial else 'closed by client'
        except OSError as e:         # resets, and Windows giving up on an XT that stopped ACKing
            why = f'{type(e).__name__}: {e}'
        except Exception:
            why = 'bridge error'
            log.exception('%s session failed', self.peer)
        finally:
            if self.task:
                self.task.cancel()
            if self.tool:
                log.info('%s tool %s was still waiting for the XT', self.peer, self.tool[0])
            self.writer.close()
            sessions.discard(self)
            log.info('%s disconnected: %s (%d open)', self.peer, why, len(sessions))

    async def command(self, verb, rest):
        if verb == 'QUIT':
            await self.send('BYE')
            return False
        if verb == 'PING':
            await self.send('PONG')
            return True
        if verb == 'LOG':            # diagnostics from the XT (CLAUDE -watch); no reply
            log.info('%s XT: %s', self.peer, from_cp437(rest))
            return True
        if verb == 'AUTH':
            self.authed = not self.args.token or from_cp437(rest) == self.args.token
            await self.send('OK' if self.authed else 'ERR', b'' if self.authed else b'bad token')
            return True
        if not self.authed:
            await self.send('ERR', b'AUTH required')
            return True
        if verb == 'OUT':
            if self.tool:
                self.tool[2].append(from_cp437(rest))
            else:
                log.info('%s stray OUT ignored', self.peer)
        elif verb == 'DONE':
            self.tool_done(*(from_cp437(rest).split() + ['', ''])[:2])
        elif verb == 'INFO':
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

    def tool_done(self, id, status):
        if not self.tool or self.tool[0] != id or status not in STATUSES:
            log.info('%s stray DONE %s %s ignored', self.peer, id, status)
            return
        fut = self.tool[1]
        if not fut.done():
            fut.set_result(ToolResult(status, unescape(''.join(self.tool[2]))))
        if status == 'stopped' and self.task:
            self.task.cancel()       # Esc on the XT ends the whole turn

    async def run_tool(self, call):
        """Have the XT run one tool call; returns its ToolResult."""
        self.tools_sent += 1
        call.id = f't{self.tools_sent}'
        self.tool = (call.id, asyncio.get_running_loop().create_future(), [])
        try:
            for verb, payload in call_lines(call.id, call):
                await self.send(verb, to_cp437(payload))
            result = await self.tool[1]
        finally:
            self.tool = None
        log.info('%s tool %s %s -> %s', self.peer, call.id, call.name, result.status)
        log.debug('%s tool %s result: %r', self.peer, call.id, result.text[:400])
        return result

    async def answer(self, text):
        log.info('%s >> %s', self.peer, text)
        line_open = False            # sent text that isn't followed by NL yet
        reply = self.chat.reply(text)
        try:
            async for item in reply:
                if isinstance(item, ToolCall):
                    if line_open:    # the XT starts tool headers on a fresh line
                        await self.send('NL')
                        line_open = False
                    item.result = await self.run_tool(item)
                    continue
                *lines, rest = item.split('\n')
                for line in lines:
                    if line:
                        await self.send('TXT', to_cp437(line))
                    await self.send('NL')
                    line_open = False
                if rest:
                    await self.send('TXT', to_cp437(rest))
                    line_open = True
            await self.send('END')
        except asyncio.CancelledError:
            await self.tell('END', b'stopped')
        except Refused as e:
            await self.tell('ERR', to_cp437(str(e)))
        except Disconnect:
            log.info('%s dropped on purpose (mock)', self.peer)
            self.writer.close()
        except (MockAPIError, APIError) as e:
            log.info('%s API error: %s', self.peer, e)
            await self.tell('ERR', to_cp437(str(e)))
        except OSError as e:         # the link went; run() reports that
            log.info('%s reply abandoned: %s', self.peer, e)
        except Exception as e:  # report API trouble to the XT instead of dropping the link
            log.exception('reply failed')
            await self.tell('ERR', to_cp437(describe_error(e)))
        finally:
            await reply.aclose()

    async def tell(self, verb, payload=b''):
        """send(), for wrapping up a reply: the link may already be gone."""
        try:
            await self.send(verb, payload)
        except OSError:
            pass


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


def load_env(path=os.path.join(os.path.dirname(__file__), '..', '.env')):
    """Read KEY=value lines from a .env file into os.environ. Variables already
    set in the environment win. Returns the names loaded."""
    loaded = []
    try:
        with open(path, encoding='utf-8-sig') as f:
            lines = f.read().splitlines()
    except FileNotFoundError:
        return loaded
    for line in lines:
        line = line.strip()
        if line.startswith('export '):
            line = line[7:].lstrip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        key, value = (part.strip() for part in line.split('=', 1))
        if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
            value = value[1:-1]
        if key and key not in os.environ:
            os.environ[key] = value
            loaded.append(key)
    return loaded


def setup_logging(path, verbose):
    """INFO (or DEBUG with -v) on the console; everything, including the wire
    trace, in a rotating log file unless path is '-'."""
    root = logging.getLogger()
    root.setLevel(logging.DEBUG)
    console = logging.StreamHandler()
    console.setLevel(logging.DEBUG if verbose else logging.INFO)
    console.setFormatter(logging.Formatter('%(asctime)s %(message)s', '%H:%M:%S'))
    root.addHandler(console)
    for noisy in ('httpx', 'httpcore', 'anthropic', 'asyncio'):
        logging.getLogger(noisy).setLevel(logging.INFO)
    if path != '-':
        f = logging.handlers.RotatingFileHandler(path, maxBytes=5_000_000, backupCount=3, encoding='utf-8')
        f.setFormatter(logging.Formatter('%(asctime)s.%(msecs)03d %(levelname).1s %(message)s', '%Y-%m-%d %H:%M:%S'))
        root.addHandler(f)
        log.info('Logging to %s', os.path.abspath(path))


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
    ap.add_argument('--replay', metavar='LOG',
                    help='play back a session from a debug log (see replay.py); any message starts it')
    ap.add_argument('--connection', type=int, metavar='N',
                    help='replay only: the Nth connection in the log (default: the last one with a message)')
    ap.add_argument('--speed', type=float, default=1.0,
                    help='mock and replay: scale delays (2 = twice as fast, 0 = no delays)')
    ap.add_argument('--log', default=DEFAULT_LOG, metavar='FILE',
                    help='full debug log with a wire trace (default: bridge/xtbridge.log; "-" for none)')
    ap.add_argument('-v', '--verbose', action='store_true', help='show the debug log on the console too')
    args = ap.parse_args()
    if args.replay and args.log == DEFAULT_LOG:   # don't append to what may be the replay's source
        args.log = os.path.join(os.path.dirname(DEFAULT_LOG), 'replay.log')
    setup_logging(args.log, args.verbose)
    loaded = load_env()
    if loaded:
        log.info('Loaded %s from .env', ', '.join(loaded))
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
    if getattr(args, 'replay', None):
        try:
            turns = replay.parse_log(args.replay, getattr(args, 'connection', None))
        except (OSError, ValueError, IndexError) as e:
            raise ConfigError(f'replay: {e}')
        table['replay'] = {'type': 'replay', 'name': 'Claude', 'model': os.path.basename(args.replay),
                           'turns': turns}
        args.provider = 'replay'
        log.info('Replaying %d turn(s) from %s', len(turns), args.replay)
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
