"""Mock Claude for the mock Messages API server (mockapi.py): no API key, no
network, no cost. It behaves like the real model as far as the DOS PC can tell:
a pause before the first word, then text streamed in token-sized pieces, and the
same failure modes (API errors, refusals, cut-off replies, dropped links).

Ordinary messages get a canned conversational reply. Messages starting with
`!` trigger test scenarios; send `!help` for the list.
"""

import random
import re
import shlex
import time
from dataclasses import dataclass

STATUSES = ('ok', 'error', 'denied', 'stopped')


@dataclass
class ToolResult:
    """What the DOS PC reported for a call: a status (see STATUSES) and its output."""
    status: str
    text: str


@dataclass
class ToolCall:
    """A tool call the mock makes: name, string arguments, and the result once answered."""
    name: str
    args: dict
    id: str = ''
    result: ToolResult = None


# Raised by scenario generators; mockapi.py turns them into API behaviour.

class Refused(Exception):
    """Claude declined to answer (stop_reason "refusal")."""


class Disconnect(Exception):
    """Drop the client's connection mid-reply."""


class MockAPIError(Exception):
    """Stands in for an Anthropic API error (an `error` event mid-stream)."""


class RetryOnce(Exception):
    """Answer this request with HTTP 529; the next one succeeds."""


CHUNKS_TEXT = ('Chunked transfer with the chunks cut every seven bytes, so sizes, CRLFs, event '
               'lines and even UTF-8 sequences like \u00e9\u2500 land across pieces. All arrived.')


SCENARIOS = {
    '!help': 'list these test commands',
    '!scroll': '60 numbered lines, for scrolling and PgUp/PgDn scrollback',
    '!burst': '200 short lines all at once, like a reply that finished thinking (fast scrolling)',
    '!long': 'several paragraphs of prose, for word wrap',
    '!wide': 'a 120-character word and a 1500-byte line (hard wrap, TXT splitting)',
    '!cp437': 'accented letters, symbols, box drawing, and Unicode CLAWDOS must approximate',
    '!markdown': 'Markdown the real model is told not to send, to see how it looks',
    '!slow': 'a reply that trickles out one word per second (try Esc)',
    '!stall': 'thinks for 30 seconds before answering (spinner, Esc)',
    '!error': 'an API error after a few words',
    '!refuse': 'a refusal (the message is dropped from history)',
    '!cutoff': 'a reply that hits max_tokens',
    '!drop': 'the connection drops mid-reply (once; the retry succeeds)',
    '!retry': 'HTTP 529 once, then a normal reply saying "second attempt"',
    '!chunks': 'chunked encoding split every 7 bytes',
    '!bigevent': 'one text delta of 20 KB (over the DOS PC\'s event buffer)',
    '!history': 'what the mock remembers of this conversation',
    '!run': 'run the rest of the message as a DOS command on the DOS PC: !run TCC PRIMES.C',
    '!tool': 'call one DOS tool: !tool read path=C:\\AUTOEXEC.BAT ("k=v w" quotes spaces)',
    '!tooltour': 'call every DOS tool in turn and check the results',
    '!toolstall': 'call sysinfo, then think for 30 seconds before answering (the spinner during tool rounds)',
    '!play': 'play a tune on the PC speaker',
}

PLAY_DEMO = 'T140 O3 L4 E E F G G F E D C C D E E. D8 D2 E E F G G F E D C C D E D. C8 C2'

# The tool tour: (tool, args, expected status, text the result must contain).
# Tools HARNESS.EXE doesn't have yet are expected to fail; flip them as they land.
TOUR_EXPECT = {}
TOUR_TEXT = 'Hello from the tool tour.\nSecond line.'


