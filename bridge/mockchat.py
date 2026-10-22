"""Mock Claude backend for XTBridge: no API key, no network, no cost.

It behaves like the real backend as far as the XT can tell: a pause before the
first word, then text streamed in token-sized pieces, conversation memory, and
the same failure modes (API errors, refusals, cut-off replies, dropped links).

Ordinary messages get a canned conversational reply. Messages starting with
`!` trigger test scenarios; send `!help` for the list.
"""

import asyncio
import logging
import random
import re
import shlex

from tools import ToolCall

log = logging.getLogger('mockchat')


# Raised by backends' reply() generators; xtbridge.py handles them. They live
# here, not in xtbridge.py, so they are the same classes when xtbridge.py
# runs as __main__.

class Refused(Exception):
    """Claude declined to answer (stop_reason "refusal")."""


class Disconnect(Exception):
    """Drop the client's connection mid-reply."""


class MockAPIError(Exception):
    """Stands in for an Anthropic API error."""


SCENARIOS = {
    '!help': 'list these test commands',
    '!scroll': '60 numbered lines, for scrolling and PgUp/PgDn scrollback',
    '!long': 'several paragraphs of prose, for word wrap',
    '!wide': 'a 120-character word and a 1500-byte line (hard wrap, TXT splitting)',
    '!cp437': 'accented letters, symbols, box drawing, and Unicode the bridge must approximate',
    '!markdown': 'Markdown the real model is told not to send, to see how it looks',
    '!slow': 'a reply that trickles out one word per second (try Esc)',
    '!stall': 'thinks for 30 seconds before answering (spinner, Esc)',
    '!error': 'an API error after a few words',
    '!refuse': 'a refusal (the message is dropped from history)',
    '!cutoff': 'a reply that hits max_tokens',
    '!drop': 'the bridge hangs up mid-reply',
    '!history': 'what the mock remembers of this conversation',
    '!tool': 'call one XT tool: !tool read path=C:\\AUTOEXEC.BAT ("k=v w" quotes spaces)',
    '!tooltour': 'call every XT tool in turn and check the results',
    '!draw': 'draw a sunset with the draw tool',
    '!play': 'play a tune on the PC speaker',
}

DRAW_DEMO = """\
SCREEN 1
COLOR 1,0
' sky is the blue background; palette 0 is green, red, brown
' the sun first, then the ground covers its lower half
CIRCLE (160,150),45,2
PAINT (160,140),2,2
LINE (0,151)-(319,199),1,BF
LINE (0,150)-(319,150),3
' sun rays
LINE (160,95)-(160,80),2
LINE (125,110)-(112,98),2
LINE (195,110)-(208,98),2
LINE (110,135)-(92,130),2
LINE (210,135)-(228,130),2
' a house
LINE (30,120)-(80,150),3,BF
DRAW "C3 BM30,120 E25 F25"
PAINT (55,110),3,3
LINE (48,132)-(60,150),0,BF
' trees
CIRCLE (260,125),14,1
PAINT (260,125),1,1
LINE (260,139)-(260,150),3
CIRCLE (290,130),10,1
PAINT (290,130),1,1
LINE (290,140)-(290,150),3
' birds
DRAW "C3 BM100,60 E4 F4 E4 F4"
DRAW "C3 BM200,45 E3 F3 E3 F3"
LOCATE 2,12
PRINT "SUNSET ON THE XT"
"""

PLAY_DEMO = 'T140 O3 L4 E E F G G F E D C C D E E. D8 D2 E E F G G F E D C C D E D. C8 C2'

# The tool tour: (tool, args, expected status, text the result must contain).
# Tools CLAUDE.EXE doesn't have yet are expected to fail; flip them as they land.
TOUR_EXPECT = {'run': 'error'}
TOUR_DRAW = 'SCREEN 1\n' + '\n'.join(f'LINE ({i * 2},0)-({319 - i * 2},199),{1 + i % 3}'
                                     for i in range(120)) + '\nLOCATE 12,15\nPRINT "TOUR"\n'
