"""Mock Claude for the mock Messages API server (mockapi.py): no API key, no
network, no cost. It behaves like the real model as far as the DOS PC can tell:
a pause before the first word, then text streamed in token-sized pieces, and the
same failure modes (API errors, refusals, cut-off replies, dropped links).

Ordinary messages get a canned conversational reply. Messages starting with
`!` trigger test scenarios; send `!help` for the list.
"""

import base64
import json
import os
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
    """Stands in for an Anthropic API error (an `error` event mid-stream) of type `kind`."""

    def __init__(self, message, kind='overloaded_error'):
        super().__init__(message)
        self.kind = kind


class HttpError(Exception):
    """Answer this request with an HTTP error instead of a stream (the message is the error's text)."""

    def __init__(self, status, kind, message, retry_after=None):
        super().__init__(message)
        self.status, self.kind, self.retry_after = status, kind, retry_after


class Ping:
    """Yielded by a scenario: send a `ping` event here."""


class Fail:
    """Yielded by a scenario as the first thing of the reply to a tool result: answer that request with HTTP
    `status` (error type `kind`), or with status None an `error` event of type `kind` before any text. The
    resend of the request goes on from after this point."""

    def __init__(self, status, kind, message):
        self.status, self.kind, self.message = status, kind, message


# !threadretry: what the continue after its call meets once.
RETRY_FAILS = {'529': (529, 'overloaded_error', 'Overloaded (simulated, on a continue)'),
               'overload': (None, 'overloaded_error', 'Overloaded (simulated, before any text of a continue)'),
               '404': (404, 'not_found_error', 'Not found (simulated, on a continue)'),
               '400': (400, 'invalid_request_error', 'Bad request (simulated, on a continue)')}


class Drop:
    """Yielded by a scenario after a tool call: drop the link here. The resend of the request that brought
    the result goes on from after this point. With `burn` the mock counts the dropped continue as having
    used its response up, so a resend of it meets thread_already_continued."""

    def __init__(self, burn=False):
        self.burn = burn


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
    '!runrefuse': 'a full_memory run of VER, then a refusal (the whole turn goes, the run with it)',
    '!cutoff': 'a reply that hits max_tokens',
    '!drop': 'the connection drops mid-reply (once; the retry succeeds)',
    '!retry': 'HTTP 529 once, then a normal reply saying "second attempt"',
    '!busy': 'HTTP 529 three times (retry-after 1), so the DOS PC\'s retries run out; then a normal reply',
    '!400': 'HTTP 400 once (invalid_request_error, retry-after 1), then a normal reply',
    '!401': 'HTTP 401 once (authentication_error, retry-after 1), then a normal reply',
    '!429': 'HTTP 429 once (rate_limit_error, retry-after 1), then a normal reply',
    '!overload': 'an overloaded_error event before any text (once; the retry succeeds)',
    '!badstream': 'an invalid_request_error event before any text (not retried)',
    '!chunks': 'chunked encoding split every 7 bytes',
    '!bigevent': 'one text delta of 20 KB (over the DOS PC\'s event buffer)',
    '!bigsig': 'a 24 KB thinking signature (over the event buffer), a tool call, then a check that it came back intact',
    '!loop': 'calls sysinfo again and again until tool_choice is none (the 25-round cap), then answers',
    '!ping': 'ping events between the text deltas',
    '!think': 'a thinking block with thinking_delta text and a signature, then text',
    '!thinktool': 'thinking text with a quote, a backslash and non-ASCII letters, then a tool call and a check',
    '!redacted': 'a redacted_thinking block, a tool call, then a check that the block came back',
    '!history': 'what the mock remembers of this conversation',
    '!run': 'run the rest of the message as a DOS command on the DOS PC: !run TCC PRIMES.C',
    '!bigrun': 'the same with full_memory (under CLAWDOS.EXE the harness steps out of memory): !bigrun MEM /C',
    '!tool': 'call one DOS tool: !tool read path=C:\\AUTOEXEC.BAT ("k=v w" quotes spaces)',
    '!tooltour': 'call every DOS tool in turn and check the results',
    '!toolstall': 'call sysinfo, then think for 30 seconds before answering (the spinner during tool rounds)',
    '!play': 'play a tune on the PC speaker',
    '!bench': 'the benchmark (scripts/bench.py): 20 reads of 4 KB, a full_memory run, a 4 KB reply',
    '!note': 'text, a sysinfo call, then a progress note with blank lines and a break around it, then text',
    '!shotmem': 'MEM in the harness, a screenshot through the launcher, MEM again: the screenshot must not keep memory',
    '!threadlost': 'a sysinfo call (!threadlost run: a full_memory run) after which the mock forgets the thread',
    '!threaddrop': 'a sysinfo call, then the link drops in the continue\'s reply (!threaddrop burn: and a resend '
                   'meets thread_already_continued)',
    '!threadfail': 'a sysinfo call, then thread_not_found for the continue and for its replay (the turn ends)',
    '!threadretry': 'a sysinfo call, then the continue meets 529 once (!threadretry overload, 404 or 400 instead)',
    '!headercol': 'text, a sysinfo call, then peeks of CGA memory until the tool header\'s row is found: '
                   'logs the column its bullet starts in (0 is right)',
    '!nothreads': 'two sysinfo calls; from the second request on, thread_unsupported_request for any thread',
    '!skills': 'checks the system prompt lists the shipped skills (not as memories), then reads one in two parts',
    '!tailscreen': 'peeks CGA memory for a reopened chat\'s transcript: each earlier message\'s first row, in order',
    '!reopened': 'logs the messages a reopened chat sent (!reopened NAME labels the line)',
}

