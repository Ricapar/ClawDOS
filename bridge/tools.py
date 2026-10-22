"""Tools the model can use on the XT, and how a call travels over the wire.

The schemas live here, on the bridge; CLAUDE.EXE only has the executors. A
call goes to the XT as

  TOOL <id> <name>
  ARG <key> <chunk>        (repeated; chunks of one key are appended)
  CALL

and comes back as OUT <chunk> lines and DONE <id> <status>. Chunks are
escaped: "\\n" is a newline, "\\\\" a backslash. Every argument is a string.
"""

from dataclasses import dataclass

from cp437 import to_cp437

MAX_ROUNDS = 25          # tool rounds per user message
MAX_PAYLOAD = 500        # longest line payload sent to the XT
STATUSES = ('ok', 'error', 'denied', 'stopped')

DRAW_HELP = (
    "Draw a picture in CGA graphics with a GW-BASIC style script, shown full screen "
    "until the user presses a key, then the chat comes back. One rich call is better "
    "than many small ones. Statements, separated by newlines or ':': SCREEN 1 "
    "(320x200, 4 colours, the default) or SCREEN 2 (640x200, 2 colours); COLOR bg,palette "
    "(SCREEN 1: background 0-15, palette 0 = green/red/brown, 1 = cyan/magenta/white); "
    "CLS; PSET (x,y)[,c]; PRESET (x,y)[,c]; LINE [(x1,y1)]-(x2,y2)[,c][,B|BF]; "
    "CIRCLE (x,y),r[,c[,,,aspect]] (full circles/ellipses only); PAINT (x,y)[,c[,border]]; "
    "DRAW \"turtle string\" (U D L R E F G H n, M x,y, B and N prefixes, Cn, Sn, An, TAn); "
    "LOCATE row,col then PRINT \"text\" (25 rows; 40 or 80 columns); SLEEP seconds; REM or ' "
    "comments. STEP(dx,dy) gives relative coordinates. Arguments are numbers with + - * / "
    "and parentheses; there are no variables or loops, so write out repetition. Leading "
    "line numbers are ignored.")

PLAY_HELP = (
    "Play music on the PC speaker using a GW-BASIC PLAY string, e.g. \"T120 O3 L8 CDEFG4\". "
    "Notes A-G with # or + (sharp) and - (flat), optionally followed by a length; "
    "On octave 0-6 (O3 starts at middle C, default O4); < and > change octave; Nn plays "
    "note number 0-84 (0 = rest); Ln default length 1-64 (4 = quarter note); Pn or Rn rest; "
    "Tn tempo 32-255 quarter notes per minute (default 120); a dot after a note makes it 1.5 "
    "times as long; MN normal, ML legato, MS staccato. Esc stops playback.")

# name, class (R read-only, H harmless output, W changes something), description,
# params {key: (description, required)}
TOOLS = [
    {'name': 'sysinfo', 'cls': 'R',
     'description': "Describe the XT: DOS version, total and free memory, EMS, drives with "
                    "free space, BIOS date, video adapter, date and time, IP address.",
     'params': {}},
    {'name': 'dir', 'cls': 'R',
     'description': "List a directory: names, sizes, dates, attributes. DOS 8.3 names.",
     'params': {'path': ("Directory or wildcard pattern, e.g. C:\\ or C:\\DOS\\*.EXE", True)}},
    {'name': 'read', 'cls': 'R',
     'description': "Read a file. Text files come back as numbered lines; binary files as a "
                    "hex dump. Keep reads targeted: the link is slow.",
     'params': {'path': ("File to read, e.g. C:\\AUTOEXEC.BAT", True),
                'start': ("Text: first line number (default 1)", False),
                'lines': ("Text: how many lines (default 100)", False),
                'offset': ("Binary: first byte offset (default 0)", False),
                'bytes': ("Binary: how many bytes, up to 256 (default 256)", False)}},
    {'name': 'peek', 'cls': 'R',
     'description': "Hex and ASCII dump of memory, e.g. 0040:0000 is the BIOS data area, "
                    "F000:FFF5 the BIOS date, B800:0000 the screen.",
     'params': {'addr': ("Address as hex seg:off, e.g. 0040:0000", True),
                'len': ("Bytes to dump, up to 256 (decimal or 0x hex; default 64)", False)}},
    {'name': 'write', 'cls': 'W',
     'description': "Create or overwrite a text file (newlines are written as CRLF), or "
                    "append to one. The user may be asked to approve.",
     'params': {'path': ("File to write; at most 64 characters, 8.3 names", True),
                'text': ("The file's contents", True),
                'append': ("1 to append instead of overwriting", False)}},
    {'name': 'delete', 'cls': 'W',
     'description': "Delete one file (no wildcards; use run with DEL for those).",
     'params': {'path': ("File to delete", True)}},
    {'name': 'mkdir', 'cls': 'W',
     'description': "Create a directory.",
     'params': {'path': ("Directory to create", True)}},
    {'name': 'rename', 'cls': 'W',
     'description': "Rename or move a file within a drive.",
     'params': {'from': ("Existing file", True), 'to': ("New name", True)}},
    {'name': 'run', 'cls': 'W',
     'description': "Run a DOS command line with COMMAND.COM /C (programs, .BAT files, "
                    "internal commands like DIR, TYPE, DEL). The screen switches to DOS while "
                    "it runs; you get the output it printed (or the final screen text) and the "
                    "exit code, which FreeCOM passes through from the program (e.g. 2 for DIR "
                    "of a missing file). Programs cannot use the network, there is no way to "
                    "type into them, and Ctrl-C can't stop them, so avoid interactive or "
                    "endless programs.",
     'params': {'command': ("The command line, at most 120 characters", True)}},
    {'name': 'poke', 'cls': 'W',
     'description': "Write bytes to memory. Careful: wrong addresses can crash the XT.",
     'params': {'addr': ("Address as hex seg:off, e.g. B800:0000", True),
                'bytes': ("Hex byte values separated by spaces, e.g. \"41 1E 42 1E\"", True)}},
    {'name': 'port', 'cls': 'W',
     'description': "Read or write one I/O port byte, e.g. 61 (speaker/keyboard control), "
                    "3D9 (CGA colour select), 3DA (CGA status).",
     'params': {'port': ("Port number in hex, e.g. 3D9", True),
                'value': ("Byte to write (decimal or 0x hex); omit to read", False)}},
    {'name': 'draw', 'cls': 'H', 'description': DRAW_HELP,
     'params': {'script': ("The drawing script", True)}},
    {'name': 'play', 'cls': 'H', 'description': PLAY_HELP,
     'params': {'music': ("The PLAY string", True)}},
    {'name': 'textart', 'cls': 'H',
     'description': "Print coloured text art into the chat transcript (80x25 text mode, code "
                    "page 437 characters such as box drawing and blocks). ~X, with X a hex "
                    "digit, sets the colour (0 black, 1 blue, 2 green, 3 cyan, 4 red, 5 magenta, "
                    "6 brown, 7 grey, 8 dark grey, 9-F bright versions; E yellow, F white); "
                    "~~ is a literal ~. Lines over 78 columns are cut, at most 40 lines.",
     'params': {'text': ("The text, with newlines between lines", True)}},
]

