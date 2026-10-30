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
import datetime
import itertools
import logging
import logging.handlers
import os
import secrets
import socket
import time

import convlog
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
DEFAULT_CONVLOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs')

log = logging.getLogger('xtbridge')
sessions = set()         # connected clients, for spotting stale ones
CONNECTIONS = itertools.count(1)   # numbers the clients as they are accepted
PARK_SECONDS = 30 * 60   # how long a dropped XT's conversation waits for RESUME
# The longest a tool call may take before the XT is presumed dead. The XT can't
# talk while a program started by `run` executes (a compile can take minutes),
# and a hung or power-cycled XT sends no FIN, so without this the call would
# wait forever and the conversation would never be parked.
TOOL_TIMEOUT = 20 * 60


class Conversation:
    """What a reconnecting XT gets back with RESUME <token>: its chat (history,
    backend, model) and memory index. Held by a Session while connected, parked
    for PARK_SECONDS after its connection drops."""

    def __init__(self):
        self.token = secrets.token_hex(4)
        while self.token in conversations:
            self.token = secrets.token_hex(4)
        self.chat = None
        self.memories = None
        self.session = None      # the Session using it; None while parked
        self.parked_at = None
        conversations[self.token] = self

    def expired(self, now):
        """True if parked for longer than PARK_SECONDS as of time.monotonic() `now`."""
        return self.session is None and now - self.parked_at >= PARK_SECONDS

    def park(self, chat, memories):
        """Keep the chat and memories for a RESUME; no session is using them now."""
        self.chat, self.memories = chat, memories
        self.session, self.parked_at = None, time.monotonic()


conversations = {}       # token -> Conversation


def purge_parked():
    """Forget the conversations that have waited longer than PARK_SECONDS."""
    now = time.monotonic()
    for token, conv in list(conversations.items()):
        if conv.expired(now):
            del conversations[token]


def trace(peer, direction, raw):
    """Log one protocol line (bytes, without the newline) at DEBUG."""
    if not log.isEnabledFor(logging.DEBUG):
        return
    text = raw.decode('cp437', 'replace')
    if text[:4] in ('TXT ', 'OUT ') and len(text) > TRACE_TXT:
        text = f'{text[:TRACE_TXT]}... ({len(raw)} bytes)'
    log.debug('%s %s %s', peer, direction, text)


# --- protocol ----------------------------------------------------------------