TOUR_TEXT = 'Hello from the tool tour.\nSecond line.'


def tour_steps():
    steps = [
        ('sysinfo', {}, 'ok', 'DOS'),
        ('dir', {'path': 'C:\\'}, 'ok', 'AUTOEXEC.BAT'),
        ('write', {'path': 'C:\\CXTTOUR.TXT', 'text': TOUR_TEXT}, 'ok', None),
        ('read', {'path': 'c:\\cxttour.txt'}, 'ok', 'Hello from the tool tour.'),
        ('rename', {'from': 'C:\\CXTTOUR.TXT', 'to': 'C:\\CXTTOUR2.TXT'}, 'ok', None),
        ('mkdir', {'path': 'C:\\CXTTDIR'}, 'ok', None),
        ('delete', {'path': 'C:\\CXTTOUR2.TXT'}, 'ok', None),
        ('delete', {'path': 'C:\\*.BAT'}, 'error', None),
        ('peek', {'addr': '0040:0000', 'len': '16'}, 'ok', None),
        ('poke', {'addr': '0000:04F0', 'bytes': '43 58 54'}, 'ok', None),
        ('peek', {'addr': '0000:04F0', 'len': '3'}, 'ok', '43 58 54'),
        ('port', {'port': '61'}, 'ok', None),
        ('textart', {'text': '~EClaude~C-XT'}, 'ok', None),
        ('read', {'path': 'C:\\NOPE.TXT'}, 'error', 'not found'),
        ('draw', {'script': TOUR_DRAW}, 'ok', None),
        ('play', {'music': 'T200 L16 O4 CEG>C'}, 'ok', None),
        ('run', {'command': 'VER'}, 'ok', 'FreeDOS'),
    ]
    out = []
    for name, args, status, want in steps:
        if TOUR_EXPECT.get(name) == 'error':
            status, want = 'error', None
        out.append((name, args, status, want))
    return out

LOREM = (
    "The IBM Personal Computer XT, model 5160, arrived in March 1983. It kept "
    "the original PC's Intel 8088 at 4.77 MHz but added a 10 MB hard disk, "
    "eight expansion slots, and room for 640 KB of memory on later "
    "motherboards. Text on a CGA card lives at segment B800, two bytes per "
    "character: one for the code page 437 glyph and one for its colours.\n\n"
    "Writing to that memory while the card is drawing the screen causes the "
    "famous CGA snow, which is why careful programs wait for the horizontal or "
    "vertical retrace before touching it. Scrolling by copying 4000 bytes is "
    "slow on an 8088, so Claude-XT moves the CRTC start address instead and lets "
    "the 16 KB of video memory act as a ring of scrollback.\n\n"
    "Networking was never part of the original design, but a packet driver, "
    "mTCP, and a PicoMEM card on WiFi make it a small TCP/IP host. The heavy "
    "lifting of TLS happens on the bridge, which turns a modern HTTPS API "
    "into a line protocol simple enough for a 40-year-old machine."
)

CP437_SAMPLE = (
    "Accents: café, naïve, Ångström, señor, Zürich, façade\n"
    "Symbols: ½ ¼ ° ± ² · ¿ ¡ « » £ ¥ ƒ α ß π Σ µ τ Φ Ω δ ∞ √ ≡ ≈ ≥ ≤ ÷\n"
    "Boxes:   ┌──┬──┐ ╔══╦══╗ ░▒▓█ ▀▄▌▐\n"
    "         └──┴──┘ ╚══╩══╝\n"
    "Approximated by the bridge: “curly quotes” ‘single’ – en — em … "
    "→ ← • ✓ ✗ © ® ™ € ≠\n"
    "Become ?: emoji 🙂 and the CP437 glyphs for control codes ♥ ♦ ☺ ♪ ¶ §\n"
)