# The tool headers' titles (src/harness/tools.cpp), for !tailscreen.
TOOL_TITLES = {'sysinfo': 'SysInfo', 'dir': 'Dir', 'read': 'Read', 'peek': 'Peek', 'search': 'Search',
               'write': 'Write', 'edit': 'Edit', 'delete': 'Delete', 'mkdir': 'MkDir', 'rename': 'Rename',
               'poke': 'Poke', 'port': 'Port', 'textart': 'TextArt', 'play': 'Play', 'run': 'Run',
               'memory_list': 'Memories', 'memory_read': 'Memory', 'memory_write': 'Remember'}
SHOWN_TYPED = 3         # a reopened chat shows its messages from the third last typed one (lib/chatfile.h)
TAIL_PEEKS = 20         # !tailscreen reads at most this many 256-byte pieces of the screen

# A thinking signature longer than the DOS PC's 16 KB event buffer (see !bigsig).
BIG_SIGNATURE = base64.b64encode(bytes(range(256)) * 72).decode()   # 24576 bytes

PLAY_DEMO = 'T140 O3 L4 E E F G G F E D C C D E E. D8 D2 E E F G G F E D C C D E D. C8 C2'

DEFAULT_TOOLS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                             'image', 'clawdos', 'TOOLS.JSN')

# !note's progress note: blank lines and spaces before it, a break inside, blank lines after.
NOTE_TEXT = '\n\n  Two\nlines\n\n'
# A tool tour's continue requests carry one tool result (at most 4 KB of output) and the fixed fields.
TOUR_CONTINUE_MAX = 8192

# The text of a !think block, and the opaque data of a !redacted one.
THINKING_TEXT = ('The user sent !think. I should answer with a thinking block that has some text in it, '
                 'streamed as thinking_delta pieces ahead of the signature, so the DOS PC keeps it in the '
                 'history and sends it back unchanged on the next request.')
# !thinktool: JSON needs escaping here (a quote, a backslash) and UTF-8 needs more than a byte (\u00e9, \u00fc).
THINKING_ESCAPES = 'He said "wait", the path is C:\\DOS\\, caf\u00e9 and Z\u00fcrich, then a tool call.'
REDACTED_DATA = base64.b64encode(bytes(range(256)) * 3).decode()


