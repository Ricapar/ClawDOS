# CLAWDOS as a native API client (the bridge becomes a dumb TLS tunnel)

Date: 2026-10-31. Branch: `native-api`.

## Intent

Today the Python bridge (bridge/dosbridge.py) does the real work of the harness:
it holds the API key, builds the request, keeps the conversation history, parses
the model's stream, runs the tool loop and hands the DOS PC pre-digested 500-byte
lines. CLAWDOS.EXE is a terminal with tool executors.

The story we want to tell is the opposite: **the harness runs on the IBM PC and
does all the work itself**. The 8088 talks the Claude Messages API directly, keeps
its own history on its own disk, parses the model's streamed JSON, runs the agent
loop and executes the tools. The only thing it cannot do is TLS (see
docs/ROADMAP.md, "Why a bridge"), so the one piece left off the PC is a **dumb TLS
tunnel**: a program with no knowledge of the API that accepts a plain TCP
connection and forwards the bytes to `api.anthropic.com:443` inside TLS. socat or
stunnel can be that program; we ship a 60-line stdlib Python one for convenience.

```
 DOS PC (CLAWDOS.EXE)           --plain HTTP over TCP-->   TLS tunnel        --HTTPS-->  Claude API
  UI, history on disk, request                             (socat/stunnel/
  building, SSE + JSON parsing,                             tunnel.py: just
  agent loop, tools, API key                                copies bytes)
```

Native TLS on the 8088 (BearSSL) or a tunnel inside the PicoMEM firmware can
later replace the tunnel without touching CLAWDOS: both deliver the same plain
socket to the same HTTP client.

## Goals

- CLAWDOS.EXE speaks the Anthropic Messages API (streaming) itself: request
  body, headers, SSE events, content blocks, tool_use/tool_result, stop reasons,
  usage, errors and retries.
- The conversation history lives on the DOS PC's disk, in the API's own message
  format, and is streamed from there into each request so no turn ever needs it
  all in RAM.
- The system prompt, the tool schemas and the provider settings are files on the
  DOS PC that the user (or the model, via its tools) can read and edit.
- Everything a user sees today keeps working: streaming replies, word wrap,
  CP437 rendering of Unicode, tools with the safe-mode prompt, memories in the
  system prompt, `/new`, `/help`, `/theme`, `/model`, Esc to stop, the clock set
  on connect, `-m`/`-i`/`-q`/`-unsafe`/`-noscroll`, and the lost-link recovery.
- Every boot test keeps a working equivalent, run against a mock API server
  instead of the mock bridge.
- The Python that remains has no application logic: a byte-copying tunnel and a
  test mock. No third-party packages, so no `.venv`.

## Non-goals (for this design)

- OpenAI-style `/chat/completions` providers. The config format leaves room for
  them (a provider `type`), but only `type anthropic` is built. Many hosts expose
  an Anthropic-compatible `/v1/messages` (OpenRouter does), so `/model` still
  switches between providers and models that speak it.
- Native TLS or a PicoMEM-side tunnel. Enabled by this design, not part of it.
- Images, the bridge-side git history and the other roadmap items that assumed a
  smart bridge (M6.x). They need a new home; not here.
- Server-side refusal fallbacks (the `server-side-fallback` beta the bridge
  sends). See The request for why; it can return later as a non-streaming
  special case if refusals turn out to matter.
- `--replay LOG` (bridge/replay.py). It replayed bridge-protocol logs, which stop
  existing. A mock scenario can re-create what it was used for later.

## Anti-goals

- A "thin" bridge that still knows the API, the key or the history. If the tunnel
  needs a change when the API changes, the design has failed.
- Holding the history in conventional memory. 640K minus DOS, mTCP and CLAWDOS
  leaves about 300K; a long coding session with tool results is bigger than that.
- Slowing the screen. Streaming text must still appear as it arrives; JSON and
  UTF-8 decoding happen per delta, not per reply.

## Constraints

- 8086 code (`wcc/wpp -0`), Watcom large model, no quoted `-D` values. CLAWDOS.EXE
  is 105 KB today with a 43 KB near data segment; the new code must leave the
  `run` tool enough memory to launch compilers (EMS parking, lib/ems.c, remains).
- mTCP sockets (GPLv3) are the only network API. One TCP connection to the
  tunnel, kept open across turns with HTTP keep-alive; the tunnel keeps one TLS
  connection per client connection.