def tour_steps():
    """The tool tour's steps: (tool, args, expected status, expected text or check)."""
    steps = [
        ('sysinfo', {}, 'ok', 'DOS'),
        ('dir', {'path': 'C:\\'}, 'ok', 'AUTOEXEC.BAT'),
        # an empty or missing floppy drive must fail, not prompt "Abort, Retry, Fail?"
        ('dir', {'path': 'A:\\'}, 'error', None),
        ('write', {'path': 'C:\\DOSTOUR.TXT', 'text': TOUR_TEXT}, 'ok', None),
        ('read', {'path': 'c:\\dostour.txt'}, 'ok', 'Hello from the tool tour.'),
        ('edit', {'path': 'C:\\DOSTOUR.TXT', 'old': 'Second line.', 'new': 'Second line, edited.\nThird.'},
         'ok', '    3 Third.'),
        ('read', {'path': 'C:\\DOSTOUR.TXT', 'start': '3'}, 'ok', '    3 Third.'),
        ('edit', {'path': 'C:\\DOSTOUR.TXT', 'old': 'Not in the file', 'new': 'x'}, 'error', 'not found'),
        ('search', {'text': 'EDITED', 'path': 'C:\\DOSTOUR.TXT C:\\NOPE\\*.C'}, 'ok',
         '    2: Second line, edited.'),
        ('rename', {'from': 'C:\\DOSTOUR.TXT', 'to': 'C:\\DOSTOUR2.TXT'}, 'ok', None),
        ('mkdir', {'path': 'C:\\DOSTDIR'}, 'ok', None),
        ('delete', {'path': 'C:\\DOSTOUR2.TXT'}, 'ok', None),
        ('delete', {'path': 'C:\\*.BAT'}, 'error', None),
        ('poke', {'addr': '0000:04F0', 'bytes': '44 4F 53'}, 'ok', None),
        ('peek', {'addr': '0000:04F0', 'len': '3'}, 'ok', '44 4F 53'),
        ('port', {'port': '61'}, 'ok', None),
        ('textart', {'text': '~EClaw~CDOS'}, 'ok', None),
        ('read', {'path': 'C:\\NOPE.TXT'}, 'error', 'not found'),
        ('play', {'music': 'T200 L16 O4 CEG>C'}, 'ok', None),
        ('memory_write', {'name': 'DOSTOUR', 'text': 'Tour memory\nSecond line.'}, 'ok', None),
        ('memory_list', {}, 'ok', 'DOSTOUR'),
        ('memory_read', {'name': 'dostour'}, 'ok', 'Second line.'),
        ('memory_write', {'name': 'NO/GOOD', 'text': 'x'}, 'error', None),
        # a program that reads standard input gets end-of-file, not a hang (FIND with
        # no file reads stdin); the exit code is reported and the rest of the list skipped
        ('run', {'command': 'FIND /C "x"\nVER'}, 'ok', lambda r: '> FIND' in r and 'exit code' in r),
        # a list of commands stops at the first that fails
        ('run', {'command': 'VER\nDIR C:\\NOPE.XYZ\nREM never'}, 'ok',
         lambda r: '> DIR C:\\NOPE.XYZ' in r and 'not run:\n  REM never' in r),
    ]
    out = []
    for name, args, status, want in steps:
        if TOUR_EXPECT.get(name) == 'error':
            status, want = 'error', None
        out.append((name, args, status, want))
    return out

LOREM = (
    "The IBM Personal Computer model 5160, arrived in March 1983. It kept "
    "the original PC's Intel 8088 at 4.77 MHz but added a 10 MB hard disk, "
    "eight expansion slots, and room for 640 KB of memory on later "
    "motherboards. Text on a CGA card lives at segment B800, two bytes per "
    "character: one for the code page 437 glyph and one for its colours.\n\n"
    "Writing to that memory while the card is drawing the screen causes the "
    "famous CGA snow, which is why careful programs wait for the horizontal or "
    "vertical retrace before touching it. Scrolling by copying 4000 bytes is "
    "slow on an 8088, so ClawDOS moves the CRTC start address instead and lets "
    "the 16 KB of video memory act as a ring of scrollback.\n\n"
    "Networking was never part of the original design, but a packet driver, "
    "mTCP, and a PicoMEM card on WiFi make it a small TCP/IP host. The heavy "
    "lifting of TLS happens in a dumb tunnel; everything else, from the JSON to "
    "the conversation history, is done by the 40-year-old machine itself."
)