class Session:   # pylint: disable=too-many-instance-attributes  # one connection's state
    """One XT connection: reads its commands and streams the replies back."""

    def __init__(self, reader, writer, args, backends):
        self.reader = reader
        self.writer = writer
        self.args = args
        self.backends = backends
        self.chat = self.attach(backends.make(args.provider, args.model))
        self.task = None
        self.clog = convlog.NoLog()  # the conversation log, opened once numbered
        self.turn = None             # usage and text of the reply in progress
        self.memories = None         # the XT's memory index for this conversation, once listed
        self.authed = not args.token
        self.peer = writer.get_extra_info('peername')
        # asyncio turns Nagle off, so each streamed TXT fragment would be its own
        # tiny packet: a burst of them overflows the XT's few receive buffers,
        # and the drops stall the link for tens of seconds while Windows backs
        # off retransmitting. With Nagle on they are coalesced into full packets.
        sock = writer.get_extra_info('socket')
        if sock is not None:
            try:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 0)
            except OSError:
                pass
        self.tools_sent = 0
        self.tool = None             # (id, future, OUT chunks) of the call in progress
        self.xt_silent = False       # a tool call timed out: hang up once the turn is settled
        purge_parked()
        self.conv = Conversation()
        self.conv.session = self

    async def send(self, verb, payload=b''):
        """Send one protocol line."""
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
        """Serve the connection until it ends."""
        number = next(CONNECTIONS)
        sessions.add(self)
        log.info('%s connected (connection #%d, %d open)', self.peer, number, len(sessions))
        self.clog = convlog.open_log(getattr(self.args, 'convlog', None), self.peer, number)
        self.clog.event('session_start', peer=self.peer_text(), connection=number,
                        protocol=PROTOCOL_VERSION, conversation=self.conv.token, **self.chat_fields())
        if len(sessions) > 1:
            log.info('%s other open connections: %s', self.peer,
                     ', '.join(str(s.peer) for s in sessions if s is not self))
        why = 'closed by bridge'
        try:
            auth = ' AUTH' if self.args.token else ''
            now = datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')   # the XT sets its clock
            await self.send('HELLO', f'XTBRIDGE {PROTOCOL_VERSION}{auth} SESSION {self.conv.token} '
                                     f'TIME {now}'.encode())
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
        except Exception:  # pylint: disable=broad-exception-caught  # last resort: log it, don't kill the server
            why = 'bridge error'
            log.exception('%s session failed', self.peer)
        finally:
            if self.tool:
                log.info('%s tool %s was still waiting for the XT', self.peer, self.tool[0])
            if self.task and not self.task.done():
                self.task.cancel()
                await asyncio.wait([self.task], timeout=2)   # so its turn_end is logged first
            self.writer.close()
            sessions.discard(self)
            if self.conv.session is self:      # not taken over: keep it for a RESUME
                self.conv.park(self.chat, self.memories)
            log.info('%s disconnected: %s (%d open)', self.peer, why, len(sessions))
            self.clog.event('session_end', reason=why)
            self.clog.close()

    async def resume(self, token):
        """RESUME <token>: carry on the conversation of an earlier connection,
        parked or (if the bridge never noticed it drop) still attached."""
        purge_parked()
        conv = conversations.get(token)
        if conv is None or conv is self.conv:
            await self.send('ERR', b'unknown session')
            return
        old = conv.session
        parked_s = None
        if old is not None:              # a stale connection: take the conversation over
            log.info('%s takes over conversation %s from %s', self.peer, token, old.peer)
            conv.chat, conv.memories = old.chat, old.memories
            conv.session = None
            if old.task and not old.task.done():
                old.task.cancel()
            old.writer.close()
        else:
            parked_s = int(time.monotonic() - conv.parked_at)
        del conversations[self.conv.token]
        self.conv, conv.session = conv, self
        self.memories = conv.memories
        self.chat = self.attach(conv.chat)
        log.info('%s resumed conversation %s', self.peer, token)
        self.clog.event('session_resume', conversation=token, parked_s=parked_s,
                        took_over=old.peer_text() if old else None)
        await self.send('OK', b'resumed')

    def peer_text(self):
        """The client's address as host:port."""
        return f'{self.peer[0]}:{self.peer[1]}' if self.peer else 'unknown'

    def chat_fields(self):
        """Which backend, model and system prompt the conversation is on."""
        return {'provider': self.chat.provider, 'model': self.chat.model, 'prompt': self.backends.prompt,
                'effort': getattr(self.chat, 'effort', None), 'system': getattr(self.chat, 'system', None)}

    def attach(self, chat):
        """Hook `chat` up to this session (usage logging, memories); returns it."""
        chat.on_api_call = self.api_called
        providers.set_memories(chat, getattr(self, 'memories', None))
        return chat

    async def list_memories(self):
        """Once per conversation, on a backend with a system prompt: list the XT's
        memories (an ordinary tool call) and add them to the prompt."""
        if self.memories is not None or not providers.uses_system_prompt(self.chat):
            return
        result = await self.run_tool(ToolCall('memory_list', {}))
        if result.status != 'ok':
            log.info('%s no memory list: %s %s', self.peer, result.status, result.text[:80])
        self.memories = result.text if result.status == 'ok' else ''
        providers.set_memories(self.chat, self.memories)
        self.clog.event('system_prompt', text=self.chat.system)

    def api_called(self, info):
        """A backend finished one API round (see providers.report)."""
        self.flush_text()
        self.clog.event('api_call', **info)
        if self.turn:
            self.turn['rounds'] += 1
            self.turn['stop'] = info.get('stop_reason')
            convlog.add_usage(self.turn['usage'], info.get('usage') or {})

    def flush_text(self):
        """Log the reply text streamed since the last flush as one event."""
        if self.turn and self.turn['text']:
            self.clog.event('assistant', text=''.join(self.turn['text']))
            self.turn['text'] = []

    async def command(self, verb, rest):
        """Handle one line from the client; False once it has said QUIT."""
        if verb == 'QUIT':
            await self.send('BYE')
            return False
        if verb == 'PING':
            await self.send('PONG')
        elif verb == 'LOG':          # diagnostics from the XT (link loss, clock); no reply
            log.info('%s XT: %s', self.peer, from_cp437(rest))
        elif verb == 'AUTH':
            self.authed = not self.args.token or from_cp437(rest) == self.args.token
            await self.send('OK' if self.authed else 'ERR', b'' if self.authed else b'bad token')
        elif not self.authed:
            await self.send('ERR', b'AUTH required')
        elif verb == 'RESUME':
            await self.resume(from_cp437(rest).strip())
        else:
            await self.chat_command(verb, rest)
        return True

    async def chat_command(self, verb, rest):
        """The commands of an authenticated client that act on the chat."""
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
            self.memories = None
            self.chat = self.attach(self.backends.make(self.chat.provider, self.chat.model))
            self.clog.event('new_chat')
            await self.send('OK')
        elif verb == 'STOP':
            if self.task:
                self.task.cancel()
        elif verb == 'MSG':
            await self.message(from_cp437(rest).strip())
        else:
            await self.send('ERR', b'unknown command')

    async def message(self, text):
        """MSG <text>: start answering it, or run a /model command."""
        if self.task and not self.task.done():
            await self.send('ERR', b'busy')
        elif not text:
            await self.send('ERR', b'empty message')
        elif text.split()[0].lower() in ('/model', '/provider'):
            await self.say(self.switch(text.split(None, 1)[1:]))
        else:
            self.task = asyncio.create_task(self.answer(text))

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
        before = self.backends.describe(self.chat)
        self.chat = self.attach(self.backends.make(key, model or None, history))
        log.info('%s switched to %s', self.peer, self.backends.describe(self.chat))
        self.clog.event('model_switch', previous=before, kept=len(history) // 2, **self.chat_fields())
        kept = f', keeping {len(history) // 2} earlier exchange(s)' if history else ''
        return f'Switched to {self.backends.describe(self.chat)}{kept}.'

    def tool_done(self, tool_id, status):
        """DONE <id> <status>: the XT finished the tool call in progress."""
        if not self.tool or self.tool[0] != tool_id or status not in STATUSES:
            log.info('%s stray DONE %s %s ignored', self.peer, tool_id, status)
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
        silent = ToolResult('error', f'the XT did not answer within {TOOL_TIMEOUT // 60} minutes')
        if self.xt_silent:           # an earlier call this turn already gave up on it
            return silent
        self.tool = (call.id, asyncio.get_running_loop().create_future(), [])
        self.clog.event('tool_call', id=call.id, name=call.name, args=call.args)
        started = time.monotonic()
        try:
            for verb, payload in call_lines(call.id, call):
                await self.send(verb, to_cp437(payload))
            try:
                result = await asyncio.wait_for(asyncio.shield(self.tool[1]), TOOL_TIMEOUT)
            except asyncio.TimeoutError:
                log.info('%s tool %s %s: no answer in %d s, presuming the XT dead',
                         self.peer, call.id, call.name, TOOL_TIMEOUT)
                self.xt_silent = True
                result = silent
        finally:
            self.tool = None
        self.clog.event('tool_result', id=call.id, status=result.status, text=result.text,
                        ms=int((time.monotonic() - started) * 1000))
        log.info('%s tool %s %s -> %s', self.peer, call.id, call.name, result.status)
        log.debug('%s tool %s result: %r', self.peer, call.id, result.text[:400])
        return result

    async def relay(self, reply):
        """Stream the model's `reply` to the XT, running its tool calls; returns
        the outcome ('ok', or 'cut_off' if the model ran out of tokens)."""
        await self.list_memories()
        line_open = False            # sent text that isn't followed by NL yet
        async for item in reply:
            if isinstance(item, ToolCall):
                self.flush_text()
                if line_open:    # the XT starts tool headers on a fresh line
                    await self.send('NL')
                    line_open = False
                item.result = await self.run_tool(item)
                continue
            self.turn['text'].append(item)
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
        return 'cut_off' if self.turn['stop'] in ('max_tokens', 'length') else 'ok'

    async def answer(self, text):
        """Answer the user's `text` with the chat backend, logging the turn."""
        log.info('%s >> %s', self.peer, text)
        self.clog.event('user', text=text)
        self.turn = {'started': time.monotonic(), 'usage': convlog.no_usage(), 'rounds': 0,
                     'text': [], 'stop': None}
        outcome, detail = 'ok', None
        reply = self.chat.reply(text)
        try:
            outcome = await self.relay(reply)
        except asyncio.CancelledError:
            outcome = 'stopped'
            await self.tell('END', b'stopped')
        except Refused as e:
            outcome, detail = 'refused', str(e)
            await self.tell('ERR', to_cp437(str(e)))
        except Disconnect:
            outcome = 'dropped'
            log.info('%s dropped on purpose (mock)', self.peer)
            self.writer.close()
        except (MockAPIError, APIError) as e:
            outcome, detail = 'error', str(e)
            log.info('%s API error: %s', self.peer, e)
            await self.tell('ERR', to_cp437(str(e)))
        except OSError as e:         # the link went; run() reports that
            outcome, detail = 'link_lost', str(e)
            log.info('%s reply abandoned: %s', self.peer, e)
        except Exception as e:  # pylint: disable=broad-exception-caught  # report API trouble to the XT, don't drop the link
            outcome, detail = 'error', describe_error(e)
            log.exception('reply failed')
            await self.tell('ERR', to_cp437(describe_error(e)))
        finally:
            self.flush_text()
            turn, self.turn = self.turn, None
            self.clog.event('turn_end', outcome=outcome, detail=detail, rounds=turn['rounds'],
                            ms=int((time.monotonic() - turn['started']) * 1000), usage=turn['usage'])
            await reply.aclose()
            if self.xt_silent:       # the turn is in the history; park it for a RESUME
                log.info('%s hanging up on the silent XT', self.peer)
                self.writer.close()

    async def tell(self, verb, payload=b''):
        """send(), for wrapping up a reply: the link may already be gone."""
        try:
            await self.send(verb, payload)
        except OSError:
            pass


def httpx_message(e):
    """The message for an httpx transport failure, or None if `e` is not one."""
    try:
        import httpx  # pylint: disable=import-outside-toplevel  # optional: only providers using it need it
    except ImportError:
        return None
    if isinstance(e, httpx.TimeoutException):
        return 'The model API timed out.'
    if isinstance(e, httpx.TransportError):
        return 'Bridge could not reach the model API.'
    return None


def anthropic_message(e):
    """The message for an Anthropic SDK error, or None if `e` is not one."""
    try:
        import anthropic  # pylint: disable=import-outside-toplevel  # optional, like httpx
    except ImportError:
        return None
    if isinstance(e, anthropic.AuthenticationError):
        return 'Bridge has no valid Anthropic API key.'
    if isinstance(e, anthropic.RateLimitError):
        return 'Rate limited by the API; try again shortly.'
    if isinstance(e, anthropic.APIStatusError):
        return f'API error {e.status_code}: {e.message}'[:200]
    if isinstance(e, anthropic.APIConnectionError):
        return 'Bridge could not reach the Claude API.'
    return None


def describe_error(e):
    """A short message for the XT's screen about an unexpected exception."""
    return httpx_message(e) or anthropic_message(e) or f'{type(e).__name__}: {e}'[:200]


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
    """Parse the command line and serve until interrupted."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', default='0.0.0.0')
    ap.add_argument('--port', type=int, default=DEFAULT_PORT)
    ap.add_argument('--provider', default='anthropic',
                    help='backend new conversations start on (see providers.py)')
    ap.add_argument('--model', help="model to use instead of the provider's default")
    ap.add_argument('--prompt', default='default', metavar='NAME',
                    help='system prompt file bridge/prompts/NAME.txt, on top of the built-in one '
                         '(re-read for each conversation)')
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
    ap.add_argument('--convlog', default=DEFAULT_CONVLOG, metavar='DIR',
                    help='conversation log, one JSONL file per connection (default: bridge/logs; "-" for none)')
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
        raise SystemExit(f'xtbridge: {e}') from e
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
            raise ConfigError(f'replay: {e}') from e
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
    backends = Backends(table, getattr(args, 'speed', 1.0), getattr(args, 'prompt', None) or 'default',
                        getattr(args, 'prompt_dir', None))
    args.backend_name = backends.describe(backends.make(args.provider, args.model))

    async def handle(reader, writer):
        await Session(reader, writer, args, backends).run()

    return await asyncio.start_server(handle, args.host, args.port, limit=MAX_LINE)


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