# !skills: the skills the build ships (image/skills), one read in parts of SKILL_PART lines, and a line from
# each end of it.
SKILLS = ('C:\\SKILLS\\DEVTOOLS.TXT', 'C:\\SKILLS\\DEMOFX.TXT')
SKILL_READ = SKILLS[0]
SKILL_PART = 40
SKILL_FIRST = '    1 Dev tools'
SKILL_LAST = 'Plain DEBUG still works too.'


def skills_listed(system):
    """None if the system prompt's skills list names every shipped skill and its memories list none of them,
    else what is wrong. The skills list runs from "Skills now" to the "Memories:" paragraph."""
    start, mems = system.find('Skills now'), system.find('Memories now')
    if start < 0 or mems < 0 or not start < system.find('Memories:') < mems:
        return 'no skills list before the memories list'
    listed, memories = system[start:system.find('Memories:')], system[mems:]
    missing = [s for s in SKILLS if s not in listed]
    if missing:
        return 'skills list lacks ' + ', '.join(missing)
    doubled = [s for s in SKILLS if s.rsplit('\\', 1)[1].split('.')[0] in memories]
    if doubled:
        return 'memories list has ' + ', '.join(doubled)
    return None


def schema_types(path=DEFAULT_TOOLS):
    """{tool: {argument: JSON type}} from TOOLS.JSN, the one source of the schemas."""
    with open(path, encoding='utf-8') as f:
        return {t['name']: {k: v.get('type') for k, v in t['input_schema']['properties'].items()}
                for t in json.load(f)}


def typed_value(kind, text):
    """A !tool value as its schema type: numbers and booleans as JSON, not strings.
    Text that doesn't parse stays a string, so the harness's own error can be tried."""
    if kind == 'integer':
        try:
            return int(text, 0)
        except ValueError:
            return text
    if kind == 'number':
        try:
            return int(text) if text.lstrip('-').isdigit() else float(text)
        except ValueError:
            return text
    if kind == 'boolean' and text.lower() in ('true', 'false', '1', '0'):
        return text.lower() in ('true', '1')
    return text


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
        ('read', {'path': 'C:\\DOSTOUR.TXT', 'start': 3}, 'ok', '    3 Third.'),
        ('edit', {'path': 'C:\\DOSTOUR.TXT', 'old': 'Not in the file', 'new': 'x'}, 'error', 'not found'),
        ('search', {'text': 'EDITED', 'path': 'C:\\DOSTOUR.TXT C:\\NOPE\\*.C'}, 'ok',
         '    2: Second line, edited.'),
        ('rename', {'from': 'C:\\DOSTOUR.TXT', 'to': 'C:\\DOSTOUR2.TXT'}, 'ok', None),
        ('mkdir', {'path': 'C:\\DOSTDIR'}, 'ok', None),
        ('delete', {'path': 'C:\\DOSTOUR2.TXT'}, 'ok', None),
        ('delete', {'path': 'C:\\*.BAT'}, 'error', None),
        ('poke', {'addr': '0000:04F0', 'bytes': '44\t4F, 53\t'}, 'ok', None),   # tabs arrive as tabs
        ('peek', {'addr': '0000:04F0', 'len': 3}, 'ok', '44 4F 53'),
        ('port', {'port': '61'}, 'ok', None),
        ('textart', {'text': '~EClaw~CDOS'}, 'ok', None),
        ('read', {'path': 'C:\\NOPE.TXT'}, 'error', 'not found'),
        ('play', {'music': 'T200\tL16 O4 CEG>C'}, 'ok', None),
        ('memory_write', {'name': 'DOSTOUR', 'text': 'Tour memory\nSecond line.'}, 'ok', None),
        ('memory_list', {}, 'ok', 'DOSTOUR'),
        ('memory_read', {'name': 'dostour'}, 'ok', 'Second line.'),
        ('memory_write', {'name': 'NO/GOOD', 'text': 'x'}, 'error', None),
        # a program that reads standard input gets end-of-file, not a hang: STDINEOF.COM
        # (tests/unit/stdineof.asm), started directly (no COMMAND.COM), reads to the end and says so
        ('run', {'command': 'C:\\TESTS\\STDINEOF.COM'}, 'ok',
         lambda r: 'STDIN EOF after 0 bytes' in r and 'exit code 0' in r),
        # a list of commands stops at the first that fails; full_memory makes it a
        # relaunch under CLAWDOS.EXE (HARNESS.EXE alone runs it in place and says so)
        ('run', {'command': 'VER\nDIR C:\\NOPE.XYZ\nREM never', 'full_memory': True}, 'ok',
         lambda r: '> DIR C:\\NOPE.XYZ' in r and 'not run:\n  REM never' in r),
    ]
    out = []
    for name, args, status, want in steps:
        if TOUR_EXPECT.get(name) == 'error':
            status, want = 'error', None
        out.append((name, args, status, want))
    return out