MARKDOWN_SAMPLE = (
    "# A heading\n\n"
    "Some **bold** and *italic* text, plus `inline code`.\n\n"
    "| Column | Other |\n|---|---|\n| a | b |\n\n"
    "```c\nint main(void) { return 0; }\n```\n"
)

# Plain conversational replies, picked by the first pattern that matches.
SMALL_TALK = [
    (r'\b(hi|hello|hey|howdy|greetings)\b',
     "Hello from the mock bridge! I'm a stand-in for Claude, so no API calls "
     "are being made. Everything about the connection is real though: your "
     "XT sent that message over TCP, and this reply is streaming back a few "
     "characters at a time. Send !help to see the test commands."),
    (r'\b(who|what) are you\b',
     "I'm the mock backend of XTBridge. I imitate Claude closely enough "
     "to test CLAUDE.EXE: replies stream in token-sized pieces after a short "
     "pause, I remember the conversation, and I can fail in all the ways "
     "the real API can. Start the bridge without --mock to talk to Claude."),
    (r'\b(xt|8088|ibm|cga|pc)\b',
     "Your machine is a classic: an Intel 8088 at 4.77 MHz with 640 KB of "
     "memory and a CGA card. That's about 0.33 MIPS, so every byte drawn on "
     "screen counts. Even so, it's keeping up with this stream just fine."),
    (r'\bjoke\b',
     "Why did the 8088 refuse to go to the party? It heard there would be "
     "32-bit registers and didn't want to feel segmented."),
    (r'\b(thanks|thank you|thx)\b',
     "You're welcome! This has been a mock conversation, but the real one "
     "will look exactly the same on your screen."),
    (r'\b(bye|goodbye|see you)\b',
     "Goodbye! Press F10 to leave CLAUDE.EXE."),
]