- DOS 8.3 filenames. All CLAWDOS data under `C:\CLAWDOS\`.
- The API key passes in clear between the PC and the tunnel (LAN/WiFi) and sits
  on the microSD card. Accepted for a hobby machine; the README tells users to use
  a dedicated key with a spending limit.

## Design

### Files on the DOS PC

| File | What |
|---|---|
| `C:\CLAWDOS\CLAWDOS.CFG` | Settings and providers (below). Written by the image build from the repo template plus the local secrets. |
| `C:\CLAWDOS\PROMPT.TXT` | The complete system prompt with `{name}` and `{memories}` placeholders. Replaces bridge/prompts/default.txt and the TOOLS_PROMPT / MEMORY_PROMPT add-ons, which move into this file. Source: `image/clawdos/PROMPT.TXT`. |
| `C:\CLAWDOS\TOOLS.JSN` | The `tools` array of the request, verbatim. Replaces the schemas in bridge/tools.py. Source: `image/clawdos/TOOLS.JSN`, the single source of truth; the mock reads the same file. |
| `C:\CLAWDOS\CHATS\*.JSN` | One file per conversation: the `messages` array's elements, in the Anthropic format (below). Also the conversation log. |
| `C:\CLAWDOS\CLAWDOS.LOG` | Append-only diagnostics: one line per API round (date, provider, model, ms to first token, ms total, stop reason, tokens in/out/cache read/cache write) and one per note that used to go to the bridge's log as a `LOG` line (clock set, link lost, reconnects, retries, tool call errors). Replaces bridge/convlog.py's usage events and the bridge log. |
| `C:\MEMORY\*.TXT` | Unchanged. |

`CLAWDOS.CFG` is line-oriented like MTCP.CFG (`key value`, `#` comments), with
`[name]` sections for providers:

```
# ClawDOS settings
provider anthropic          # the [section] used at startup
theme claude
timezone -5                 # hours from UTC, for the clock set from the HTTP Date header
name Claude                 # {name} in PROMPT.TXT (a provider may override)

[anthropic]
type anthropic
tunnel 192.168.1.10:8443    # where the TLS tunnel listens; "plain" host:port in tests
host api.anthropic.com      # the Host header the tunnel's far end expects
path /v1/messages
key sk-ant-...              # or: keyfile C:\CLAWDOS\ANTHROP.KEY
model claude-opus-5-5
effort low
# auth x-api-key (default) or bearer; betas adds an anthropic-beta header

[openrouter]
type anthropic
tunnel 192.168.1.10:8444
host openrouter.ai
path /api/v1/messages
auth bearer
key sk-or-...
model anthropic/claude-opus-5.5
name Claude
```

Every line of a provider section except `type`, `tunnel`, `host`, `path`, `key`
and `model` is optional, and the request only carries what is set: no `effort`
line, no `output_config`; no `betas`, no `anthropic-beta` header. `cache_control`
is sent unless `cache 0`. That is how an `[openrouter]`-style section works on
day one without the writer knowing anything about OpenRouter.

`CLAWDOS_BRIDGE`, `CLAUDE_TOKEN` and `CLAWDOS_THEME` leave MTCP.CFG. On the
command line, `CLAWDOS [host:port]` now means "reach the current provider at this
address instead of its `tunnel` line", which is how the boot tests point it at the
mock on `10.0.2.2:18023`. `-p NAME` picks a provider section.

The image build (scripts/build.py) writes `CLAWDOS.CFG` from
`image/clawdos/CLAWDOS.CFG` (the template, committed, no key) and the local
`.env` (gitignored, already holds `ANTHROPIC_API_KEY`): `--tunnel host:port`
replaces `--bridge`, remembered in build/settings.json as today, and the key from
`.env` is filled into the `[anthropic]` section. A build without a key still
boots; CLAWDOS says which file to edit.

### The request

One `POST {path} HTTP/1.1` per API round, on the open connection, with
`Host`, `x-api-key` (or `Authorization: Bearer` with `auth bearer`),
`anthropic-version: 2023-06-01`, `anthropic-beta` (only with `betas`),
`Content-Type: application/json`, `Content-Length`, `Connection: keep-alive`,
`Accept: text/event-stream`. The body is produced by a writer that walks a fixed
sequence of parts, some conditional on the config, and either counts or sends
them, so the length is known without buffering:

```
{"model":"...","max_tokens":16000,"stream":true,
 "output_config":{"effort":"low"},                 if effort is set
 "cache_control":{"type":"ephemeral"},             unless cache 0
 "tool_choice":{"type":"auto","disable_parallel_tool_use":true},
                                                   {"type":"none"} on the 26th round
 "system":[{"type":"text","text":<PROMPT.TXT with substitutions, JSON-escaped>,
            "cache_control":{"type":"ephemeral"}}],
 "tools":<TOOLS.JSN verbatim>,
 "messages":[<CHATS file verbatim>]
}
```

`disable_parallel_tool_use` is deliberate: the model then makes at most one tool
call per round, so one arguments buffer suffices and the tools run in the order
the user sees them. It costs a request per call instead of per batch of calls;
on this machine the tools were always run one at a time anyway. No `thinking`
field is sent: the models we use think adaptively by default and `effort` is the
only control. The bridge's server-side fallback beta (`fallbacks: "default"`) is
**not** carried over: a fallback mid-reply requires dropping blocks that were
already written before the fallback marker, which an append-only history cannot
do. A refusal therefore ends the turn (below) instead of being retried on
another model.

`{memories}` is the output of the local `memory_list` executor, taken once per
conversation as the bridge did; `{name}` from the provider or global `name`. The
prompt file is escaped on the fly (one pass counts, one sends), so it is never
held in memory either. Prompt caching keeps the two breakpoints the bridge uses:
system (covers tools + system) and the top-level one that follows the growing
conversation.

### The history file

A conversation file holds the elements of `messages`, each a complete JSON
object, separated by `,\r\n`; the request writer puts the brackets around it.
Everything in it is UTF-8 JSON exactly as the API wants it, so sending it is a
copy. CLAWDOS writes:

- the user's text: `{"role":"user","content":"..."}` (CP437 → UTF-8, escaped);
  after tool results it is appended as a text block to the pending user message,
  since roles must alternate (the bridge's `_add_user`).
- the assistant message, assembled from the stream, block by block in the order
  they arrive: `text` blocks (the raw JSON-escaped `text_delta` fragments
  concatenated, which is valid because each delta is a complete JSON string),
  `thinking` blocks (`thinking_delta` fragments, then the `signature_delta` as
  `"signature"`), `redacted_thinking` blocks (the `data` from the block start)
  and `tool_use` blocks (`id`, `name`, and the concatenated `input_json_delta`
  fragments as `input`). Thinking blocks are never shown, but they **must** be
  echoed back unchanged on later rounds ("preserved thinking": stripping them
  can make the next request a 400), which is why the writer copies every block
  type it knows and the file holds exactly what the API sent. A block of a type
  the writer does not know ends the turn with an error rather than being
  silently dropped.
- tool results: `{"role":"user","content":[{"type":"tool_result","tool_use_id":
  "...","content":"...","is_error":true}, ...]}` with the status texts the bridge
  used (`Error: ...`, `The user denied this tool call.`, `Interrupted by the
  user.`).

The file is append-only within a turn, and a turn remembers the offset it
started at. Anything that ends a turn early (Esc, a dropped link, an API error,
a refusal) **truncates the file back** to the last complete message boundary and
then applies the bridge's `_settle` rules: no assistant message this turn → back
to the turn start (the user's text is kept in the input box for another try); an
assistant message with unanswered `tool_use` blocks → append `stopped` results so
the next request is valid. A reply cut by `max_tokens` keeps what arrived and
shows `[reply cut off: too long]`, as today.

Conversation files are named `MMDDnnn.JSN` (month, day, sequence) so they sort by
date. `/new` starts a new file. Nothing deletes them; they are the logs.

### The response

The HTTP client reads the status line and headers, then the body, which is
`Transfer-Encoding: chunked` and `text/event-stream`. Layers, each a small C
module in lib/ with unit tests in tests/unit/dosunit.c:

1. **http.c**: request writing with the counting pass, status/header parsing
   (`Content-Length`, `Transfer-Encoding`, `Date`, `Connection`, `retry-after`),
   chunked decoding, a line reader over the body. Reads in 2 KB pieces through
   mTCP, keeps the connection open after the body unless the server closed it.
2. **sse.c**: splits the body into events (`event:` and `data:` lines, blank line
   ends an event). One `data:` line fits in 16 KB (a `signature_delta` carries a
   base64 blob of a few KB; text and JSON deltas are small); a longer one is an
   error for the turn ("event too long"), which the mock exercises.
3. **json.c**: a non-recursive tokenizer for small objects (events are a few
   hundred bytes; the largest are a `signature_delta` and a `content_block_start`
   for a tool_use). Gives
   key lookup by path (`delta.text`, `delta.partial_json`, `message.usage.
   input_tokens`, `error.message`), string unescaping including `\uXXXX` and
   surrogate pairs into UTF-8, and escaping for the writer. Also parses a
   completed tool_use `input` object into the `key=string` pairs the executors
   take (numbers in decimal, booleans `1`/`0`, as bridge/tools.py `stringify`).
4. **utf8.c**: UTF-8 → CP437 with the fallback table from bridge/cp437.py
   (quotes, dashes, arrows, ticks, `...`; unknown → `?`; NFKD stripping of
   accents is replaced by a table of the Latin-1 letters CP437 lacks), and
   CP437 → UTF-8 (a 256-entry table) for what we send.

The events we act on: `message_start` (usage), `content_block_start` (text,
thinking, redacted_thinking or tool_use: id, name), `content_block_delta`
(`text_delta` → decode, write to the console and to the history;
`thinking_delta` and `signature_delta` → history only, with the spinner showing
"thinking"; `input_json_delta` → append to the arguments buffer and the
history), `content_block_stop` (a block is complete), `message_delta`
(stop_reason, output usage), `message_stop`, `error`, `ping`. Unknown events and
unknown keys are ignored; an unknown block type is an error (above).

The arguments buffer grows from today's 6 KB to 16 KB in far memory (the `edit`
tool's `old`/`new` and `write`'s `text` are the big ones); a tool call whose
input exceeds it is answered with an error result, not dropped.

### The agent loop

On `message_stop` with `stop_reason tool_use`, each tool_use block is run in
order exactly as src/clawdos/tools.cpp runs a `CALL` today (header in the
transcript, safe-mode question, executor, result), then the results message is
appended and the next round's request goes out. Up to 25 rounds; the 26th sends
`tool_choice none` plus the "Tool limit reached" note, as the bridge does. The
`run` tool's screen switch and EMS parking are unchanged; between rounds the
connection is serviced (ACKs) so the tunnel's socket stays alive, which replaces
today's PING/PONG.

Esc: the DOS side closes the TCP connection (there is no cheaper way to cancel
an HTTP request), truncates the history as above, and reconnects on the next
submit. A tool call in progress is stopped as today.

### Errors, retries, lost links

- HTTP 429, 529, 500-599, or a TCP error before any body byte: retry the same
  request after 2 s, then 8 s (two retries, like the SDK); `retry-after` wins if
  present. A note in the transcript on each retry.
- 400/401/403/404 and `error` events: show `error.message`, end the turn,
  truncate.
- The link drops mid-stream: what was shown stays on screen, the partial
  assistant message is dropped from the file, and the request is sent again on a
  new connection with a `[link lost, asking again]` note, **once per turn**; a
  second drop ends the turn with an error. The model answers from scratch; that
  is the price of a stateless API and it is simpler than today's RESUME, which
  disappears along with session ids. Each reconnect is logged as
  `connection #n`, which drop.bat watches for.
- `stop_reason refusal`: "The model declined to answer that." and the user
  message is removed from the file (bridge behaviour).

### The clock

The response's `Date` header is RFC 1123 GMT; `timezone` from CLAWDOS.CFG gives
local time, and the DOS clock is set once per run when it is clearly wrong (year
< 2020), with the same transcript note as today ("Clock set from the bridge"
becomes "Clock set from the API"). The note also goes to CLAWDOS.LOG, which is
how the clock boot test sees it (Testing, below).

### `/model`

`/model` lists the `[sections]` of CLAWDOS.CFG, marking the current one and
whether it has a key. `/model NAME` or `/model NAME:MODEL` or `/model MODEL`
switches; the history file is in the Anthropic format already, so switching
between `type anthropic` providers keeps the conversation and only reconnects
to the other tunnel address. `/model` output is the client's own, printed like a
reply.

### What the Python becomes

`bridge/` is replaced by `tunnel/`:

- `tunnel/tunnel.py`: asyncio + ssl, stdlib only. `--map 8443=api.anthropic.com:443`
  (repeatable; that one is the default). Each client connection gets its own TLS
  connection; bytes are copied both ways; logging is connections only, never
  bytes (the key passes through). The README also shows the one-line `socat` and
  `stunnel` equivalents so nobody has to run our code.
- `tunnel/mockapi.py`: a plain-HTTP fake of `/v1/messages` with streaming, for
  the boot tests and for offline UI work. It keeps every `!scenario` of
  bridge/mockchat.py (`!scroll`, `!wide`, `!cp437`, `!error`, `!drop`, `!cutoff`,
  `!refuse`, `!stall`, `!tool`, `!tooltour`, `!run`, `!play`, ...) and adds
  `!retry` (a 529 then success), `!chunks` (chunked encoding split at awkward
  places, including inside UTF-8 sequences and JSON escapes) and `!bigevent` (a
  data line over 8 KB). It reads `image/clawdos/TOOLS.JSN` for `!tooltour`.
  Scenario text is served through the same SSE/JSON path a real reply takes, so
  the tests exercise the parsers.
- `tunnel/test_mockapi.py`: unit tests for the mock's SSE framing and for the
  JSON of each scenario, with Python's own parser as the reference.

`bridge/dosbridge.py`, `providers.py`, `tools.py`, `prompts/`, `cp437.py`,
`convlog.py`, `replay.py`, `chat.py`, `mockchat.py`, `test_bridge.py`,
`requirements.txt` and `providers.example.toml` are deleted once the native
client passes the boot tests. docs/PROTOCOL.md goes too; its replacement,
docs/API.md, documents what CLAWDOS sends and expects (this section, kept
current), the config file and the tunnel.

### Testing

- `tests/unit/dosunit.c` (`image/tests/units.bat --expect UNITS-OK`): json
  tokenizer and unescape (including `\u` surrogates), escape round trips, UTF-8 ↔
  CP437 including fallbacks, chunked decoding across buffer boundaries, SSE event
  splitting, request length counting equals bytes sent, history truncate and
  settle rules, CLAWDOS.CFG parsing, Date header parsing.
- Boot tests: `--bridge` becomes `--mock` in scripts/boottest.py and runs
  `tunnel/mockapi.py` on 18023. The mock's log still goes into the test log, but
  CLAWDOS's own notes no longer pass through it: every test batch file ends with
  `TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1` so `--expect` sees them on the serial
  log. tools.bat, drop.bat, memory.bat, clock.bat keep their `--expect` strings
  (clock's changes to the new note; the tool tour's PASS line is printed by the
  mock, which checks the results it gets back as today). New: `retry.bat`
  (`!retry`, expects the second attempt's text), `chunks.bat`, `bigevent.bat`
  (expects the error note and a usable prompt afterwards).
- `make test` runs `tunnel/test_mockapi.py` with the system `python3` (no venv).
- Real API: `make tunnel` on the Mac, DOSBox-X with `--tunnel 10.0.2.2:8443`,
  then 86Box for the 8088 numbers that matter: time from Enter to first token on
  a fresh conversation and on one with 100 KB of history. Those two measurements
  go in the ROADMAP when the branch lands.

### Memory and size budget

Expected additions: http + sse + json + utf8 + cfg + history ≈ 20-30 KB of code
and 4 KB of near data; buffers in far heap: 8 KB receive line, 2 KB socket read,
16 KB arguments. Removed: lib/dosbridge.cpp's protocol half and the TOOL/ARG/
CALL/OUT/DONE handling in tools.cpp (the escaping and chunking disappear). Net:
CLAWDOS.EXE around 125 KB, 300 K minus about 30 K free for `run`. If it ends up
tighter than that, the arguments buffer moves to the EMS page.

## Decisions made here (the builder may not revisit)

- Anthropic Messages API, streaming, as the one wire format; history stored in
  that format.
- History on disk, append-only, truncate-to-boundary on failure.
- Prompt, tools and config as plain files on the PC.
- Content-Length by a counting pass, never chunked requests.
- One keep-alive connection; Esc closes it.
- One tool call per round (`disable_parallel_tool_use`); no fallbacks beta.
- Every content block type the API sends is stored and echoed back; thinking is
  never displayed.
- CLAWDOS.LOG is the diagnostics channel the boot tests read.
- Tunnel is byte-copying only; socat/stunnel must work as drop-ins.
- Python left behind has no dependencies and no application logic.

## Left to the builder

- Module boundaries inside lib/ and how the UI state machine in clawdos.cpp
  changes (WAITING/STREAMING states stay; the line handler becomes an event
  handler).
- Exact buffer sizes within the budget above, and whether `sse.c` and `http.c`
  are one module.
- The order of work, but the old bridge path should keep building until the new
  one passes `image/tests/tools.bat`, then be deleted in one commit.
- The exact fallback table entries for UTF-8 → CP437 beyond those in cp437.py.
- How build.py edits the config template (string replace is fine).