# The benchmark (scripts/bench.py, image/tests/bench.bat). Tasks after it rerun
# it and compare, so keep it as it is: no randomness, no pauses. Each read's
# result (60 numbered lines of 60 characters, image/tests/bench/BENCH.TXT) is
# close to the 4 KB tool output cap, so the history passes 100 KB by the end.
BENCH_READS = 20
BENCH_FILE = 'C:\\TESTS\\BENCH\\BENCH.TXT'
# What a whole read result holds: 59 of the 60 lines fit the 4 KB output cap
# (each is 67 bytes with its number), then the line saying where to go on.
BENCH_READ_LAST = 'Bench line 59: '
BENCH_READ_END = '...(more lines from 60; use start)'
BENCH_PIECE = 16          # characters per text delta of the final reply
BENCH_PARAGRAPH = (
    "The 8088 reads every byte of this reply off the network card, parses the "
    "server-sent events and the JSON inside them, turns UTF-8 into code page "
    "437, wraps the words and writes them into CGA memory between retraces, "
    "while it also appends the same text to the conversation file on disk. "
    "The streamed characters per second are therefore the PC's figure, not "
    "the mock's, which sends this as fast as the link will take it. "
)
BENCH_REPLY = ''.join(f'Paragraph {i} of 9. {BENCH_PARAGRAPH}\n\n' for i in range(1, 10)).rstrip()

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


def peek_bytes(text):
    """The bytes of a peek result: each line "B800:0000 " then " XX" sixteen times, then the characters."""
    return bytes(int(h, 16) for line in text.splitlines()
                 if re.match(r'[0-9A-F]{4}:[0-9A-F]{4} ', line) for h in line[10:58].split())


