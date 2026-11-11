# What CLAWDOS sends to the API, and what it expects back

HARNESS.EXE is a client of the Anthropic Messages API. The only thing between it
and `api.anthropic.com` is a TLS tunnel that copies bytes (below). This page is
the reference for the DOS side: the files it uses, the request it writes, the
stream it reads and how the history stays valid.

## Files on the DOS PC

| File | What |
|---|---|
| `C:\CLAWDOS\CLAWDOS.CFG` | Settings and providers (below). The image build fills in the tunnel address and the key from `.env` (with `build.py --no-key`, no key and `auth tunnel`; not in the test images `boottest.py` and `test8088.py` build: the mock needs no key). |
| `C:\CLAWDOS\PROMPT.TXT` | The whole system prompt. `{name}` becomes the name of the model behind ClawDOS (the assistant calls itself ClawDOS), `{memories}` the output of `memory_list` (or `(none)`), `{skills}` one line per `C:\SKILLS\*.TXT`: its whole path, size and first line (or `(none)`). Each list holds at most 4,096 bytes; a longer one ends on a whole line and then `(more memories, not listed)` or `(more skills in C:\SKILLS, not listed)`. CP437 text; CRLF or LF. Read when a conversation's first request is made (into its `.PRE`), so an edit applies to the next conversation. |
| `C:\CLAWDOS\TOOLS.JSN` | The request's `tools` array, verbatim. The single source of the tool schemas; `tunnel/mockapi.py` reads it too. The repo's copy is laid out for reading; the image build writes it minified, without the whitespace (about 1.7 KB less in every request). Every tool sets `eager_input_streaming` so a big argument (a file for `write`) streams in small `input_json_delta` fragments instead of arriving as one event, which the 16 KB event buffer could not hold. |
| `C:\CLAWDOS\CHATS\MMDDnnn.JSN` | One file per conversation: the elements of `messages`, each a complete JSON object, separated by `,\r\n`. |
| `C:\CLAWDOS\CHATS\MMDDnnn.PRE` | The conversation's frozen prefix: `"system":[...],"tools":[...]` exactly as every request of it sends them, rendered from PROMPT.TXT (with `{name}`, `{memories}` and `{skills}` as they were then), the conversation's cache settings (`cache`, `cachettl`) and TOOLS.JSN by the conversation's first request. A resumed HARNESS (after a relaunch) and a reopened chat (F6, `-c`) use it as it is; if it is missing it is rendered again. Esc while the memories or skills are listed for it stops the turn and writes none. Written as `.PR$` and renamed, so it is whole or absent. |
| `C:\CLAWDOS\CLAWDOS.LOG` | A line per API round (`api <provider> <model> round= ttft= total= up= len= stop= in= out= cr= cw= ev= th=`): times in ms from the start of the request (`ttft` to the first text, `total` to the end, `up` to the last body byte handed to the socket, so `ttft - up` is the wait for the API), `len` the body's Content-Length, both for the attempt that got the reply, `ev` the longest SSE event, `th` what that attempt did with message threads (`create`, `cont` or `none`; below). A line per notable event (retries, `connection #n`, errors, `relaunch:`, `run: N ms for the commands, N ms before them, N ms after` for each run inside the harness (the lending of its buffers around it), `progress note:` with the first 60 characters of each progress note drawn, `thread <error_code>: replaying the conversation`, `prefix <file>: N bytes` when a `.PRE` is rendered, `opened <file>: ...` for a reopened chat, `model: [section] settings: ...` after `/model` to another section, `chats: deleted NAME`, with `-k` (the boot tests) `chat list:` and the list's rows as video memory shows them, `leaving: <why>`, the reason the harness left its main loop), and at each HARNESS.EXE exit `stack: used N of M bytes`, the stack's high-water mark. Each line has a timestamp. |
| `C:\MEMORY\*.TXT` | ClawDOS's memories (the memory tools). The image ships none. |
| `C:\SKILLS\*.TXT` | Skills: knowledge the build ships (`image/skills/`), listed in the prompt and read with `read`; the memory tools never reach them. |

