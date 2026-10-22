# XT tool calls: design

Date: 2026-10-21. Status: approved in brainstorming, awaiting spec review.

## Goal

Let the model use tools that run on the IBM PC XT itself, through CLAUDE.EXE:
browse and edit files, inspect the system, run DOS programs, draw CGA
graphics, play music on the PC speaker, and poke at hardware. The tool set is
fixed and hard-coded. Success: a request like "draw me a sunset and play a
tune" or "look at my AUTOEXEC.BAT and fix the PATH" makes Claude chain several
tool calls on the real machine, each one visible in the transcript, with
nothing destructive happening without consent while safe mode is on.

## Decisions

- The tool loop lives in the bridge. The XT only executes calls and returns
  results; it keeps no tool history.
- The bridge owns the tool schemas (names, descriptions, parameters). The XT
  has a dispatch table and executors.
- No backwards compatibility: no version or capability handshake. The bridge
  always offers every tool. `HELLO` is unchanged.
- Backends: Anthropic and OpenAI-compatible providers, plus scripted tool
  calls in the mock. `/model` switching flattens past tool turns to text.
- Safe mode, toggled with F3, is on at startup. Read-only (R) and harmless (H)
  tools never prompt. Tools that change things (W) prompt y/n when safe mode
  is on and run freely when it is off.
- `run` shells out visibly and also captures the output.
- `draw` takes a GW-BASIC-style script, one call per picture.
- `play` takes a GW-BASIC PLAY string.
- Programs started by `run` cannot use the network: our mTCP stack owns the
  packet driver.

## Protocol additions (docs/PROTOCOL.md gets these during implementation)

Server to client:

| Line | Meaning |
|---|---|
| `TOOL <id> <name>` | Start of a tool call. `id` is short (`t1`, `t2`, ...); the bridge maps it to the API's id. |
| `ARG <key> <chunk>` | Argument text, escaped. Repeated lines for the same key append. |
| `CALL` | All arguments sent; execute now. |

Client to server:

| Line | Meaning |
|---|---|
| `OUT <chunk>` | Result text, escaped. Repeats. |
| `DONE <id> <status>` | Call finished. Status is `ok`, `error`, `denied` (the user said no) or `stopped` (Esc). |

Escaping, in `ARG` and `OUT`: `\n` is a newline, `\\` a backslash. Every
argument is a string: the bridge writes numbers in decimal and booleans as
`1`/`0`, and the schemas have no arrays or objects. Chunks keep server lines
within 500 bytes. A tool's result is capped at 4 KB on the XT.

Example:

```
S: TXT Let me look at your startup file.
S: TOOL t1 read
S: ARG path C:\AUTOEXEC.BAT
S: ARG lines 40
S: CALL
C: OUT    1 @ECHO OFF\n   2 PATH C:\DOS;C:\NET\n...
C: DONE t1 ok
S: NL
S: TXT Your PATH is missing ...
S: END
```

Calls run one at a time; parallel `tool_use` blocks are serialized by the
bridge. On Esc during a call, the XT sends `DONE <id> stopped` then `STOP`.
The bridge records a tool_result saying the user interrupted, keeping the
history valid, and sends `END stopped`.

## Tool catalog

Class: R read-only, H harmless output (screen or sound), W changes something.

| Tool | Class | Args | Returns / summary |
|---|---|---|---|
| `sysinfo` | R | none | DOS version, free/total RAM, EMS, drives with free space, BIOS date, video adapter, date/time, IP address |
| `dir` | R | `path` (wildcards OK) | name, size, date, attributes; at most ~150 entries plus a "+N more" line |
| `read` | R | `path`, `start`, `lines`; for binary files `offset`, `bytes` (up to 256) | numbered text lines, or a hex dump for binary files |
| `peek` | R | `addr` (`seg:off` hex), `len` up to 256 | hex and ASCII dump |
| `write` | W | `path`, `text`, `append` | bytes written; creates or overwrites; newlines written as CRLF |
| `delete` | W | `path` | one file, no wildcards |
| `mkdir` | W | `path` | ok or DOS error |
| `rename` | W | `from`, `to` | ok or DOS error |
| `run` | W | `command` (at most 120 characters) | captured output (or a screen snapshot), exit code |
| `poke` | W | `addr`, `bytes` (hex) | bytes written |
| `port` | W | `port`, optional `value` | reads or writes one byte. Port reads are W because they can have side effects. |
| `draw` | H | `script` | "drawn, viewed Ns", or an error with line number |
| `play` | H | `music` | duration, or "stopped by user" |
| `textart` | H | `text` with `~X` colour codes | prints coloured CP437 text into the transcript |

Paths longer than 64 characters are refused. DOS error codes are reported as
words ("file not found", "access denied", "disk full").