def screen_rows(vram):
    """The text of each whole screen row (160 bytes: character, attribute) in vram."""
    return [bytes(vram[r * 160:(r + 1) * 160:2]).decode('cp437') for r in range(len(vram) // 160)]


def tail_rows(messages):
    """How a reopened chat's transcript starts each shown message's part, in order: "> " and the start of
    typed text, a bullet and the start of reply text, a bullet and the title of a tool call."""
    typed = [i for i, m in enumerate(messages) if m['role'] == 'user' and isinstance(m['content'], str)]
    rows = []
    for m in messages[typed[-SHOWN_TYPED] if len(typed) >= SHOWN_TYPED else 0:]:
        blocks = [{'type': 'text', 'text': m['content']}] if isinstance(m['content'], str) else m['content']
        for b in blocks:
            if b.get('type') == 'text':
                start = b['text'].split('\n')[0][:30]
                start = re.match(r'[ -~]*', start).group(0).rstrip()      # as far as it is plain ASCII
                rows.append(('> ' if m['role'] == 'user' else '\x07 ') + start)
            elif b.get('type') == 'tool_use':
                rows.append('\x07 ' + TOOL_TITLES.get(b['name'], b['name']))
    return rows


def rows_missing(rows, want):
    """The rows of want that the screen rows do not hold in that order (each a row's start)."""
    i = 0
    for row in rows:
        if i < len(want) and row.startswith(want[i]):
            i += 1
    return want[i:]


def message_shape(messages):
    """role[block types] for each message: user[text] assistant[thinking,text,tool_use] ..."""
    def kinds(content):
        return 'text' if isinstance(content, str) else ','.join(b['type'] for b in content)
    return ' '.join(f"{m['role']}[{kinds(m['content'])}]" for m in messages)


def header_column(vram):
    """The column of the bullet on the first screen row (160 bytes: character, attribute) whose text holds
    "SysInfo", the tool header; None until such a row is complete."""
    for row in range(len(vram) // 160):
        text = bytes(vram[row * 160:(row + 1) * 160:2]).decode('cp437')
        at = text.find('SysInfo')
        if at >= 0:
            return text.find('\x07') if '\x07' in text[:at] else -1
    return None


class MockChat:
    """One mock conversation. `speed` scales every delay; 0 means no waiting."""

    def __init__(self, speed=1.0, seed=None, state=None, tools_path=DEFAULT_TOOLS, threads=None):
        self.speed = speed
        self.threads = threads                             # the server's Threads, for the thread scenarios
        self.thread_kind = 'no thread'                     # what the request being answered was (the server)
        self.requests = []                                 # (kind, body bytes) of each request answered
        self.tools_path = tools_path                       # the TOOLS.JSN the server checks requests against
        self.no_tools = False                              # the request said tool_choice none (set by the server)
        self.system = ''                                   # the system prompt's text, as the model sees it (the server)
        self.messages = []                                 # the request's messages (the server)
        self.progress = False                              # the request asked for display "updates" (the server)
        self.progress_note = ''                            # the thinking text sent then, if a scenario sets one
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

    def _cmd_runrefuse(self, _text):
        yield ToolCall('run', {'command': 'VER', 'full_memory': True})
        raise Refused('The model declined to answer that. (simulated, after a run)')

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
            raise HttpError(529, 'overloaded_error', 'Overloaded (simulated; try again)', retry_after=1)
        yield from self.stream('The first request got a 529; this is the second attempt.')

    def _cmd_busy(self, _text):
        self.state['busy'] = self.state.get('busy', 0) + 1
        if self.state['busy'] <= 3:
            raise HttpError(529, 'overloaded_error', f'Overloaded (simulated, attempt {self.state["busy"]} of 3)',
                            retry_after=1)
        yield from self.stream('The overload is over; this is the reply after three 529s.')

    def once_http_error(self, status, kind, message):
        """Raise the error the first time, so the next request gets the reply."""
        key = f'http{status}'
        if not self.state.get(key):
            self.state[key] = True
            raise HttpError(status, kind, message, retry_after=1)
        yield from self.stream(f'The first request got a {status}; this is the reply after it.')

    def _cmd_400(self, _text):
        yield from self.once_http_error(400, 'invalid_request_error', 'Bad request (simulated)')

    def _cmd_401(self, _text):
        yield from self.once_http_error(401, 'authentication_error', 'invalid x-api-key (simulated)')

    def _cmd_429(self, _text):
        yield from self.once_http_error(429, 'rate_limit_error', 'Rate limited (simulated)')

    def _cmd_overload(self, _text):
        if not self.state.get('overloaded'):
            self.state['overloaded'] = True
            raise MockAPIError('Overloaded (simulated, before any text)')
        yield from self.stream('The stream said overloaded_error before any text; '
                               'this is the reply after the overload.')

    def _cmd_badstream(self, _text):
        raise MockAPIError('Bad request (simulated, before any text)', 'invalid_request_error')
        yield   # never reached: makes this a generator like the others

    def _cmd_chunks(self, _text):
        self.chunk_every = 7
        yield from self.stream(CHUNKS_TEXT)

    def _cmd_bigevent(self, _text):
        yield 'A single text delta of 20 KB follows: ' + 'x' * 20480

    def _cmd_bigsig(self, _text):
        # Long thinking gives a long signature, sent as one event; the DOS PC
        # must keep it whole, because the request after a tool call echoes it.
        self.signature = BIG_SIGNATURE
        yield from self.call('sysinfo', {})
        yield from self.stream(' The long signature made the round trip.')

    def _cmd_loop(self, _text):
        calls = 0
        while not self.no_tools:
            calls += 1
            yield f'Loop call {calls}.'
            yield ToolCall('sysinfo', {})
        print(f'loop: {calls} tool calls, then tool_choice none', flush=True)
        yield f'Loop ended after {calls} tool calls: tool_choice was none, so I answer without a tool.'

    def _cmd_ping(self, _text):
        for word in ('Ping', 'events', 'between', 'the', 'text', 'deltas.'):
            yield Ping()
            yield word + ' '
        yield Ping()

    def _cmd_think(self, _text):
        self.thinking_text = THINKING_TEXT
        yield from self.stream('The thinking block had text and a signature.')

    def _cmd_thinktool(self, _text):
        self.thinking_text = THINKING_ESCAPES
        yield from self.call('sysinfo', {})
        yield from self.stream(' The escaped thinking text came back.')

    def _cmd_redacted(self, _text):
        self.redacted_data = REDACTED_DATA
        yield from self.call('sysinfo', {})
        yield from self.stream(' The redacted thinking block came back unchanged.')

    def _cmd_history(self, _text):
        yield from self.stream(self.describe_history())

    def _cmd_run(self, text):
        yield from self.call('run', {'command': text.split(None, 1)[1] if ' ' in text.strip() else ''})

    def _cmd_bigrun(self, text):
        yield from self.call('run', {'command': text.split(None, 1)[1] if ' ' in text.strip() else '',
                                     'full_memory': True})
        print(f'bigrun: the result came by {self.thread_kind}', flush=True)

    def _cmd_tool(self, text):
        words = shlex.split(text, posix=False)[1:]
        if not words:
            yield 'Usage: !tool NAME key=value ...'
            return
        try:
            types = schema_types(self.tools_path).get(words[0], {})
        except (OSError, ValueError, KeyError):
            types = {}
        args = {}
        for w in words[1:]:
            key, _, value = w.strip('"').partition('=')
            args[key] = typed_value(types.get(key), value.strip('"'))
        yield from self.call(words[0], args)

    def _cmd_play(self, _text):
        yield from self.call('play', {'music': PLAY_DEMO})

    def _cmd_tooltour(self, _text):
        yield from self.tooltour()

    def _cmd_bench(self, _text):
        failure = None
        calls = [ToolCall('read', {'path': BENCH_FILE}) for _ in range(BENCH_READS)]
        calls.append(ToolCall('run', {'command': 'VER', 'full_memory': True}))
        for i, call in enumerate(calls, 1):
            yield f'Bench call {i} of {len(calls)}: {call.name}.'
            yield call
            got = call.result
            whole = call.name != 'read' or BENCH_READ_LAST in got.text and got.text.rstrip().endswith(BENCH_READ_END)
            if (got.status != 'ok' or not whole) and failure is None:
                failure = f'call {i} {call.name}: {got.status} {got.text[:60]!r}...{got.text[-40:]!r}'
        print('bench: PASS' if failure is None else f'bench: FAIL {failure}', flush=True)
        for i in range(0, len(BENCH_REPLY), BENCH_PIECE):
            yield BENCH_REPLY[i:i + BENCH_PIECE]

    def _cmd_note(self, _text):
        yield 'Text before the note.'
        yield ToolCall('sysinfo', {})
        self.progress_note = NOTE_TEXT          # the next response opens with it
        yield 'Text after the note.'

    def _cmd_shotmem(self, _text):
        """The largest program MEM sees in the harness before and after a full_memory screenshot: the resumed
        harness makes the PNG, and must give back the memory it used for that."""
        sizes = []
        for args in ({'command': 'MEM'}, {'command': 'C:\\TESTS\\QUADS.COM', 'screenshot': True, 'full_memory': True},
                     {'command': 'MEM'}):
            call = ToolCall('run', args)
            yield call
            found = re.search(r'Largest executable program size\s+\S+\s+\(\s*([\d,]+) bytes\)', call.result.text)
            if args['command'] == 'MEM':
                sizes.append(int(found.group(1).replace(',', '')) if found else None)
        ok = None not in sizes and sizes[1] >= sizes[0]
        print(f'shotmem: largest program {sizes[0]} before, {sizes[1]} after: {"PASS" if ok else "FAIL"}', flush=True)
        yield f'Largest program before {sizes[0]}, after {sizes[1]}.'

    def _cmd_threadlost(self, text):
        tool, args = ('run', {'command': 'VER', 'full_memory': True}) if 'run' in text.split()[1:] else ('sysinfo', {})
        if self.threads:
            self.threads.lose_next = True
        yield f'Calling {tool}; the mock forgets the thread before the next request.'
        call = ToolCall(tool, args)
        yield call
        print(f'threadlost {tool}: the result came by {self.thread_kind}', flush=True)
        yield f' {tool} -> {call.result.status}, after the thread was lost and the conversation replayed.'

    def _cmd_threaddrop(self, text):
        burn = 'burn' in text.split()[1:]
        yield 'Calling sysinfo; the link drops in the reply after it.'
        call = ToolCall('sysinfo', {})
        yield call
        yield 'Half a reply, then the link dro'
        yield Drop(burn)
        print(f'threaddrop{" burn" if burn else ""}: the result came again by {self.thread_kind}', flush=True)
        yield f'The reply after the dropped continue: sysinfo -> {call.result.status}.'

    def _cmd_threadretry(self, text):
        kind = (text.split()[1:] or ['529'])[0]
        status, error, message = RETRY_FAILS.get(kind, RETRY_FAILS['529'])
        yield f'Calling sysinfo; the continue after it meets {kind} once.'
        call = ToolCall('sysinfo', {})
        yield call
        yield Fail(status, error, message)
        print(f'threadretry {kind}: the result came again by {self.thread_kind}', flush=True)
        yield f'The reply after the {kind}: sysinfo -> {call.result.status}.'

    def _cmd_headercol(self, _text):
        yield 'Reply text, then a tool header straight after it.'
        call = ToolCall('sysinfo', {})
        yield call
        vram, column = bytearray(), None
        for off in range(0, 0x4000, 256):         # the screen's ring of rows, 160 bytes each from offset 0
            call = ToolCall('peek', {'addr': f'B800:{off:04X}', 'len': 256})
            yield call
            vram += peek_bytes(call.result.text)
            column = header_column(vram)
            if column is not None:
                break
        print(f'tool header column: {column} ({"PASS" if column == 0 else "FAIL"})', flush=True)
        yield f'The tool header starts in column {column}.'

    def _cmd_tailscreen(self, _text):
        want, before = tail_rows(self.messages[:-1]), len(self.messages) - 1
        yield f'Looking for the {len(want)} rows of the reopened chat on the screen.'
        vram, missing = bytearray(), want
        for off in range(0, TAIL_PEEKS * 256, 256):
            call = ToolCall('peek', {'addr': f'B800:{off:04X}', 'len': 256})
            yield call
            vram += peek_bytes(call.result.text)
            missing = rows_missing(screen_rows(vram), want)
            if not missing:
                break
        if missing:                   # what the screen held instead
            for row in screen_rows(vram)[:40]:
                print('tail screen row: ' + row.replace('\x07', '*').rstrip(), flush=True)
        print(f'tail screen: {"PASS" if want and not missing else "FAIL"} ({len(want) - len(missing)} of '
              f'{len(want)} rows{", missing " + repr(missing[0]) if missing else ""}; '
              f'{before} messages before this one)', flush=True)
        yield f'Found {len(want) - len(missing)} of {len(want)} rows.'

    def _cmd_reopened(self, text):
        label = (text.split() + [''])[1]
        last = self.messages[-1]['content'] if self.messages else ''
        blocks = [{'type': 'text', 'text': last}] if isinstance(last, str) else last
        said = ' | '.join(p for b in blocks if b.get('type') == 'text' for p in b['text'].split('\n\n'))
        results = '; '.join(f"{'error' if b.get('is_error') else 'ok'} {str(b.get('content'))[:60]}"
                            for b in blocks if b.get('type') == 'tool_result')
        print(f'reopened {label}: {len(self.messages)} messages: {message_shape(self.messages)}; text: {said}'
              + (f'; results: {results}' if results else ''), flush=True)
        yield f'Reopened chat {label} continued: {len(self.messages)} messages came.'

    def _cmd_threadfail(self, _text):
        if self.threads:
            self.threads.fail_next = 2
        yield 'Calling sysinfo; the continue after it and its replay are both refused.'
        yield ToolCall('sysinfo', {})
        yield 'This text is never sent: the turn ends at the second refusal.'

    def _cmd_nothreads(self, _text):
        if self.threads:
            self.threads.unsupported = 'on'
        for n in (1, 2):
            yield f'Calling sysinfo ({n} of 2).'
            yield ToolCall('sysinfo', {})
        replayed = self.threads is not None and self.threads.unsupported == 'replayed'
        if self.threads:
            self.threads.unsupported = None
        print(f'thread unsupported: {"PASS" if replayed else "FAIL"} (the requests after the refusal '
              f'came without a thread: {self.thread_kind})', flush=True)
        yield 'Both calls done; threads stayed off after thread_unsupported_request.'

    def _cmd_skills(self, _text):
        failure = skills_listed(self.system)
        yield f'The system prompt lists the skills{" (" + failure + ")" if failure else ""}; reading one in two parts.'
        first = ToolCall('read', {'path': SKILL_READ, 'lines': SKILL_PART})
        yield first
        cont = f'...(more lines from {SKILL_PART + 1}; use start)'
        if failure is None and not (first.result.status == 'ok' and SKILL_FIRST in first.result.text
                                    and first.result.text.rstrip().endswith(cont)):
            failure = f'first part: {first.result.status} {first.result.text[-80:]!r}'
        rest = ToolCall('read', {'path': SKILL_READ, 'start': SKILL_PART + 1})
        yield rest
        if failure is None and not (rest.result.status == 'ok' and SKILL_LAST in rest.result.text):
            failure = f'second part: {rest.result.status} {rest.result.text[-80:]!r}'
        verdict = 'skills: PASS' if failure is None else f'skills: FAIL {failure}'
        print(verdict, flush=True)
        yield f' {verdict}'

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
        """Call every tool in turn and end with a PASS or FAIL verdict. Each response opens with a progress
        note naming the next step (the server sends it as thinking text when the request asks for display
        "updates"); it is set before the text that leads the response, since the server reads it then."""
        steps = list(tour_steps())
        self.progress_note = f'Tour step 1 of {len(steps)}: {steps[0][0]}.'
        yield 'Touring the DOS tools.\n'
        failure = None
        for i, (name, args, status, want) in enumerate(steps, 1):
            call = ToolCall(name, args)
            yield call
            got = call.result
            ok = got.status == status and (want is None or (want(got.text) if callable(want) else want in got.text))
            self.progress_note = (f'Tour step {i + 1} of {len(steps)}: {steps[i][0]}.' if i < len(steps)
                                  else 'Tour done; the verdict next.')
            yield f'{i:2}. {name}: {got.status}{"" if ok else "  <- expected " + status}\n'
            if not ok and failure is None:
                failure = f'step {i} {name}: got {got.status} {got.text[:60]!r}'
        verdict = 'tool tour: PASS' if failure is None else f'tool tour: FAIL {failure}'
        print(verdict, flush=True)
        self.tour_threads()
        yield verdict

    def tour_threads(self):
        """With threads, every request of the tour after the first continues it, and is small, except one
        whose tool_choice changed (the round cap's none, a fixed field): that starts a new thread."""
        if not self.requests or self.requests[0][0] != 'thread create':
            return
        continues, recreated, wrong = [], 0, 0
        for (_, _, before), (kind, n, choice) in zip(self.requests, self.requests[1:]):
            if kind == 'thread continue' and choice == before:
                continues.append(n)
            elif kind == 'thread create' and choice != before:
                recreated += 1
            else:
                wrong += 1
        largest = max(continues, default=0)
        ok = not wrong and largest < TOUR_CONTINUE_MAX
        print(f'tool tour threads: {"PASS" if ok else "FAIL"} (a create of {self.requests[0][1]} bytes, '
              f'{len(continues)} continues, the largest {largest} bytes, {recreated} new for a tool_choice change, '
              f'{wrong} wrong)', flush=True)

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