### CLAWDOS.CFG

Lines are `key value` (spaces or tabs between), `#` starts a comment except in
`key` and `keyfile` lines, `[name]` starts a provider section (up to 8).

```
provider anthropic          # the [section] used at startup (-p overrides)
name Claude                 # {name} in PROMPT.TXT: the model behind ClawDOS (a section may override)

[anthropic]
type anthropic              # the only type: a host that speaks the Messages API
tunnel 192.168.1.10:8443    # where the TLS tunnel listens (a host on the command line replaces it)
host api.anthropic.com      # the Host header
path /v1/messages           # default
auth x-api-key              # default; "bearer" sends Authorization: Bearer instead;
                            # "tunnel" sends no key header (the tunnel adds it: tunnel.py --key-file)
key sk-ant-...              # or: keyfile C:\CLAWDOS\ANTHROP.KEY
model claude-opus-5-5
effort low                  # omitted: no output_config is sent; F5 cycles it
maxtokens 64000             # max_tokens; 64000 when omitted (thinking counts toward it)
betas ...                   # extra anthropic-beta values; omitted: only those below
cache 1                     # default; 0 sends no cache_control
cachettl 5m                 # default; 1h: both cache markers get "ttl":"1h"; anything else
                            # is an error naming the line
progress 1                  # thinking display "updates": the model's progress notes (beta)
dropthinking 1              # thinking block_binding drop_block (beta)
threads 1                   # message threads: later requests send only what is new (beta)
```

`progress`, `dropthinking` and `threads` are off when absent; the shipped
`[anthropic]` section turns all three on (another host that speaks the API is
unlikely to have the betas). `cache`, `progress`, `dropthinking` and `threads`
take 0 or 1 (anything else is an error naming the line). `cache`, `cachettl`,
`progress`, `dropthinking` and `threads` are the conversation's fixed settings:
they are taken from the provider in use when the conversation's prefix is
written (its first request that gets that far) and kept through F5, a relaunch
(RESUME.TXT carries them: `fixed P D C T H`) and `/model` within the section.
An edit to them applies from the first conversation after CLAWDOS is started
again (the file is read once, at startup). `/model` to another
section chooses them again from that section (lib/apireq.c `api_fixed_switch`):
the thread is dropped, so the next request is a create with the whole
conversation (or no thread), and the `.PRE`'s system block gets that section's
cache marker, rewritten in place (written as `.PR$` and renamed) with the
prompt and tools left byte for byte; if it cannot be rewritten the
conversation keeps the file's marker, so the two markers still agree. The API
binds thinking blocks and the prompt cache to the system prompt and tools, not
to their markers. If a request with the shipped settings is refused with a 400
naming a beta, `thinking` or `thread` (none of them has met the live API yet),
set that line to 0 and start CLAWDOS again (CLAWDOS.CFG is read once, at
startup; the new conversation takes the new settings).

A section without a key still works against the mock API; against the real one
the API answers 401, which is shown (and the harness warns at startup). With
`auth tunnel` no key is needed or sent, and there is no warning: the tunnel in key mode
supplies it (below), and a `key` line in such a section is ignored. `tunnel`, `host` and `model` are required.

## The request