CP437_SAMPLE = (
    "Accents: café, naïve, Ångström, señor, Zürich, façade\n"
    "Symbols: ½ ¼ ° ± ² · ¿ ¡ « » £ ¥ ƒ α ß π Σ µ τ Φ Ω δ ∞ √ ≡ ≈ ≥ ≤ ÷\n"
    "Boxes:   ┌──┬──┐ ╔══╦══╗ ░▒▓█ ▀▄▌▐\n"
    "         └──┴──┘ ╚══╩══╝\n"
    "Approximated by CLAWDOS: “curly quotes” ‘single’ – en — em … "
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
     "Hello from the mock API! I'm a stand-in for Claude, so no real API calls "
     "are being made. Everything about the connection is real though: your "
     "DOS PC sent that message over TCP, and this reply is streaming back a few "
     "characters at a time. Send !help to see the test commands."),
    (r'\b(who|what) are you\b',
     "I'm the mock Messages API for ClawDOS. I imitate the real model closely enough "
     "to test HARNESS.EXE: replies stream in token-sized pieces after a short "
     "pause, I remember the conversation, and I can fail in all the ways "
     "the real API can. Point CLAWDOS at a TLS tunnel for the real thing."),
    (r'\b(8088|ibm|cga|pc)\b',
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
     "Goodbye! Press F10 to leave HARNESS.EXE."),
]