class MockChat:
    """One mock conversation. `speed` scales every delay; 0 means no waiting."""

    def __init__(self, speed=1.0, seed=None):
        self.speed = speed
        self.rng = random.Random(seed)
        self.history = []   # (user text, reply text) pairs

    async def pause(self, seconds):
        if self.speed > 0:
            await asyncio.sleep(seconds / self.speed)

    async def stream(self, text, per_token=0.035):
        """Yield text in roughly token-sized pieces (1-6 characters)."""
        i = 0
        while i < len(text):
            n = self.rng.randint(1, 6)
            await self.pause(per_token * self.rng.uniform(0.5, 1.5))
            yield text[i:i + n]
            i += n

    async def reply(self, text):
        await self.pause(self.rng.uniform(0.4, 1.2))   # time to first token
        command = text.split()[0].lower() if text.startswith('!') else None
        parts = []
        async for chunk in self.respond(command, text):
            if isinstance(chunk, str):
                parts.append(chunk)
            yield chunk
        # Only completed replies are remembered, as with the real backend.
        self.history.append((text, ''.join(parts)))

    async def respond(self, command, text):
        if command is None:
            async for c in self.stream(self.small_talk(text)):
                yield c
        elif command == '!help':
            lines = ['Mock bridge test commands:\n']
            lines += [f'  {k:<10} {v}\n' for k, v in SCENARIOS.items()]
            lines.append('Anything else gets a canned conversational reply.')
            async for c in self.stream(''.join(lines), per_token=0.01):
                yield c
        elif command == '!scroll':
            for i in range(1, 61):
                await self.pause(0.02)
                yield f'Line {i} of 60: the quick brown fox jumps over the lazy dog.\n'
        elif command == '!long':
            async for c in self.stream(LOREM):
                yield c
        elif command == '!wide':
            yield 'A 120-character word: ' + ''.join(chr(65 + i % 26) for i in range(120)) + '\n'
            yield 'A 1500-byte line: ' + ' '.join(f'word{i:03}' for i in range(170)) + '\n'
            yield 'Done.'
        elif command == '!cp437':
            async for c in self.stream(CP437_SAMPLE, per_token=0.01):
                yield c
        elif command == '!markdown':
            async for c in self.stream(MARKDOWN_SAMPLE, per_token=0.01):
                yield c
        elif command == '!slow':
            for w in 'This reply trickles out one word per second so you can press Esc to stop it.'.split():
                await self.pause(1.0)
                yield w + ' '
        elif command == '!stall':
            await self.pause(30)
            yield 'Sorry for the wait. That was a 30-second stall.'
        elif command == '!error':
            async for c in self.stream('Starting a reply, and then '):
                yield c
            raise MockAPIError('API error 529: Overloaded (simulated)')
        elif command == '!refuse':
            raise Refused('Claude declined to answer that. (simulated)')
        elif command == '!cutoff':
            async for c in self.stream(LOREM.split('\n\n')[0]):
                yield c
            yield '\n[reply cut off: too long]'
        elif command == '!drop':
            async for c in self.stream('This sentence will never be fin'):
                yield c
            raise Disconnect()
        elif command == '!history':
            async for c in self.stream(self.describe_history()):
                yield c
        elif command == '!tool':
            words = shlex.split(text, posix=False)[1:]
            if not words:
                yield 'Usage: !tool NAME key=value ...'
                return
            args = {}
            for w in words[1:]:
                key, _, value = w.strip('"').partition('=')
                args[key] = value.strip('"')
            async for c in self.call(words[0], args):
                yield c
        elif command == '!draw':
            async for c in self.call('draw', {'script': DRAW_DEMO}):
                yield c
        elif command == '!play':
            async for c in self.call('play', {'music': PLAY_DEMO}):
                yield c
        elif command == '!tooltour':
            async for c in self.tooltour():
                yield c
        else:
            async for c in self.stream(f'Unknown test command {command}. Send !help for the list.'):
                yield c

    async def call(self, name, args):
        """Announce a tool call (without ending the line), make it, report it."""
        async for c in self.stream(f'Calling {name}.'):
            yield c
        call = ToolCall(name, args)
        yield call
        result = call.result
        async for c in self.stream(f'{name} -> {result.status}: {result.text[:300]}', per_token=0.005):
            yield c

    async def tooltour(self):
        yield 'Touring the XT tools.\n'
        failure = None
        for i, (name, args, status, want) in enumerate(tour_steps(), 1):
            call = ToolCall(name, args)
            yield call
            got = call.result
            ok = got.status == status and (want is None or want in got.text)
            yield f'{i:2}. {name}: {got.status}{"" if ok else "  <- expected " + status}\n'
            if not ok and failure is None:
                failure = f'step {i} {name}: got {got.status} {got.text[:60]!r}'
        verdict = 'tool tour: PASS' if failure is None else f'tool tour: FAIL {failure}'
        log.info(verdict)
        yield verdict

    def small_talk(self, text):
        lowered = text.lower()
        for pattern, answer in SMALL_TALK:
            if re.search(pattern, lowered):
                return answer
        words = len(text.split())
        reply = (f"(Mock reply #{len(self.history) + 1}.) You wrote {words} "
                 f"word{'s' if words != 1 else ''}: “{text}”. ")
        if self.history:
            reply += f"Before that you said “{self.history[-1][0]}”, so the bridge is keeping our history. "
        reply += self.rng.choice([
            "On the real bridge, Claude would answer this properly.",
            "That's an interesting thought; the real Claude would have more to say about it.",
            "I'm only a mock, but your message arrived intact.",
        ])
        return reply

    def describe_history(self):
        if not self.history:
            return 'Nothing yet: this is the first message of the conversation.'
        out = [f'I remember {len(self.history)} earlier message(s):\n']
        out += [f'  {i}. {t[:60]}\n' for i, (t, _) in enumerate(self.history, 1)]
        out.append('F2 starts a new conversation and clears this.')
        return ''.join(out)