One `POST {path} HTTP/1.1` per API round on a keep-alive connection to the
tunnel, with `Host`, `x-api-key` (or `Authorization: Bearer`; neither with `auth tunnel`),
`anthropic-version: 2023-06-01`, `anthropic-beta` (the provider's `betas`
line, then `thinking-display-updates-2026-08-18` with `progress`,
`thinking-binding-controls-2026-08-01` with `dropthinking` and
`message-threads-2026-08-12` with `threads`, each only once;
no header when there is nothing to send),
`Content-Type: application/json`, `Accept: text/event-stream`,
`Connection: keep-alive` and `Content-Length`. The body is written once per
attempt, straight into the socket: a few fields from the provider's settings,
then the conversation's `.PRE` file and the history file streamed from disk
(lib/apireq.c). `Content-Length` is the sum of their sizes, known before a byte
is sent. The provider's settings are a copy taken when the turn began
(`api_req_settings`): the socket's flushes handle keys, and an F5 handled while
a request is written or waits changes none of its bytes, its length or its
thread fingerprint; it applies from the next message (across a relaunch too,
RESUME.TXT's `turneffort`). `system` and `tools` are the `.PRE` file's bytes,
the same on every request of a conversation (through `/model`, F5 and a
relaunch; `/model` to another section rewrites only the marker): the model's
thinking blocks are bound to the exact prefix that produced them, and the
prompt cache keys on it.

```
{"model":"...","max_tokens":64000,"stream":true,              maxtokens, default 64000
 "thinking":{"type":"adaptive",                             with progress or dropthinking
             "display":"updates",                           progress
             "block_binding":{"prefix_mismatch_behavior":"drop_block"}},   dropthinking
 "output_config":{"effort":"low"},                          if effort is set
 "cache_control":{"type":"ephemeral"},                      unless cache 0; ,"ttl":"1h" with cachettl 1h
 "tool_choice":{"type":"auto","disable_parallel_tool_use":true},
                                                            {"type":"none"} on the 26th round
 "thread":{"type":"create"},                                with threads (below); a continue
                                                            request ends here with "messages"
 "system":[{"type":"text","text":<PROMPT.TXT, escaped>,     system and tools: the .PRE file
           "cache_control":{"type":"ephemeral"}}],           the same marker as the top-level one
 "tools":<TOOLS.JSN>,
 "messages":[<the CHATS file>]}
```

`thinking`, the top-level `cache_control` and the system block's marker come
from the conversation's fixed settings (above), so both markers always carry the
same ttl. The models think adaptively whether or not `thinking` is sent; it is
there for its two beta options. `display` `updates` makes the model's notes
between tool calls arrive as thinking text (without it the text is empty), which
is shown (below). `block_binding` `drop_block` is the safety net for a prefix
that had to change: the API then drops the thinking blocks bound to the old one
instead of answering 400. `effort` is the control for how hard it thinks: F5
cycles it through low, medium, high, xhigh and max for the session, from the
next message, which costs one uncached request after a change. No `fallbacks` is sent. One tool call per round keeps one arguments buffer
enough and runs the tools in the order the user sees them.

## Threads

With `threads 1` (beta `message-threads-2026-08-12`, on every request of the
conversation) a request either creates a thread or continues one. A create is
the request above with `"thread":{"type":"create"}`: the API stores the whole
conversation, the reply included, under the response's id (`message_start`'s
`message.id`). A continue names that response and carries the same fixed
fields but no `system` and no `tools`, and only the messages written to the
history since it:

```
{"model":...,"max_tokens":...,"stream":true,"thinking":...,"output_config":...,
 "cache_control":...,"tool_choice":...,
 "thread":{"type":"continue","previous_message_id":"msg_..."},
 "messages":[<the CHATS file from the offset where that reply ended>]}
```

A tool round then uploads its tool result and the fields, under 1 KB in the
tool tour, instead of the 14 KB prefix and the whole history. The history file
stays the complete conversation, so a create from it (a replay) is always
possible, and is what happens whenever a continue is not certain to be right.
The next request continues only when all of these hold; otherwise it creates a
thread with the whole conversation:

- the request before it got its reply to `message_stop` with a stop reason of
  `end_turn` or `tool_use` (the API does not store a refusal, or a reply whose
  tool call `max_tokens` cut), and that reply was written to the history;
- nothing at or before the offset where that reply ended has been cut from the
  history since (lib/history.c keeps the lowest size the file has been cut to:
  a rollback, a reopened message, a truncation at open), and there is a new
  message after it;
- the fixed fields are the same: `thinking`, the cache markers and the betas
  are fixed per conversation already (above; `/model` to another section drops
  the thread anyway), so what can change is the provider and its `betas` line
  or model (`/model`), the effort (F5, from the next message) and
  `tool_choice` (the round cap's `none`, so the 26th round of a turn is a
  create); a hash of these is kept with the thread;