## Bridge

- `bridge/tools.py` (new): the schemas, plus a one-line description and
  string parameters per tool. It also has `to_anthropic()` / `to_openai()` and
  the escape/unescape and chunking helpers.
- `providers.py`: `AnthropicChat.reply()` and `OpenAIChat.reply()` yield
  events, either text chunks or `ToolCall(id, name, args)`. Each tool result
  goes back into the chat's history (Anthropic `tool_result` blocks, OpenAI
  `role: tool` messages), and the model is called again. Each user message is
  capped at 25 tool rounds. At the cap, a last call goes out with
  `tool_choice` none and a note asking the model to wrap up.
- `xtbridge.py`: for a `ToolCall`, `Session.answer()` sends
  `TOOL`/`ARG`/`CALL` and awaits a future. `Session.command()` handles
  `OUT` (append to the pending result) and `DONE` (resolve the future).
  `OUT`/`DONE` with no pending call are ignored and logged.
- The system prompt gains a short paragraph:
  - Claude is running on the real machine.
  - `draw` uses GW-BASIC syntax; one rich `draw` call is better than many
    small ones.
  - File reads should be targeted.
  - W tools may be denied by the user.
- `transcript()` flattens tool turns, e.g. `[read C:\AUTOEXEC.BAT -> 24 lines]`.
- Errors:
  - A dropped connection mid-tool cancels the turn, and the history gets a
    synthetic tool_result.
  - An API error mid-loop sends `ERR` and trims the history back to the last
    consistent point.
