# What CLAWDOS sends to the API, and what it expects back

HARNESS.EXE is a client of the Anthropic Messages API. The only thing between it
and `api.anthropic.com` is a TLS tunnel that copies bytes (below). This page is
the reference for the DOS side: the files it uses, the request it writes, the
stream it reads and how the history stays valid.

## Files on the DOS PC

| File | What |
|---|---|
| `C:\CLAWDOS\CLAWDOS.CFG` | Settings and providers (below). The image build fills in the tunnel address and the key from `.env` (not in the test images `boottest.py` and `test8088.py` build: the mock needs no key). |
| `C:\CLAWDOS\PROMPT.TXT` | The whole system prompt. `{name}` becomes the name of the model behind ClawDOS (the assistant calls itself ClawDOS), `{memories}` the output of `memory_list` (or `(none)`). CP437 text; CRLF or LF. |
| `C:\CLAWDOS\TOOLS.JSN` | The request's `tools` array, verbatim. The single source of the tool schemas; `tunnel/mockapi.py` reads it too. The repo's copy is laid out for reading; the image build writes it minified, without the whitespace (about 1.7 KB less in every request). Every tool sets `eager_input_streaming` so a big argument (a file for `write`) streams in small `input_json_delta` fragments instead of arriving as one event, which the 16 KB event buffer could not hold. |
| `C:\CLAWDOS\CHATS\MMDDnnn.JSN` | One file per conversation: the elements of `messages`, each a complete JSON object, separated by `,\r\n`. |
| `C:\CLAWDOS\CLAWDOS.LOG` | A line per API round (`api <provider> <model> round= ttft= total= up= len= stop= in= out= cr= cw= ev=`): times in ms from the start of the request (`ttft` to the first text, `total` to the end, `up` to the last body byte handed to the socket, so `ttft - up` is the wait for the API), `len` the body's Content-Length, both for the attempt that got the reply, `ev` the longest SSE event. A line per notable event (retries, `connection #n`, errors, `relaunch:`), and at each HARNESS.EXE exit `stack: used N of M bytes`, the stack's high-water mark. Each line has a timestamp. |
| `C:\MEMORY\*.TXT` | ClawDOS's memories (the memory tools). |

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
auth x-api-key              # default; "bearer" sends Authorization: Bearer instead
key sk-ant-...              # or: keyfile C:\CLAWDOS\ANTHROP.KEY
model claude-opus-5-5
effort low                  # omitted: no output_config is sent; F5 cycles it
maxtokens 64000             # max_tokens; 64000 when omitted (thinking counts toward it)
betas ...                   # omitted: no anthropic-beta header
cache 1                     # default; 0 sends no cache_control
```

A section without a key still works against the mock API; against the real one
the API answers 401, which is shown. `tunnel`, `host` and `model` are required.

## The request

One `POST {path} HTTP/1.1` per API round on a keep-alive connection to the
tunnel, with `Host`, `x-api-key` (or `Authorization: Bearer`),
`anthropic-version: 2023-06-01`, `anthropic-beta` (only with `betas`),
`Content-Type: application/json`, `Accept: text/event-stream`,
`Connection: keep-alive` and `Content-Length`. The body is written twice, once
into a counting sink for the length and once into the socket, streaming the
prompt, the tools file and the history file from disk (lib/apireq.c):

```
{"model":"...","max_tokens":64000,"stream":true,              maxtokens, default 64000
 "output_config":{"effort":"low"},                          if effort is set
 "cache_control":{"type":"ephemeral"},                      unless cache 0
 "tool_choice":{"type":"auto","disable_parallel_tool_use":true},
                                                            {"type":"none"} on the 26th round
 "system":[{"type":"text","text":<PROMPT.TXT, escaped>,"cache_control":{"type":"ephemeral"}}],
 "tools":<TOOLS.JSN>,
 "messages":[<the CHATS file>]}
```

No `thinking` field is sent (the models think adaptively; `effort` is the
control: F5 cycles it through low, medium, high, xhigh and max for the session,
which costs one uncached request after a change) and no `fallbacks`. One tool call per round keeps one arguments buffer
enough and runs the tools in the order the user sees them.

## The history file

Everything in it is UTF-8 JSON exactly as the API wants it. CLAWDOS writes:

- the user's text: `{"role":"user","content":"..."}`; after tool results it is
  appended to that message as a text block (roles must alternate);
- the assistant message, block by block as it streams: `text`, `thinking`
  (with its `signature`), `redacted_thinking` and `tool_use` blocks. Thinking
  blocks are never shown but must be echoed back unchanged on later rounds
  ("preserved thinking"), so the file holds exactly what the API sent. A block
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

A turn remembers where it started. Anything that ends it early (Esc, a dropped
link, an API error, a refusal) truncates the file back to the last complete
message and then: no assistant message this turn, back to the turn start (the
text goes back into the input box); an unanswered `tool_use`, a stopped result
is appended so the next request is valid. A reply cut by `max_tokens` keeps
what arrived and shows `[reply cut off: too long]`. `/new` starts a new file;
nothing deletes them.

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
`thinking_delta`/`signature_delta` and `input_json_delta` to the history,
the latter also to the 16 KB arguments buffer), `content_block_stop`,
`message_delta` (stop_reason, output tokens), `message_stop`, `error`, `ping`.
Unknown events and keys are ignored.

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
  and the request goes out again on a new connection, once per turn. The model
  answers from scratch.
- `stop_reason refusal`: "The model declined to answer that." and the user
  message is removed from the file.
- 300 s without a byte counts as a lost link. Esc closes the connection.

## The clock

CLAWDOS leaves the clock alone. At boot ASTCLOCK.COM reads the PicoMEM 2's
battery clock and NETUP.BAT sets the time by SNTP (see CLAUDE.md).

## The tunnel

`python3 tunnel/tunnel.py [--map PORT=HOST[:443]]...` (default
`8443=api.anthropic.com:443`): for each client connection, a TLS connection to
the mapped host and two byte pumps. It logs connections and byte counts, never
content. Equivalents:

```
socat TCP-LISTEN:8443,fork,reuseaddr OPENSSL:api.anthropic.com:443
```

```
[anthropic]            ; stunnel
client = yes
accept = 8443
connect = api.anthropic.com:443
```

`python3 tunnel/mockapi.py [--port 18023] [--speed 1]` fakes the API on plain
HTTP for tests and offline use: `CLAWDOS 10.0.2.2:18023` from an emulator, then
`!help` for its scenarios.