- the turn before ended normally: a turn cut short (Esc, a lost link given up
  on, an API error, a refusal, a failed history write) clears the thread, and
  so does F2.

The thread (response id, offset, hash) goes into RESUME.TXT (`thread ID AT
FP`), so the request after a `full_memory` run continues it; a relaunch that
comes back after the thread has expired meets `thread_not_found` and replays.

Errors with `error.details.error_code` starting `thread_` come before the
model runs and are not retries: the log gets `thread <code>: replaying the
conversation`, the screen nothing, and the same request goes again at once
as a create with the whole conversation, outside the retry budget.
`thread_unsupported_request` turns threads off for the rest of the
conversation instead (no `thread`, no beta). `thread_fingerprint_mismatch`
means the fixed fields were wrong, a bug, and is replayed the same way. If the
replay is refused with a thread error too, the turn ends with the message.
A 404 without a thread code (an unknown model) is an ordinary error.
`/model` to a provider section whose host lacks the beta takes that section's
`threads` (and its other fixed settings) for the rest of the conversation, so
no `thread` or threads beta goes there (image/tests/provider.bat). A lost
link, a 529 or an overload in the stream during a continue resends the same
continue (the retry rules below); if the API answers that it was already
continued or is in flight, that is a thread error and is replayed.

## The history file

Everything in it is UTF-8 JSON exactly as the API wants it. CLAWDOS writes:

- the user's text: `{"role":"user","content":"..."}`; after tool results it is
  appended to that message as a text block (roles must alternate);
- the assistant message, block by block as it streams: `text`, `thinking`
  (with its `signature`), `redacted_thinking` and `tool_use` blocks. Thinking
  blocks must be echoed back unchanged on later rounds ("preserved thinking"),
  so the file holds exactly what the API sent, whether or not their text was
  shown as a progress note. A block
  type it does not know ends the turn with an error rather than being dropped;
- tool results: `{"role":"user","content":[{"type":"tool_result","tool_use_id":
  "...","content":"...","is_error":false}]}`. The content is the tool's output,
  or `Error: ` + output, `The user denied this tool call.` or
  `Interrupted by the user.` with `is_error` true. The output holds at most
  4,096 bytes; `read`, `dir` and `search` stop at a whole line and end with
  one saying so (`...(more lines from N; use start)`, `+N more`,
  `+N more matches`). Tool arguments arrive as the schema's types (`integer`,
  `boolean`, `string`); the executors also take `"true"`/`"false"`/`"1"`/`"0"`
  for a flag and digits in a string for a number, and fail the call naming
  the argument for anything else. Tabs in arguments stay tabs.
- a `run` result with a screenshot (its `screenshot`, `shot_at`, `shot_mode` or
  `shot_color` argument, on a graphics screen): the content is a list, the text
  block then the picture,
  `"content":[{"type":"text","text":"..."},{"type":"image","source":{"type":"base64","media_type":"image/png","data":"..."}}]`.
  The PNG (C:\CLAWDOS\SHOT.PNG, about 16 KB: 320x200 at 2 bits or 640x200 at
  1, indexed, one stored deflate block; lib/shotpng.h) is streamed into the
  file as base64 (about 22 KB) a piece at a time and deleted. If it cannot be
  read or written, that message is cut off and the result written again as a
  plain string whose last line says the screenshot could not be attached (one
  tool_result, no image block). A text screen comes back as text in the result instead. A
  full_memory run's screen goes from the launcher to the resumed harness in
  C:\CLAWDOS\SHOT.RAW (a 6-byte `ShotInfo`, then the 16 KB; lib/shot.h). On
  the real API the first image in a thread that had none makes the continue
  fail with `thread_not_found`, and CLAWDOS replays the conversation (the mock
  does the same); every later full replay carries every picture again.