- `bridge/chat.py` answers every `TOOL` with `DONE <id> error` ("this client
  has no tools"), so it never hangs the bridge.
- `mockchat.py` gains scenarios that issue scripted calls:
  - `!tool NAME key=value ...` calls any one tool, with shell-style quoting
    for values.
  - `!draw` and `!play` are demos.
  - `!tooltour` calls every tool in turn, checks the results, and logs
    `tool tour: PASS` or `tool tour: FAIL ...`.
  All of them report the results they get back.

## CLAUDE.EXE

- New `TOOL` state.
  - Arguments are collected into a 6 KB key/value buffer, unescaped on arrival.
  - On `CALL`, the tool is looked up in a static table
    `{name, display name, header arg key, class, executor}`.
    - An unknown name returns `DONE <id> error`.
  - Executors fill a 4 KB result buffer and return a status plus a short
    summary string.
- Code layout:
  - `src/claude/tools.cpp` holds the dispatch, display and permission logic.
  - Executors live in `lib/`:
    - `dosfs` for the file tools
    - `sysinfo`
    - `gfx` for the CGA primitives
    - `basdraw` for the draw parser
    - `sound` for the PLAY parser and the speaker
    - `shell` for `run` and its output capture
  - `peek`, `poke`, `port` and `textart` are small enough to live in
    `tools.cpp`.
- Transcript:
  - Each call prints `● Name(header arg)` (truncated to one line), then
    `  └ summary` through `note()`.
  - Errors are red, and so is "Denied by user".
  - The full result goes only to the model.
  - `write` previews under the header, e.g.
    `  12 lines, 340 bytes, overwrites existing file`.
- Permission:
  - With safe mode on, a W call turns the status row into
    `Allow Name(arg)? (y/n)`.
  - y runs the call, n sends `denied`, Esc sends `stopped` plus `STOP`.
    No other keys are accepted.
- Safe mode:
  - F3 toggles it, and the hint row shows `F3 safe:on` (green) or
    `F3 safe:off` (red).
  - Each toggle also prints a note in the transcript.
  - `-unsafe` starts with safe mode off, mainly for unattended autotests.
  - `-q` (unattended) quits after the `-m` message's reply ends, and shortens
    the hold times for `run` and `draw`.
- CLAUDE.EXE moves from the compact to the large memory model: its code
  segment is already about 63.5 KB of the 64 KB the compact model allows.
- While a tool runs:
  - The spinner shows a tool verb (Reading, Running, Drawing, Playing, ...).
  - Esc is honoured wherever an executor can poll the keyboard: between
    notes, statements and directory entries. A program under `run` handles
    its own Ctrl-C.
  - The network is not polled; the bridge waits, and TCP retransmits.
- Memory: the 6 KB argument and 4 KB result buffers are allocated once. The
  16 KB VRAM save buffer is a far allocation made only for `run` and `draw`,
  and freed afterwards.
- `F1` help and the welcome tips mention F3.

## Executors

**Screen save/restore (`lib/screen`).** `scr_suspend(buf)` copies all 16 KB
of CGA memory to a far buffer, records the CRTC start address, footer and
cursor, and sets a clean mode 3. `scr_resume(buf)` restores text mode,
memory, start address and cursor, so scrollback survives. On EGA/VGA
(software scrolling) both calls just cover the visible 4 KB page.

**`run` (`lib/shell`):**
1. Suspend the screen and print `C:\> command`.
2. Hook INT 10h AH=0Eh (BIOS teletype). DOS console output (INT 21h
   functions 02h, 09h, 40h, and INT 29h) ends up there, as does BIOS-level
   program output, so each character is captured exactly once. Hooking
   INT 21h and INT 29h as well would capture console output twice. The hook
   copies the character (dropping CR, undoing backspace), then chains to the
   original handler.
3. Spawn `%COMSPEC% /C command`.
4. Unhook and get the exit code from INT 21h AH=4Dh. Whether FreeCOM passes
   the child's errorlevel through is checked during implementation, and the
   tool description states what the model can rely on.
5. If nothing was captured, the result is an 80x25 text snapshot of the
   screen instead.
6. Show `-- exit code N - any key returns to Claude --`. It continues on
   its own after 5 s, or 1 s with `-q`.
7. Resume the screen.

If the output overflows 4 KB, the first 1 KB and last 3 KB are kept, with an
`...(N bytes omitted)...` marker between them. During implementation, verify
how much memory the child gets, and shrink CLAUDE.EXE's allocation if needed.

**`draw` (`lib/basdraw` parses, `lib/gfx` draws):**
- Syntax:
  - Statements are separated by newlines or `:`.
  - Leading line numbers are ignored.
  - `'` and `REM` start comments.
  - Arguments are integer literals with `+ - * /` and parentheses.
  - No variables or loops.
- Statements:
  - `SCREEN 1|2`, `COLOR bg,palette`, `CLS`
  - `PSET` / `PRESET (x,y)[,c]`, with `STEP` for relative coordinates
    everywhere
  - `LINE [(x1,y1)]-(x2,y2)[,c][,B|BF]`
  - `CIRCLE (x,y),r[,c[,,,aspect]]`: full circles and ellipses only
  - `PAINT (x,y)[,c[,border]]`: a scanline flood fill with a bounded
    explicit stack, no recursion
  - `DRAW "..."`: the turtle language U D L R E F G H M B N C S A, plus TA
    if it's cheap
  - `LOCATE r,c` and `PRINT "text"`, drawn through the BIOS in graphics mode
  - `SLEEP n`
- After the script, the picture stays up until a key is pressed, or 3 s
  with `-q`.
- A parse or range error stops the script and is reported as
  `line N: STATEMENT: message`. Everything drawn up to that point stays on
  screen.

**`play` (`lib/sound`):**
- Notes A to G with `# + -`, plus `O`, `< >`, `N`, `L`, `T`, `P`/`R`,
  dotted notes, and `MN`/`ML`/`MS`. `MF`/`MB` are accepted and ignored.
- Tones come from PIT channel 2, gated through port 61h.
- Timing reads the PIT counter, not the 55 ms BIOS tick.
- Esc is checked between notes, and the speaker is turned off on every exit
  path.

**File tools (`lib/dosfs`).** Thin INT 21h wrappers. `read` treats a file as
binary if its first 512 bytes contain NULs or many other control characters.

**`textart`.** Writes through `con_*`. `~X` (X a hex digit) sets the
foreground colour, and `~~` is a literal tilde. Lines are clipped at 78
columns, and the output is capped at 40 lines.

## Testing

- **Bridge (`bridge/test_bridge.py`):**
  - Escape and chunk round-trips.
  - The tool loop against fake Anthropic and OpenAI streams, covering a
    single call, parallel calls, deny, stop, the round cap, a disconnect,
    and an API error mid-loop.
  - Mock scenarios end to end over a socket, with a scripted fake XT client.
- **Parser unit tests (`tests/unit/`):** `basdraw` and the PLAY parser are
  plain C with no hardware access, written against a recording backend. They
  build into `CXTUNIT.EXE`, which `image/tests/units.bat` runs in DOSBox-X
  with results on COM1. The installed Watcom has no Win32 libraries, and there
  is no other host C compiler.
- **DOSBox-X end to end:** `image/tests/tools.bat` runs
  `CLAUDE -unsafe -q -m "!tooltour"` against the mock bridge, which
  `scripts/boottest.py --bridge` starts. The bridge logs each `DONE` status,
  and boottest asserts on that log.
- **86Box by eye:** `draw` output, `play` timing and CGA snow, checked with
  `scripts/winshot.ps1`.

## Out of scope

Text search (Claude can run `FIND`), setting the date or time, floppy
access, a persistent multi-call canvas, variables and loops in `draw`, and
network use by programs started with `run`.
