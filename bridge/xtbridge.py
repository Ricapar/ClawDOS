#!/usr/bin/env python3
"""XTBridge: lets a DOS machine chat with Claude over a plain TCP connection.

The XT can't do modern TLS, so this runs on a nearby modern machine (a PC, or a
Raspberry Pi tucked inside the XT's case). It speaks a tiny line protocol in
code page 437 to the XT (see docs/PROTOCOL.md) and HTTPS to the Claude API.

  python bridge/xtbridge.py                    # Claude, port 8023
  python bridge/xtbridge.py --echo             # offline test backend
  python bridge/xtbridge.py --token SECRET     # require AUTH from clients

Credentials come from the environment (ANTHROPIC_API_KEY, or an `ant auth
login` profile).
"""

import argparse
import asyncio
import logging
import unicodedata

PROTOCOL_VERSION = 1
DEFAULT_PORT = 8023
DEFAULT_MODEL = 'claude-opus-5-5'
MAX_TOKENS = 16000
MAX_LINE = 4096          # longest line accepted from a client
MAX_TXT = 500            # longest payload sent to a client
FALLBACK_BETA = 'server-side-fallback-2026-07-01'

SYSTEM_PROMPT = """\
You are Claude, chatting with someone through Claude-XT on a 1983 IBM PC XT \
(Intel 8088 at 4.77 MHz, 640 KB RAM, CGA display), connected to you over a \
network bridge. Your replies appear on an 80-column text-mode screen using \
code page 437, so write plain text: no Markdown syntax such as **bold**, # \
headings, tables, or code fences, because it would show up literally. Simple \
lists with "-" are fine. Keep replies conversational and fairly short; the \
screen is small and text draws slowly. Latency-sensitive; begin your visible \
answer immediately."""

log = logging.getLogger('xtbridge')


# --- text conversion ---------------------------------------------------------

# Characters CP437 lacks, mapped to close equivalents.
CP437_FALLBACKS = {
    '‘': "'", '’': "'", '‚': "'", '‛': "'",
    '“': '"', '”': '"', '„': '"', '″': '"', '′': "'",
    '–': '-', '—': '--', '―': '--', '−': '-', '‐': '-', '‑': '-',
    '…': '...', ' ': ' ', ' ': ' ', ' ': ' ', ' ': ' ',
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


# --- backends ------------------------------------------------------------------

class Refused(Exception):
    pass


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


class ClaudeChat:
    """One conversation with Claude. History is append-only."""

    def __init__(self, client, model, effort):
        self.client = client
        self.model = model
        self.effort = effort
        self.messages = []

    async def reply(self, text):
        self.messages.append({'role': 'user', 'content': text})
        try:
            async with self.client.beta.messages.stream(
                model=self.model,
                max_tokens=MAX_TOKENS,
                system=SYSTEM_PROMPT,
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


class EchoChat:
    """Offline stand-in for testing the XT side without an API key."""

    def __init__(self):
        self.turns = 0

    async def reply(self, text):
        self.turns += 1
        if 'scroll' in text.lower():
            # Long output, for exercising scrolling and scrollback on the XT.
            for i in range(1, 61):
                await asyncio.sleep(0.02)
                yield f'Line {i} of 60: the quick brown fox jumps over the lazy dog.\n'
            return
        words = (f'Echo #{self.turns} — you said “{text}”.\n'
                 'This is the offline test backend: no Claude API calls are made. '
                 'Here is some longer text so you can see word wrapping on an '
                 '80-column screen, plus a few CP437 characters: café, '
                 '½, °C, • bullet, and an arrow → done.').split(' ')
        for i, w in enumerate(words):
            await asyncio.sleep(0.03)
            yield w if i == 0 else ' ' + w


# --- protocol ----------------------------------------------------------------

class Session:
    def __init__(self, reader, writer, args, make_chat):
        self.reader = reader
        self.writer = writer
        self.args = args
        self.make_chat = make_chat
        self.chat = make_chat()
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
            await self.send('INFO', to_cp437(self.args.backend_name))
        elif verb == 'NEW':
            if self.task:
                self.task.cancel()
            self.chat = self.make_chat()
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
            else:
                self.task = asyncio.create_task(self.answer(text))
        else:
            await self.send('ERR', b'unknown command')
        return True

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
        except Exception as e:  # report API trouble to the XT instead of dropping the link
            log.exception('reply failed')
            await self.send('ERR', to_cp437(describe_error(e)))


def describe_error(e):
    try:
        import anthropic
    except ImportError:
        return str(e)
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
    ap.add_argument('--model', default=DEFAULT_MODEL)
    ap.add_argument('--effort', default='low', choices=['low', 'medium', 'high', 'xhigh', 'max'])
    ap.add_argument('--token', help='shared secret clients must send with AUTH')
    ap.add_argument('--echo', action='store_true', help='offline echo backend (no API calls)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format='%(asctime)s %(message)s', datefmt='%H:%M:%S')

    if args.echo:
        args.backend_name = 'echo (offline test)'
        make_chat = EchoChat
    else:
        import anthropic
        client = anthropic.AsyncAnthropic()
        args.backend_name = f'{args.model} (effort {args.effort})'
        make_chat = lambda: ClaudeChat(client, args.model, args.effort)

    async def handle(reader, writer):
        await Session(reader, writer, args, make_chat).run()

    server = await asyncio.start_server(handle, args.host, args.port, limit=MAX_LINE)
    log.info('XTBridge on %s:%d, backend: %s', args.host, args.port, args.backend_name)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