A turn remembers where it started. Anything that ends it early (Esc, a dropped
link, an API error, a refusal) truncates the file back to the last complete
message and then: no assistant message this turn, back to the turn start (the
text goes back into the input box); an unanswered `tool_use`, a stopped result
is appended so the next request is valid. A reply cut by `max_tokens` keeps
what arrived and shows `[reply cut off: too long]`. A turn carried on after a
relaunch keeps its start: RESUME.TXT holds the offset (and the closing bytes of
a results message the turn's text joined), so a rollback there takes the whole
turn as it would have before the relaunch. `/new` starts a new file;
only the chat list's Del deletes one (with its `.PRE`), and opening another chat
deletes the current one if nothing was said in it.

Writes collect in a 2 KB buffer (lib/history.c) and go to DOS when it fills
and whenever the file is read, cut or completed: each finished message is
written out and committed (INT 21h function 68h) before anything else happens,
a rollback writes nothing it would cut, and the request copies the file only
after everything is on disk. A write that fails (a full disk) ends the turn
with "Cannot write the conversation file." (also when it fails while a reply
streams): the message is rolled back, and a call left unanswered gets a short
error result, or if even that cannot be written the turn is rolled back. A cut
(a rollback) is committed as well, so a PC switched off after one leaves the
file as cut, and a reopen does not send what was dropped.

## Reopening a chat

F6 (`/chats`) or `-c NAME` makes an earlier CHATS file the conversation. `-c`
takes a chat's name only, never a path. A file that does not begin with
`{"role":"user",` is refused and left as it is, before the history module's
repair (which cuts a file back to its last whole JSON value) can touch it; so is
one ending in a tool call whose id is longer than 63 bytes. A repair that does
cut a damaged end is said on screen and in the log (`opened ...: N bytes (M
cut)`). Nothing
reads the whole file into memory: lib/chatfile.c reads it a piece at a time,
once to count the typed messages and find where the last three begin (a typed
message starts with `,\r\n{"role":"user","content":"`, a string, where results
and the text that joins them are a list; raw CR and LF never occur inside a JSON
string), then from there to the end to draw them as the live transcript did
(the typed text after `> `, reply text after a bullet, a header per tool call;
thinking blocks and results are not drawn; a text block is cut after 1,000 bytes
of its JSON string, before an escape or a UTF-8 letter the cut would split, and
ends with ` ...`; at most the last 48 KB is drawn; the round cap's note beside a
turn's last result is not drawn as typed). The list itself reads only the
first 160 bytes of each file.

The conversation's `.PRE` file is kept, so `system` and `tools` are the bytes
its thinking blocks were made under. The fixed settings are the provider's as
it is now, chosen as `/model` to that section would (`api_fixed_switch`): if
the `.PRE`'s cache marker differs it is rewritten to match, so a chat that
visited a `cache 0` section is cached again (if the rewrite fails the file's
marker is kept and the top-level one follows it); a `.PRE` that is
missing is rendered afresh at the first request, with today's prompt,
memories, skills and settings (its thinking blocks then no longer match the
prefix: with `dropthinking` the API drops them, without it the API answers
400). No thread is assumed: the first request is a create with the whole
conversation. The box above the input shows the turns (typed messages that
got a reply) until the first reply brings the token count.

What the file ends with decides what the next user text does, so that roles
still alternate:

| The file ends with | When | Opening it | The next text |
|---|---|---|---|
| a reply without a tool call | a turn that finished | nothing | a new user message |
| a typed message, unanswered | the PC went off while waiting, or mid-reply (the half reply is cut by `hist_open`) | a note says so | joins it after a blank line: `"...earlier\n\nnew"` |
| a tool call without its result | the PC went off while the tool ran (or during a `full_memory` run) | the result `Error: no result: ClawDOS stopped before this call finished, and the chat was reopened later.` (`is_error`) is written, and a RESUME.TXT for this chat is deleted so a later `-resume` cannot answer the call twice | joins that results message as a text block |
| a tool results message | a stopped or denied call, or a result written just before the PC went off | nothing | joins it as a text block |

The current conversation, if nothing was said in it, is deleted when another is
opened (each start and F2 make one). A chat that cannot be read leaves the
current one as it was (if even that cannot be reopened, a new conversation
starts and the note says so). A `.PRE` whose cache marker cannot be read is
deleted and rendered afresh. Del in the list refuses the open chat and a chat
that RESUME.TXT names (a `full_memory` run waiting for `CLAWDOS -resume`).

## The response

`Transfer-Encoding: chunked`, `text/event-stream`. Layers, each in lib/ with
unit tests in tests/unit/dosunit.c: `http.cpp` (head, chunked decoding, read to
close), `sse.c` (events; one `data:` payload fits 16 KB; a longer one goes to
`apistream_big` in pieces, which takes a `signature_delta` or a
`redacted_thinking` block, both one long string that grows with how long the
model thought (9 KB seen after a short think), and streams it into the history;
any other event over 16 KB is an error for the turn), `json.c` (key lookup by path, string decoding with `\uXXXX` and
surrogate pairs, escaping), `utf8.c` (UTF-8 to CP437 with fallbacks for quotes,
dashes, arrows, ticks and accented letters CP437 lacks; CP437 to UTF-8 for what
we send), `apistream.c` (events to screen text, history and the tool call).

Events acted on: `message_start` (model, usage), `content_block_start`,
`content_block_delta` (`text_delta` to the screen and the history,
`thinking_delta` to the history and, when its text is not empty, to the screen
as a progress note, `signature_delta` and `input_json_delta` to the history,
the latter also to the 16 KB arguments buffer), `content_block_stop`,
`message_delta` (stop_reason, output tokens), `message_stop`, `error`, `ping`.
Unknown events and keys are ignored (with the binding beta, `message_start`'s
message carries an `input_transformations` array; it is not read).

A progress note is a thinking block's text, decoded to CP437 like reply text
and drawn dim in the reply's margin as a paragraph of its own: it opens with
the block's first non-empty text, ends with the block, and what follows (a tool
header, the reply's bullet, a note) starts on a fresh line in its own colour.
An empty thinking block draws nothing. The start of each note goes to
CLAWDOS.LOG (`progress note: ...`).

## Errors, retries, lost links

- HTTP 429, 529, 500-599, or a failure before any byte of the response: the same
  request again after 2 s, then 8 s (`retry-after` wins). 400-level answers show
  `error.message` and end the turn.
- An `error` event inside a 200 stream whose `error.type` is `overloaded_error`
  or `api_error`, before any reply text was shown: the partial message is
  dropped from the file and the request is retried as for a 529, on the same
  schedule and within the same two retries. Any other stream error, or one
  after text was shown, shows `error.message` and ends the turn.
- An idle keep-alive connection the far end has dropped is reopened silently.
- The link drops mid-stream: the partial assistant message is dropped from the
  file, `[link lost, asking again]` is shown and logged with `connection #n`,
  and the request goes out again on a new connection, once per turn (the same
  continue request, in a thread). The model answers from scratch.
- A thread error (`error.details.error_code` `thread_...`): see "Threads".
- `stop_reason refusal`: "The model declined to answer that." and the user
  message is removed from the file.
- 300 s without a byte counts as a lost link. Esc closes the connection.

## The clock

CLAWDOS leaves the clock alone. At boot ASTCLOCK.COM reads the PicoMEM 2's
battery clock and NETUP.BAT sets the time by SNTP (see CLAUDE.md); `NETUP
NOCLOCK`, which every boot test but `clock.bat` uses, stops after DHCP, so the
tests reach no server but the mock on the host.

## The tunnel

`python3 tunnel/tunnel.py [--map PORT=HOST[:443]]... [--listen ADDR] [--allow ADDR]... [--key-file PATH]`
(default `8443=api.anthropic.com:443`): for each client connection, a TLS connection to
the mapped host and two byte pumps. It logs connections and byte counts, never
content. By default it knows nothing about HTTP and copies bytes exactly.

`--allow ADDR` (repeatable, single IPv4/IPv6 addresses, not networks) lets only those
clients connect on any map; others are closed at once and logged by address.

**Key mode** (`--key-file PATH`, opt-in) is the one place the tunnel reads the API, so
that the DOS PC need not hold the key (`build.py --no-key`; the provider has
`auth tunnel`, and the harness sends no key header and does not warn about a missing
key). The file holds one key, whitespace around it stripped; an unreadable or empty
file, or one with anything but visible ASCII, is a start-up error. For every request
on a map to `api.anthropic.com` (never another map: a second provider's port is still
a byte copy, and `--key-file` without an `api.anthropic.com` map is an error) it:

- finds the request head (up to the blank line, at most 64 KB), requires an origin-form
  target (`/path`) and exactly one `Host` header naming the mapped host (case aside, with
  or without its port), so the key goes only where it belongs whatever a client or a web
  page asks; drops every `x-api-key` and `authorization` header the client sent and puts
  `x-api-key: <key>` first among the headers;
- forwards exactly `Content-Length` bytes of body unread and unchanged, then looks for
  the next request head on the same connection (keep-alive; pipelined requests work);
- copies responses back unread, as before.

Limits: only Content-Length framing. A request with `Transfer-Encoding`, `Upgrade` or
`CONNECT` (501), a bad, repeated or over-18-digit `Content-Length`, a folded header line, an
absolute-form target, a missing, repeated or foreign `Host`, a bare CR/LF/NUL in the head or a
malformed header line (400), or a head over 64 KB (431) is answered with that status and
the connection closed, never forwarded. A refusal may interleave with an upstream
response if the client pipelined. The key and the requests are never logged, only
addresses, byte counts and the number of requests; the key is held in the tunnel's
memory. `--key-file` refuses to start without an `--allow`, unless `--listen` is a
loopback address, so that the tunnel never adds your key for whoever reaches the port.
Equivalents of the default mode:

```
socat TCP-LISTEN:8443,fork,reuseaddr OPENSSL:api.anthropic.com:443
```

```
; stunnel (a comment only ever on a line of its own: after a value it is part of it)
[anthropic]
client = yes
accept = 8443
connect = api.anthropic.com:443
; check the certificate chain, and that it is api.anthropic.com's
verifyChain = yes
checkHost = api.anthropic.com
; the CA bundle, where stunnel does not find the system's
CAfile = /etc/ssl/cert.pem
```

Without `verifyChain` and `checkHost` (stunnel checks neither by default) the
stanza sends the API key through TLS to whoever answers on the way; `CAfile`
(or `CApath`) must name the system's CA certificates where stunnel is not built
to use them (its path differs: `/etc/ssl/certs/ca-certificates.crt` on Debian
and Raspberry Pi OS, `/etc/ssl/cert.pem` on macOS). socat's `OPENSSL:` verifies
the certificate by default (`verify=1`), and tunnel.py uses Python's default
context, which checks the chain and the host name.

`python3 tunnel/mockapi.py [--listen 127.0.0.1] [--port 18023] [--speed 1] [--tools FILE] [--thread-ttl 300] [--first-id 1]` fakes the API on plain
HTTP for tests and offline use: `CLAWDOS 10.0.2.2:18023` from an emulator, then
`!help` for its scenarios. It checks every request against the rules below and
answers a violation with HTTP 400 (`invalid_request_error`, naming the problem
and the message index) and a `VALIDATION:` line in its log, which the boot tests
reject; it is not the whole of the real API's validation, and a task that adds a
request field adds its rule. The rules: the `anthropic-version`, `content-type` and
`Content-Length` headers (a length that does not match the body is a 400 within 10 s,
the connection closed); `anthropic-beta` values it knows (the three above: a
mistyped `betas` line is a 400); valid JSON whose top-level fields are the ones in "The
request" above (others are rejected) with `model`, `max_tokens`, `messages` and
`stream` true; `tools` equal to TOOLS.JSN (which must be readable at startup); the
`tool_choice` shapes; `output_config` holding only an `effort` of `low`, `medium`,
`high`, `xhigh` or `max`; a `thinking` object of type `adaptive` with at most
`display` `updates` (which needs the `thinking-display-updates-2026-08-18`
beta header) and `block_binding` `drop_block` (which needs
`thinking-binding-controls-2026-08-01`), as the real API checks; cache
markers `{"type":"ephemeral"}` with an optional `ttl` of `5m` or `1h`, the
top-level one and the system block's agreeing; the first message from the user, alternating roles, no empty
text blocks, known block types; each `tool_use` answered by exactly one
`tool_result` in the next message, ahead of any text block; an image block in a
`tool_result` a base64 PNG that decodes as 320x200 at 2 bits or 640x200 at 1
(the mock logs `image ok WxH (the call): palette ..., pixels <digest>` and saves
it as `build/mockshot-N.png`); and, in a conversation
the mock saw begin (a request of one message starts one), every assistant message
being a reply the mock sent, its thinking or redacted_thinking block coming back
byte-identical and first; and `system`, `tools`, `thinking` and the top-level
`cache_control` the same on every request of a conversation (`system` and
`tools` compared without their `cache_control` markers, which invalidate
nothing on the real API). With `tool_choice`
none the mock makes no tool call. With the binding beta its `message_start`
carries `input_transformations: []`; with `display` `updates` the tool tour's
responses each open with a progress note naming the next step. Its log line per
request names the thinking options, the cache ttl (the system block's in
brackets), the thread kind and the body's length (`request: N messages, last
role user, thinking updates+drop_block, cache 5m (system 5m), thread continue (1
sent), 659 bytes, betas message-threads-2026-08-12,...`, the anthropic-beta values
by name or `none`; N counts the whole conversation, a continue's stored messages
included), and, when the request carries a key header, `auth: x-api-key, N
characters` (never the key). The boot tests run a second mock beside it with
`boottest.py --mock2` (port 18024, its ids counted from 1001: another provider
section), and `--key-tunnel` puts tunnel.py's key mode in front of the first on
port 18025, over plain TCP, adding a made-up key.

Threads in the mock (tunnel/mockapi.py `Threads`): each reply that reaches
`message_stop` with `end_turn` or `tool_use` is stored by its id with the whole
conversation, its system, tools and fixed fields, for `--thread-ttl` seconds
(300) after its request. A continue is turned back into the whole request
before every rule above is checked, so the thinking and tool-pairing rules hold
across the thread. Its own rules, each answered with the API's status and
`error_code` (`thread_not_found` 404, the others 400): the beta header with any
`thread`; a create of exactly `{"type":"create"}`, a continue of `type` and
`previous_message_id`; no `system` or `tools` on a continue; the response
known and not expired; continued once (`thread_already_continued`), not while
another continue of it runs (`thread_continue_in_flight`); the fixed fields and
the beta set unchanged (`thread_fingerprint_mismatch`); no assistant message the
thread already has; `"thread": null` is refused like any bad thread object. An error before the model runs, or a dropped link, leaves
the response continuable. Refusals a correct client never meets are also
`VALIDATION:` lines: a continue of a refusal or a `max_tokens` reply or one never stored (its reply
did not reach `message_stop`), a response
continued twice (other than a resend after a dropped link), a fixed field
changed, a thread sent again after `thread_unsupported_request`. After a refusal
a client does meet, the next request must replay the whole conversation, and the
log says `thread replay after <code>: PASS` or `FAIL`. Scenarios: `!threadlost`
(the mock forgets the thread after a call; `!threadlost run` with a
`full_memory` run), `!threaddrop` (the link drops in the continue's reply;
`burn` makes the dropped continue use its response up), `!threadretry` (the
continue meets a 529 once; `overload`, `404`, `400` instead), `!threadfail` (the
continue and its replay are refused), `!nothreads` (`thread_unsupported_request`
from the second request on), and the tool tour's `tool tour threads: PASS` (each
request after the first continued, each under 8 KB, except a create for a
`tool_choice` change).