class MockChat:
    """One mock conversation. `speed` scales every delay; 0 means no waiting."""

    def __init__(self, speed=1.0, seed=None, state=None):
        self.speed = speed
        self.rng = random.Random(seed)
        self.history = []   # (user text, reply text) pairs
        self.state = state if state is not None else {}   # shared across requests: what already happened
        self.chunk_every = 0                               # !chunks: split the HTTP chunks this often

    def pause(self, seconds):
        """Sleep for `seconds` scaled by the speed (not at all when speed is 0)."""
        if self.speed > 0:
            time.sleep(seconds / self.speed)

    def stream(self, text, per_token=0.035):
        """Yield text in roughly token-sized pieces (1-6 characters)."""
        i = 0
        while i < len(text):
            n = self.rng.randint(1, 6)
            self.pause(per_token * self.rng.uniform(0.5, 1.5))
            yield text[i:i + n]
            i += n

    def reply(self, text):
        """Stream the reply to one user message, remembering it once complete."""
        self.pause(self.rng.uniform(0.4, 1.2))   # time to first token
        command = text.split()[0].lower() if text.startswith('!') else None
        yield from self.respond(command, text)

    def respond(self, command, text):
        """Yield the reply to one message: text chunks and ToolCalls. `command`
        is the lowercased first word if the message starts with "!", else None."""
        if command is None:
            source = self.stream(self.small_talk(text))
        else:
            handler = getattr(self, '_cmd_' + command[1:], None) if command[1:].isalnum() else None
            if handler is None:
                source = self.stream(f'Unknown test command {command}. Send !help for the list.')
            else:
                source = handler(text)
        yield from source

    def _cmd_help(self, _text):
        lines = ['Mock API test commands:\n']
        lines += [f'  {k:<10} {v}\n' for k, v in SCENARIOS.items()]
        lines.append('Anything else gets a canned conversational reply.')
        yield from self.stream(''.join(lines), per_token=0.01)

    def _cmd_scroll(self, _text):
        for i in range(1, 61):
            self.pause(0.02)
            yield f'Line {i} of 60: the quick brown fox jumps over the lazy dog.\n'

    def _cmd_burst(self, _text):
        yield ''.join(f'- Burst line {i} of 200: CPU, RAM, BIOS, video.\n' for i in range(1, 201))

    def _cmd_long(self, _text):
        yield from self.stream(LOREM)

    def _cmd_wide(self, _text):
        yield 'A 120-character word: ' + ''.join(chr(65 + i % 26) for i in range(120)) + '\n'
        yield 'A 1500-byte line: ' + ' '.join(f'word{i:03}' for i in range(170)) + '\n'
        yield 'Done.'

    def _cmd_cp437(self, _text):
        yield from self.stream(CP437_SAMPLE, per_token=0.01)

    def _cmd_markdown(self, _text):
        yield from self.stream(MARKDOWN_SAMPLE, per_token=0.01)

    def _cmd_slow(self, _text):
        for w in 'This reply trickles out one word per second so you can press Esc to stop it.'.split():
            self.pause(1.0)
            yield w + ' '

    def _cmd_stall(self, _text):
        self.pause(30)
        yield 'Sorry for the wait. That was a 30-second stall.'

    def _cmd_error(self, _text):
        yield from self.stream('Starting a reply, and then ')
        raise MockAPIError('API error 529: Overloaded (simulated)')

    def _cmd_refuse(self, _text):
        raise Refused('The model declined to answer that. (simulated)')
        yield   # never reached: makes this a generator like the others

    def _cmd_cutoff(self, _text):
        yield from self.stream(LOREM.split('\n\n', maxsplit=1)[0])
        yield '\n[reply cut off: too long]'

    def _cmd_drop(self, _text):
        yield from self.stream('This sentence will never be fin')
        if not self.state.get('dropped'):
            self.state['dropped'] = True
            raise Disconnect()
        yield from self.stream('ished the first time; this is the reply after the drop.')

    def _cmd_retry(self, _text):
        if not self.state.get('retried'):
            self.state['retried'] = True
            raise RetryOnce()
        yield from self.stream('The first request got a 529; this is the second attempt.')

    def _cmd_chunks(self, _text):
        self.chunk_every = 7
        yield from self.stream(CHUNKS_TEXT)

    def _cmd_bigevent(self, _text):
        yield 'A single text delta of 20 KB follows: ' + 'x' * 20480

    def _cmd_history(self, _text):
        yield from self.stream(self.describe_history())

    def _cmd_run(self, text):
        yield from self.call('run', {'command': text.split(None, 1)[1] if ' ' in text.strip() else ''})

    def _cmd_tool(self, text):
        words = shlex.split(text, posix=False)[1:]
        if not words:
            yield 'Usage: !tool NAME key=value ...'
            return
        args = {}
        for w in words[1:]:
            key, _, value = w.strip('"').partition('=')
            args[key] = value.strip('"')
        yield from self.call(words[0], args)

    def _cmd_play(self, _text):
        yield from self.call('play', {'music': PLAY_DEMO})

    def _cmd_tooltour(self, _text):
        yield from self.tooltour()

    def _cmd_toolstall(self, _text):
        yield from self.call('sysinfo', {})
        self.pause(30)
        yield from self.stream(' That was a 30-second think after the tool.')

    def call(self, name, args):
        """Announce a tool call (without ending the line), make it, report it."""
        yield from self.stream(f'Calling {name}.')
        call = ToolCall(name, args)
        yield call
        result = call.result
        yield from self.stream(f'{name} -> {result.status}: {result.text[:300]}', per_token=0.005)

    def tooltour(self):
        """Call every tool in turn and end with a PASS or FAIL verdict."""
        yield 'Touring the DOS tools.\n'
        failure = None
        for i, (name, args, status, want) in enumerate(tour_steps(), 1):
            call = ToolCall(name, args)
            yield call
            got = call.result
            ok = got.status == status and (want is None or (want(got.text) if callable(want) else want in got.text))
            yield f'{i:2}. {name}: {got.status}{"" if ok else "  <- expected " + status}\n'
            if not ok and failure is None:
                failure = f'step {i} {name}: got {got.status} {got.text[:60]!r}'
        verdict = 'tool tour: PASS' if failure is None else f'tool tour: FAIL {failure}'
        print(verdict, flush=True)
        yield verdict

    def small_talk(self, text):
        """A canned reply to an ordinary message."""
        lowered = text.lower()
        for pattern, answer in SMALL_TALK:
            if re.search(pattern, lowered):
                return answer
        words = len(text.split())
        reply = (f"(Mock reply #{len(self.history) + 1}.) You wrote {words} "
                 f"word{'s' if words != 1 else ''}: “{text}”. ")
        if self.history:
            reply += f"Before that you said “{self.history[-1][0]}”, so your history is reaching me. "
        reply += self.rng.choice([
            "The real Claude would answer this properly.",
            "That's an interesting thought; the real ClawDOS would have more to say about it.",
            "I'm only a mock, but your message arrived intact.",
        ])
        return reply

    def describe_history(self):
        """What the mock remembers, for !history."""
        if not self.history:
            return 'Nothing yet: this is the first message of the conversation.'
        out = [f'I remember {len(self.history)} earlier message(s):\n']
        out += [f'  {i}. {t[:60]}\n' for i, (t, _) in enumerate(self.history, 1)]
        out.append('F2 starts a new conversation and clears this.')
        return ''.join(out)