BY_NAME = {t['name']: t for t in TOOLS}

# The argument shown next to the tool name in transcripts.
HEADER_KEY = {'dir': 'path', 'read': 'path', 'write': 'path', 'delete': 'path', 'mkdir': 'path',
              'rename': 'from', 'run': 'command', 'peek': 'addr', 'poke': 'addr', 'port': 'port'}

TOOLS_PROMPT = """\
You also have tools that run on this real IBM PC XT itself (FreeDOS, 8088, CGA): \
read and write files, look at the system, run DOS commands, peek and poke memory \
and ports, draw CGA graphics with a GW-BASIC style script, play music on the PC \
speaker, and print coloured text art. Use them when they help or when asked to do \
something on the machine; each call takes a few seconds on this hardware, so \
prefer one well-filled call to many small ones. Tools that change files, memory \
or ports may be denied by the user; if so, don't retry, say what you would have done."""


@dataclass
class ToolResult:
    status: str
    text: str

    def model_text(self):
        if self.status == 'error':
            return 'Error: ' + self.text
        if self.status == 'denied':
            return 'The user denied this tool call.'
        if self.status == 'stopped':
            return 'Interrupted by the user.'
        return self.text


@dataclass
class ToolCall:
    name: str
    args: dict
    id: str = ''
    result: ToolResult = None


def summary(call):
    """A tool call as one line of text, for transcripts handed to other backends."""
    arg = call.args.get(HEADER_KEY.get(call.name, ''), '')
    out = call.name + (' ' + arg if arg else '')
    if call.result:
        out += ' -> ' + call.result.status
    return f'[{out}]'


def anthropic_tools():
    return [{'name': t['name'], 'description': t['description'],
             'input_schema': {'type': 'object',
                              'properties': {k: {'type': 'string', 'description': d}
                                             for k, (d, _) in t['params'].items()},
                              'required': [k for k, (_, req) in t['params'].items() if req]}}
            for t in TOOLS]


def openai_tools():
    return [{'type': 'function',
             'function': {'name': a['name'], 'description': a['description'],
                          'parameters': a['input_schema']}}
            for a in anthropic_tools()]


def stringify(args):
    """Model-supplied arguments as the strings the XT expects."""
    out = {}
    for k, v in (args or {}).items():
        if v is None:
            continue
        if isinstance(v, bool):
            v = '1' if v else '0'
        out[k] = str(v)
    return out


def escape(s):
    return s.replace('\\', '\\\\').replace('\n', '\\n')


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            nxt = s[i + 1]
            out.append('\n' if nxt == 'n' else nxt)
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def chunks(escaped, room):
    """Split escaped text into pieces whose CP437 encoding fits `room` bytes,
    never between a backslash and the character it escapes."""
    piece, size = '', 0
    i = 0
    while i < len(escaped):
        unit = escaped[i:i + 2] if escaped[i] == '\\' else escaped[i]
        n = len(to_cp437(unit))
        if size + n > room and piece:
            yield piece
            piece, size = '', 0
        piece += unit
        size += n
        i += len(unit)
    yield piece


def call_lines(id, call):
    """The (verb, payload) lines that send `call` to the XT."""
    lines = [('TOOL', f'{id} {call.name}')]
    for key, value in call.args.items():
        room = MAX_PAYLOAD - len(key) - 1
        for piece in chunks(escape(value), room):
            lines.append(('ARG', f'{key} {piece}'))
    lines.append(('CALL', ''))
    return lines
