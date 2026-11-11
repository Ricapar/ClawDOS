# ClawDOS analysis, November 2026

A review of the whole repository (branch `relaunch` at `38fd49a`) looking for
improvements in performance, reliability, user experience, functionality,
security and tooling. It is meant to be argued with: every finding has an ID so
we can refer to it, the evidence behind it, and how sure I am.

## How to read this

**Method.** I read the harness core in full (`src/harness/*`, `src/clawdos`,
`lib/apireq.c`, `apistream.c`, `http.cpp`, `net.cpp`, `history.c`, `sse.c`,
`chunked.c`, `shell.c`, `screen.c`, `console.c`, `relaunch.c`), the shipped
prompt, config and tool schemas, the boot scripts, and the docs. Two background
reviewers read the rest of `lib/` and the host side (`scripts/`, `tunnel/`,
tests); their findings are in sections 1, 2, 5, 6 and 7 and are marked where
I did not re-check them myself. API statements were checked against the current
Messages API reference for `claude-opus-5-5`, the configured model.

**What I did not do.** Nothing was built, and nothing was run on an emulator
or the real PC (only the host-side mock tests, which pass), so there are no
new timings here. Every performance item is a reading of the code plus
arithmetic; section 1 starts with the measurements that would turn them into
facts.

**Confidence labels.**

| Label | Meaning |
|---|---|
| Read | Confirmed by reading the code path; not reproduced at run time |
| Measured | A number taken from a file or the link map in this checkout |
| Docs | Follows from the API reference; not reproduced against the live API |
| Hypothesis | Plausible, needs a measurement before it is worth doing |

**Effort.** S is under a day, M a few days, L a week or more.

## Summary

The project is in good shape where it has been pushed hardest: the history
file stays valid through Esc, lost links and errors; every parser has unit
tests that run in DOSBox-X; the boot tests cover retries, drops, oversized
events and the relaunch; interrupt handlers are written with the right
paranoia. The findings below are mostly about what the 8088 does on every API
round, about three places where Opus 5.5 behaves differently from what the
client assumes, and about features the on-disk design already makes cheap.

The items I would discuss first:

| # | ID | Finding | Area | Effort | Confidence |
|---|---|---|---|---|---|
| 1 | P1 | Every round uploads the prompt, the tool schemas and the whole history again. The API's message threads (beta) let a tool round send only the new tool result. | Performance | M | Docs |
| 2 | P2, R1 | The request prefix is rebuilt and escaped byte by byte twice per round, and can change mid-conversation (the memory list is re-read after a relaunch). Render it once per conversation. | Performance, reliability | M | Read |
| 3 | B1, B2 | Two tool bugs that mislead the model on ordinary source files: tabs in tool arguments become four spaces, and a result over 4,096 bytes is cut with no marker. | Reliability | S | Read |
| 4 | A1 | `max_tokens` is 16000 and thinking counts against it, while F5 offers `xhigh` and `max`. Replies will be cut off at high effort. | API | S | Docs |
| 5 | A2 | On Opus 5.5 the model's notes between tool calls arrive as thinking blocks with empty text, so the screen is silent through a long tool turn. | API, UX | S-M | Docs |
| 6 | U1 | Every `run` waits up to 5 seconds on "any key returns to ClawDOS", even when the command succeeded. | UX, performance | S | Read |
| 7 | U2 | A note printed while a reply is streaming (F1, F3, F5, or "link lost") leaves its colour and indent on the rest of the reply. | UX bug | S | Read |
| 8 | P8 | There is no performance baseline. The roadmap's numbers predate upload buffering, and the log cannot separate upload time from API time. | Tooling | S | Read |
| 9 | S1 | The API key is on the DOS disk, in every built image, and crosses the LAN in clear; the tunnel accepts any client. | Security | S-M | Read |
| 10 | T2 | Updating the real PC means copying changed files into the old image by hand. A missed file (TOOLS.JSN, PROMPT.TXT) leaves a mismatch nothing detects. | Tooling | S-M | Read |
| 11 | F1, F2 | Chats are saved but cannot be reopened, and nothing shrinks a long one. `/chats` and `/compact` both fall out of the history-file design. | Functionality | M | Read |
| 12 | T4 | The mock API checks only that roles alternate, so the boot tests cannot catch an invalid request. A strict validator should precede any change to the request. | Testing | M | Read |

## Decisions (2026-11-10)

Answers to the questions this report first ended with, and what each changes.

| Topic | Decision | ID | Effect |
|---|---|---|---|
| The dumb tunnel | A principle: the point is that nearly everything runs natively on the 8088. Having the tunnel add the key header is acceptable. | S1, S2 | Key injection goes ahead as an opt-in tunnel mode with a source-address allowlist. Nothing else moves into the tunnel, and threads keep it dumb. |
| Account age | Not needed. The message threads page states the thinking-block rule with no account condition. | R1 | The client is built as if the rule always applies: a frozen prefix per conversation, and thinking blocks left out whenever the prefix has to change. |
| Session shape | Short and rapid-fire, with walk-away gaps. | P1, A5, F2 | Threads fit: rounds inside a turn stay within the five-minute window, and a walk-away costs one full replay, as today. `/compact` drops in priority; a one-hour cache option becomes worth having. |
| Beta API features | Fine to use. | P1, A2, R1 | Threads, progress updates and the thinking-binding controls can be on by default for the `anthropic` provider, each behind a config line. |
| Editing PROMPT.TXT mid-session | Never used. | P2 | The prefix is frozen per conversation and nothing is lost. |
| After a `run` | Return at once and show the last few lines of output in the transcript. | U1 | The hold goes away. |
| Installing a build | A new image is built and the changed binaries are copied into the image on the SD card. | T2 | The PC's data survives today; the risk is a missed file. `build.py --update` makes it one command. |
| Cache lifetime | Configurable, five minutes by default. | A5 | A `cachettl` line per provider; `1h` is there for sessions with long walk-aways. |

## 1. Performance

The cost that matters on this machine is per API round, because a coding turn
is many rounds. Today a round does this (all in `Chat::request`,
`src/harness/chat.cpp:408`):

1. Builds the whole body into a counting sink to learn `Content-Length`
   (`chat.cpp:428`): reads PROMPT.TXT and escapes it a byte at a time, reads
   TOOLS.JSN, reads the entire history file.
2. Builds it all again into the socket (`chat.cpp:444`), in 512-byte TCP sends.
3. Streams the reply, writing each delta to the history file as it arrives.

Fixed bytes per round, before any conversation: PROMPT.TXT 1,858 + TOOLS.JSN
10,594 = 12.5 KB (Measured). A turn of 15 tool rounds in a 60 KB conversation
therefore pushes more than 1 MB through the 8088 and its NE2000, and reads the
history from disk 30 times.

### P8. Measure first (S, Read)

- The roadmap's only live number is "11.7 KB up, first token after 19 s"
  (`docs/ROADMAP.md`, M5.5). That run (`5b52429`) predates buffered upload
  (`766f1f8`), so it no longer describes the code. The "~100 KB history"
  timing is still listed as unmeasured.
- The log line's `ttft` starts at `reqStart`, set before the head is sent
  (`chat.cpp:442`), so it lumps the 8088's upload together with the API's
  latency. Add the tick when the last body byte was flushed (`up=` ms) and the
  body length (`len=`) to `logRound` (`chat.cpp:349`). That one change says
  which of the items below are worth doing.
- A repeatable benchmark: a mock scenario with a fixed reply (say 4 KB of text
  at full speed) and a pre-seeded 100 KB history, run under
  `scripts/test8088.py` in 86Box. Four numbers come out: connect, upload KB/s,
  streamed characters per second, relaunch round trip. Keep them in the
  roadmap and rerun after each change.

### P1. Message threads: send only what is new (M, Docs)

The Messages API has a beta (`anthropic-beta: message-threads-2026-08-12`)
where the first request carries `"thread":{"type":"create"}` and each later
one carries `"thread":{"type":"continue","previous_message_id":"msg_..."}`
with only the messages added since, and no `system` or `tools`. The server
keeps the conversation for five minutes after its last request. Pricing and
caching are unchanged; the request just gets small.

For ClawDOS this is the largest available win, and it keeps the tunnel dumb.
A tool round would upload the `tool_result` message (at most ~4 KB) instead of
12.5 KB plus the history. The design already fits:

- The client must keep its own full copy and be able to replay it. That is
  the history file and today's request.
- A thread that expired (the user took more than five minutes, a slow
  compile) answers 404 with `error.details.error_code` `thread_not_found`;
  the fix is to send the full request again as a new `create`. Worst case is
  today's cost.

What it needs:

- Capture `message.id` from `message_start` (`lib/apistream.c:242` reads only
  the model and usage) and remember the history offset where that response
  ended; a continue request copies the file from there.
- Read `error.details.error_code` in `readErrorBody` (`chat.cpp:321`) and
  replay on `thread_not_found`, `thread_already_continued` and
  `thread_continue_in_flight`.
- Several request fields are fixed for a thread's life: `model`,
  `tool_choice`, `output_config` (effort), top-level `cache_control`, the set
  of betas. So F5, `/model`, and the round cap's switch to
  `"tool_choice":{"type":"none"}` (`lib/apireq.c:108`) each start a new
  thread. The round cap could stop changing `tool_choice` and rely on the note
  it already appends to the tool result.
- Do not continue after a refusal, or after a tool call cut off by
  `max_tokens` (the server does not store those turns).
- A `threads 1` line per provider in CLAWDOS.CFG: it is a beta, and other
  hosts that speak the API will not have it.
- The thread id goes into RESUME.TXT so a relaunch that returns within five
  minutes continues the thread.
- The mock API needs to fake create, continue and `thread_not_found`.

Details confirmed against the message threads page (read 2026-11-10):

- A continue request carries the fixed fields again with the same values;
  only `system`, `tools` and the earlier messages are left out. `thinking` is
  one of the fixed fields, so A2's display setting must be chosen before the
  first request of a conversation.
- Continue only after `message_stop` has arrived, and each response id can be
  continued once. If the stream of a continue request drops, send the same
  request again; if that answers `thread_already_continued` or
  `thread_continue_in_flight`, replay. "Replaying is always safe."
- Check `error.details.error_code` before replaying on a 404: an unknown
  model is also a 404.
- At most four `cache_control` breakpoints per request, and the one on
  `system` counts again on every continue request. ClawDOS uses two.
- Inside a thread the system prompt is never resent, so R1 cannot occur on a
  continue request. It can still occur on a replay, which sends `system` and
  `tools` again, so P2 is needed with threads as much as without.
- Beta on every current model, on the Claude API. Not eligible for zero data
  retention.

Not compatible with server-side compaction or context editing, which matters
for F2 (client-side compaction works: start a new thread from the summary).

### P2. Render the request prefix once per conversation (M, Read)

Three costs and one hazard come from rebuilding the prefix on every round:

- `promptText` escapes PROMPT.TXT through `json_escape_cp437` one byte per
  call, each byte going through `sink_put` and the sink's function pointer
  (`lib/apireq.c:80`, `lib/json.c:268-296`). It does that twice per round.
- The counting pass reads the whole history from disk only to add up its
  length (`lib/history.c:192`), though the file's length is already known
  (`hist_size`), as is the tools file's.
- Reads are 512 bytes at a time (`COPY_BUF`, `PIECE`), so a 100 KB history is
  about 400 DOS read calls per round across the two passes.
- The hazard is R1: the prefix can change mid-conversation.

Proposal: when a conversation starts, write the rendered
`"system":[...],"tools":[...]` text to a file beside the chat (for example
`CHATS\MMDDnnn.PRE`). A round then sends a short computed head, that file, and
the history, and `Content-Length` is arithmetic on three known sizes. The
counting pass and the per-round escaping disappear, and the prefix is frozen
by construction. An edit to PROMPT.TXT applies to the next conversation
instead of the next message, which is also what prompt caching wants.

Smaller pieces that stand alone:

- Minify TOOLS.JSN at image build: 10,594 bytes become 8,840 (Measured), 17%
  off the fixed upload, for one line in `scripts/build.py`.
- Raise the copy buffers from 512 bytes to 2-4 KB.

### P3. Send full-size TCP segments (S, Hypothesis)

`HTTP_TX_MAX` is 512 (`lib/http.cpp:9`), so the request goes out in 512-byte
segments, about a third of what the MSS allows, and `TCP_MAX_XMIT_BUFS` is 5
(`src/harness/harness.cfg`). The comment there says "one TCP connection that
sends short lines", which was true of the bridge protocol and is not true of a
100 KB POST. Try a 1,460-byte send buffer and measure upload KB/s (needs P8).
The gain is per-packet overhead only, so expect something modest, not 3x.

### P4. Buffer history writes (S, Read; size of the gain unmeasured)

`hist_write` does an `lseek` and a `write` to DOS on every call
(`lib/history.c:88-103`). Two callers make that expensive:

- **Tool results and user text go to disk one character at a time.**
  `convo_tool_result` escapes the result through `json_escape_cp437`, which
  calls the sink once per input byte (`lib/json.c:293`), and the sink is
  `hist_write` (`lib/convo.c:7-11`, `:65`). A 4 KB tool result is about 4,000
  seeks and 4,000 writes through INT 21h. This happens after every tool call.
- **Streaming.** `apistream` calls `hist_write` for every text, thinking and
  tool-input delta, often for a dozen bytes.

A 1-2 KB write-behind buffer in `history.c`, flushed at `hist_message_done`,
before any rollback or truncate, and before `hist_copy`, fixes both and drops
the redundant seek. Crash safety is unchanged: the file is only committed at
message boundaries today (`history.c:120`). Batching inside
`json_escape_cp437` (a small local buffer, one sink call per run) helps the
request writer as well, which pays the same per-byte call for the prompt.

### P5. Optimise the hot path for time, not size (S, Hypothesis)

Everything is compiled `-os` (`scripts/build.py:138`). The per-byte loops that
run during a reply (`sse.c`, `chunked.c`, `json.c`, `utf8.c`, `apistream.c`,
`console.c`) total about 10 KB of code (Measured, link map); compiling those
with `-ot` might cost 1-2 KB and buy measurable throughput. Profile with P8's
benchmark before and after; the screen path itself (`con_putc` to `vfill` with
a retrace wait) is already far faster than tokens arrive.

Candidates the `lib/` review picked out, all unmeasured:

- `utf8_to_cp437` has no fast path for plain ASCII; every byte takes the
  general decode (`lib/utf8.c:111-128`). It runs on all streamed text and all
  tool arguments.
- `sse_feed` runs its state-machine switch for every byte of a data line
  (`lib/sse.c:64-129`); inside `F_DATA` it could copy a run up to the next
  newline at once.
- `search` folds case with two `toupper` calls per comparison at every
  position of every line (`lib/search.c:26-32`). Folding the needle once and
  using a 256-byte table would make the default (case-insensitive) search
  several times cheaper.

### P6. Relaunch round trip (S-M, Hypothesis)

A `full_memory` run costs about 11 s on the 8088 beyond the program's own
time (ROADMAP). Two parts of that are ours:

- `hist_open` scans the entire history byte by byte to find the last complete
  message (`lib/history.c:23-53`, `:62`). On a resume the harness closed the
  file cleanly a moment ago, so the boundary could travel in RESUME.TXT and
  the scan be skipped (keep the scan for a normal open).
- With P1, the resumed harness continues the thread instead of re-uploading
  everything.

The 5-second hold (U1) is also paid on every relaunched run.

### P7. Fewer rounds: parallel tool calls (M, Read)

Requests set `disable_parallel_tool_use` (`lib/apireq.c:111`) because there is
one 16 KB arguments buffer. The model would otherwise batch independent calls
(read three files, then answer) into one round. One buffer is still enough if
the tools run after `message_stop`: the assistant message is already in the
history file with every `tool_use` input, so the harness can walk that message
on disk, load one call's input at a time, run it, and write all the
`tool_result` blocks into a single user message. With P1 in place the saving
per avoided round is smaller, so decide after P1.

## 2. Reliability

### R1. The system prompt can change in the middle of a conversation (S-M, Read + Docs)

`Chat::resume` calls `loadMemories()` (`src/harness/chat.cpp:111`), which
lists `C:\MEMORY` afresh. If the model wrote a memory earlier in the same
conversation, or the user deleted one with F4, the `{memories}` text in the
system prompt differs after the first `full_memory` run. PROMPT.TXT and
TOOLS.JSN are also re-read for every request, which the README presents as a
feature ("an edit on the DOS PC applies to the next message").

Two consequences:

- The prompt cache misses from the first byte, so that round is billed and
  processed uncached.
- Opus 5.5 binds each thinking block to the conversation prefix that produced
  it (system prompt, tools, earlier messages). The message threads page puts
  it plainly: changing `system` or `tools` "makes the `thinking` and
  `redacted_thinking` blocks already in the conversation invalid, and the
  create request can return a 400 error." Every retry of that request fails
  the same way, so the conversation is dead until F2. Whether a given account
  gets the 400 today does not matter to the design: the client has to keep
  the prefix byte-identical for the life of a conversation, and when it
  cannot (a chat reopened under a newer prompt or tool file), leave the
  thinking blocks out of what it sends.

Fix: P2 (freeze the prefix), or as a stopgap carry the memories text in the
relaunch state. As a safety net, send
`"thinking":{"type":"adaptive","block_binding":{"prefix_mismatch_behavior":"drop_block"}}`
with beta `thinking-binding-controls-2026-08-01`, which makes the API drop
invalidated thinking blocks instead of failing the request.

The other history edits the client makes are fine under this rule: rollbacks
only remove trailing messages, and the merge of user text into a previous
`tool_result` message (`hist_reopen_last`, `lib/convo.c:37`) only happens
while that message is the last one, with no reply kept after it.

### R2. Unknown content blocks end the turn (M, Read)

`blockStart` fails on any block type other than `text`, `thinking`,
`redacted_thinking` and `tool_use` (`lib/apistream.c:180`). That is honest,
but it means any API addition that puts a new block in a reply (a fallback
marker, a server tool's result, a compaction block) breaks every turn until
the client is updated. A generic path that copies an unknown block's JSON
verbatim into the history, using the same streaming route as big signatures,
would make the client forward-compatible and is a prerequisite for F7.

### R3. A stream error is not retried (S, Read)

HTTP 429, 529 and 5xx before the stream are retried twice
(`chat.cpp:479-491`). An `error` event inside a 200 stream, which is how
`overloaded_error` arrives once streaming has begun, sets `streamRc` to -1 and
ends the turn (`chat.cpp:386-391`). If nothing has been shown yet it could
take the same retry path.

### R4. Stack headroom is unknown (S, Hypothesis)

The stack is 4,096 bytes (`scripts/build.py:205`) and stack checks are off
(`-s`, `build.py:138`). The deepest path I can construct is a turn inside
`stream`, into `waitRead`, `Http::read`, `fill`, the idle hook, `handleKey`,
`note`, `con_write`, a word wrap (160 bytes of locals) and a scroll, with the
timer and packet interrupts landing on top. I have no evidence that it
overflows. It is cheap to find out: fill the stack with a pattern at startup
and log the high-water mark at exit.

Related: `Http::read` calls itself when a read produced only chunk framing
(`lib/http.cpp:190`). Depth is small in practice; a loop removes the question.

### R5. A program that hangs under `run` takes the session with it (M, Read)

`run` has no time limit, and Ctrl-C is deliberately ignored while the child
runs (`lib/shell.c:280-283`). A program in an endless loop means a reboot.

- For relaunched runs the state survives (RESUME.TXT stays until the resume
  connects), but nothing tells the user. CLAWDOS.EXE or AUTOEXEC.BAT could
  notice the file at start and offer `CLAWDOS -resume`; the model then learns
  "the launcher left no result" (`harness.cpp:1228`), which could say the PC
  was restarted.
- For in-process runs the conversation is on disk but cannot be reopened (F1).
- A real kill switch is harder: a Ctrl-Break hook that ends the child through
  INT 21h AH=4Ch when DOS is not busy. Worth a spike after F1.

### R6. Startup needs the tunnel (S, Read)

If the first connect fails, `fatal` shows the error and exits
(`harness.cpp:1358`). A "press R to retry" there, or starting disconnected and
connecting on the first message (the request path already reconnects,
`chat.cpp:435`), would save a restart when the tunnel was simply not running
yet.

### R7. Output capture sees only teletype output (S, Read)

The hook records INT 10h function 0Eh (`shell.c:39-65`). A program that mixes
teletype output with direct video writes or INT 10h function 09h/13h returns
only the teletype part; the screen snapshot is used only when nothing at all
was captured (`shell.c:403`). Appending the final screen text when it holds
rows the capture lacks would cover TUI-style programs.

### Tool executors (B1-B9)

From the background review of `lib/`. I re-read the code for B1 to B5 and
confirm them; B6 to B9 are the reviewer's, traced by them and not re-checked
by me.

**B1. Tabs in tool arguments become four spaces (S, Read).** `targ_set`
converts every argument with `utf8_to_cp437` (`lib/toolapi.c:37`), whose
fallback table maps U+0009 to four spaces (`lib/utf8.c:62`, `:70`; the unit
test at `tests/unit/dosunit.c:397` asserts it). That is right for the screen
and wrong for arguments:

- `read` returns real tabs, so the model's `old` text for `edit` contains
  tabs, arrives as spaces, and fails with "old text not found" on any tabbed
  line. The model cannot work around it.
- `write` silently replaces the tabs in every file it writes.

Fix: keep 0x09 on the argument path and expand tabs only when drawing.

**B2. Results over 4,096 bytes are cut with no marker (S, Read).**
`tout_puts` stops copying at `TOOL_OUT_MAX` and sets `truncated`
(`lib/toolapi.c:120-130`). `read` and `search` notice the flag and stop, then
try to append their "more lines from N; use start" or "+N more matches" line,
which is dropped because the buffer is already full (`lib/dosfs.c:201-214`,
`lib/search.c:49`, `:172`). `dir` loses "+N more" the same way. A hundred
lines of C at 40 columns is about 4.6 KB, so a default `read` of a source file
often ends mid-line and the model has no way to know it did not see line 100.
Fix: reserve room for the trailer and stop before a line that will not fit.

**B3. `write` truncates the target before the new text is safe (S-M, Read).**
`tool_write` creates (truncates) the file and then writes
(`lib/dosfs.c:241-261`). On a full disk the old contents are gone, a partial
file remains, and the result says only "disk full". `memory_write` has the
same shape (`lib/memory.c:228`). `edit` already writes a temporary file and
renames; `write` could do the same.

**B4. `append:"true"` overwrites (S, Read).** `append` is read with
`targ_num("append", 0)` (`lib/dosfs.c:229`), and `targ_num` returns the
default for text it cannot parse (`lib/toolapi.c:61-70`). The schema types the
flag as a string holding "1", so a model that sends `"true"` gets an
overwrite. `all` and `case` are ignored the same way. Fix with F8: boolean
and integer types in the schema, and an error instead of a default for
unparsable values.

**B5. `hist_open` can empty a conversation (S, Read).** If the 512-byte
`malloc` in `scanBoundary` fails it returns 0 (`lib/history.c:29-30`), and
`hist_open` then truncates the file to that "boundary" (`history.c:63-65`). A
read error mid-scan truncates similarly. It needs an out-of-memory moment at
open, so it is rare, but the loss is total. Use a static buffer and fail the
open instead.

**B6. A Ctrl-Z at the end of CLAWDOS.CFG stops the harness starting**
(`lib/cfg.c:76`). A config saved by EDLIN or `COPY CON` ends in 0x1A, which
parses as an unknown setting. Stop parsing at 0x1A.

**B7. `edit` and line ends** (`lib/edit.c:107-116`, `:144-148`). A file with
no CRLF yet (one line, or empty) gets bare LF for added lines; lone CRs are
dropped; an `old` containing `\r\n` never matches.

**B8. `read` cuts lines at 160 columns with no marker** (`lib/dosfs.c:190`),
so an `edit` built from that line cannot match. `search` matches only the
first 255 characters of a line (`lib/search.c:11-12`).

**B9. Smaller ones.** Control characters do not round-trip (ESC reaches the
model as an arrow glyph and comes back as "<-", so ANSI sequences in batch
files cannot be written; `lib/utf8.c:25-30`). DOS device names are not
rejected as memory or file names (`memory_read` of `CON` would wait on the
keyboard). Forward slashes in paths fail. `edit` refuses files over 60,000
bytes while `write` takes 12 KB per call, so a larger file is effectively
uneditable. An interrupted tool discards its partial output
(`tools.cpp:451`).

## 3. API compatibility (Opus 5.5)

The configured model is `claude-opus-5-5`. Three of its behaviours do not
match what the client assumes.

### A1. `max_tokens` 16000 is too small once thinking is counted (S, Docs)

The body hard-codes `"max_tokens":16000` (`lib/apireq.c:100`). On Opus 5.5
thinking is always on, counts toward `max_tokens`, and grows with effort; the
reference suggests 64K as a starting point for agentic coding turns. F5 lets
the user pick `xhigh` and `max`. At those levels a round that thinks hard and
then writes a file (the `write` tool allows 12,000 bytes, about a quarter of
the budget) will end in `[reply cut off: too long]` with the tool call
discarded. `max_tokens` is a ceiling, not a target, so raising it costs
nothing when the reply is short. Make it a config line and default to 32-64K.

### A2. The model's progress notes are invisible (S-M, Docs)

On Opus 5.5, text the model writes between tool calls comes back as
progress-update thinking blocks, and with the default display their text is
empty. `uiThinking` is an empty function (`harness.cpp:801`) and thinking text
only goes to the history. So during a ten-round turn the user sees tool
headers and a spinner, and never "the compile failed on line 40, fixing the
declaration".

Fix: send `"thinking":{"type":"adaptive","display":"updates"}` with beta
`thinking-display-updates-2026-08-18` and draw non-empty thinking text dim,
above the tool header it precedes. The blocks still go to the history
unchanged. The system prompt can also ask for one line of intent before the
first tool call.

### A3. Conversation-bound thinking blocks

Covered by R1. The practical rule: within one conversation, never change the
system prompt, the tools, or an earlier message that has been sent.

### A4. Refusals (S, Docs)

A `refusal` stop reason shows "The model declined to answer that" and removes
the user's message (`chat.cpp:550`). The response's `stop_details` carries a
category and an explanation; showing them would tell the user what happened.
ClawDOS's daily work (poking memory and ports, writing assembly, driving a
debugger) is the kind of low-level material a security classifier can
misjudge. The API offers server-side fallbacks (`"fallbacks":"default"`, beta
`server-side-fallback-2026-07-01`) that hand a declined request to another
model. Worth deciding whether you want that; note that a fallback reply is not
stored in a thread (P1), so it forces a replay.

### A5. Smaller items (S, Docs)

- **Cache lifetime.** Cache entries live five minutes from the start of the
  request that wrote or read them. A person typing on an XT keyboard between
  turns will often exceed that, and the next request rewrites the cache at
  1.25x. A `cachettl 1h` config option (2x write, then cheap for an hour) is a
  few bytes in `api_body`; both `cache_control` markers must carry the same
  TTL. Decided: a `cachettl` config line, five minutes
  when it is absent.
- **F5 mid-conversation.** A top-level effort change invalidates the message
  cache (docs/API.md already notes one uncached request). A per-message effort
  change exists as a beta (`mid-conversation-output-config-2026-07-01`) and
  avoids it. Low priority; with P1 an effort change starts a new thread anyway.
- **Request ids.** `http_parse_head` keeps four headers (`lib/httpp.c`).
  Logging `request-id` on any non-200 makes a support question answerable.

## 4. User experience

### U1. The five-second hold after every run (S, Read)

`shell_run` ends in `hold(code, holdSecs)` (`lib/shell.c:343`, `:425`), which
waits for a key or for `holdSecs`, and the tool passes 5 for interactive
sessions (`src/harness/tools.cpp:210`, and `:204` for relaunched runs). A
build-and-test turn with ten runs spends 50 seconds waiting for a keypress
nobody owes it. The hold exists because it is the only moment the user can see
the program's output.

Decided: return to the chat as soon as the last command ends, and show the
last few lines of captured output dim under the Run header (they are already
in `t->out`), with a count of the lines above them. When nothing was captured
the screen snapshot's last rows serve the same purpose.

### U2. Notes during a streaming reply change its colour and indent (S, Read)

`note()` leaves the console at margin 4 with the note's attribute
(`harness.cpp:484-494`). `uiText` only resets them when `state == WAITING`
(`harness.cpp:787`). So anything that prints a note while `state ==
STREAMING` restyles the rest of the reply:

- F3 mid-reply: the remainder is drawn in the green or red of the safe-mode
  note, indented two extra columns.
- F5 or F1 mid-reply: the remainder is dim.
- A lost link after some text was shown: "[link lost, asking again]" is red,
  and the re-asked answer is drawn in red.

`ui_tool_header` already has the fix for its own case: it sets `state =
WAITING` so the next text starts a fresh bullet (`harness.cpp:867`). Doing the
same inside `note()` when streaming closes all of these.

### U3. Say what the machine is doing (S-M, Read)

The status row shows a random verb ("Percolating") for the whole wait. On this
hardware the phases are long enough to deserve names: sending (with a
percentage, since the length is known), waiting for the first byte, thinking
(a thinking delta arrived; `uiThinking` is the hook), replying. It also makes
a stalled upload distinguishable from a slow model.

### U4. Scrollback is 25 to 77 rows (M, Read)

Scrollback lives in the CGA's spare video memory: 77 rows at most
(`lib/screen.c:14`), and each time the window reaches the end of video memory
`rebase` keeps only 25 (`KEEP_ROWS`, `screen.c:15`, `:335`). A long reply
cannot be scrolled back to its start. Two ways out: keep scrolled-off rows in
EMS pages (the PicoMEM provides EMS and `lib/ems.c` exists), or re-render from
the chat file. The second also gives F1 its display.

### U5. Approvals (S-M, Read)

- The prompt is y/n per call (`tools.cpp:356`). An "a" answer, meaning allow
  this tool for the rest of the session, sits between asking every time and
  F3's everything-goes.
- `edit` asks with "replaces 3 lines with 5" (`tools.cpp:293`). The old and
  new text are in the arguments; showing them (red and green, a few lines
  each) lets the user judge the change instead of its size.

### U6. Smaller items

- **Light markup.** The prompt forbids Markdown because it would show
  literally. Drawing `` `code` `` in a second colour and `**bold**` in bright
  white is a small state machine in `uiText`, and the prompt could then allow
  both.
- **Cost.** The box shows tokens and turns. A per-session dollar figure needs
  two prices per provider in the config and the usage the log already records.
- **Housekeeping.** Chats and CLAWDOS.LOG grow without bound on a 32 MB disk.
  F1's list is the natural place for a delete key; the log could roll over at
  a size.

## 5. Functionality

### F1. Reopen an earlier conversation (M, Read)

Every chat is on disk in the API's own format and `hist_open` already repairs
a file that ends mid-message. What is missing is a list (`/chats`, in the
style of the F4 memory manager) and a way to show the tail of a reopened chat.
Thinking blocks in an old chat stay valid only if the frozen prefix (P2) is
reused, which is one more reason to store it beside the chat.

### F2. `/compact` (M, Docs)

Nothing shrinks a long conversation; without P1 its whole length is uploaded
every round. The shape the API reference recommends for client-side
compaction is the simple one: ask the model for a summary, start a new
conversation whose first message is that summary, and replay nothing else.
For ClawDOS that is a new CHATS file, so the old one stays as the record. It
could be offered automatically when the history passes a size.

### F3. Screenshots, roadmap M6.3 (M-L, Docs)

Still the most valuable missing tool for graphics work ("fixes were best
guess, untested visually"). Notes for the design:

- A `tool_result` may contain image blocks, so the screenshot can be the
  result of `run` itself or of a separate tool.
- PNG does not need a compressor: deflate has a "stored" mode, so the encoder
  is headers, CRC-32 and Adler-32 over 16 KB. About 22 KB once base64-encoded.
- Without P1 each screenshot is re-uploaded on every later round. With P1 it
  goes up once (the first image in a thread forces one replay).

### F4. Skills beside memories (S-M, Read)

The untracked `image/memory/DEMOFX.TXT` calls itself a skill and points at
topic files in `C:\DEVEL\DOC\DEMOFX`. That is a good pattern living in the
wrong directory: it shares the 4,000-byte memory limit, the model can
overwrite it with `memory_write`, and the user can delete it with F4. A
`C:\SKILLS` directory listed in the prompt the same way, read-only to the
memory tools and shipped by the image build, keeps authored knowledge apart
from what the model learned.

### F5. Round limit (S, Read)

`MAX_ROUNDS` is 25 per user message (`chat.cpp:26`). A compile-fix loop on a
real program reaches that. Make it a setting, or ask "25 tool calls so far,
continue? (y/n)" at the cap.

### F6. Web access (L, Docs)

The API's server-side web search and fetch tools run on Anthropic's side, so
the PC only needs to carry their result blocks in the history. That needs R2
(unknown blocks passed through), large events streamed to disk, and the
`pause_turn` stop reason. Results are big, so it is only attractive after P1.

### F7. Already on the roadmap

M6.4 (snapshot and restore) and M7 (a reusable `lib/`). Native TLS or a tunnel
inside the PicoMEM firmware would remove the last off-PC piece and S1 with it.

### F8. Tool schemas (S-M, Read)

From the review of `image/clawdos/TOOLS.JSN` against the executors:

- **Types.** `start`, `lines`, `append`, `all`, `case`, `len`, `bytes` and
  `value` are typed `"string"` while `full_memory` is a boolean. `setArg`
  already handles JSON numbers and booleans (`tools.cpp:406-409`), so the
  schema can say `integer` and `boolean`; that removes B4's cause.
- **State the caps.** `read`, `dir` and `search` do not mention the ~4,000
  characters per result, 160-column lines, 80 matches, or that `search` does
  not recurse. `read` never reports the file's total size or line count, which
  would let the model plan its paging.
- **Mixed number bases.** `port` takes a hex `port` and a decimal `value`;
  `peek` takes a hex address and a decimal `len`.
- **Missing tools.** Copy, remove directory, a recursive `dir`, and
  `memory_delete` (today the model has to use `delete`, which asks). A memory
  append would save resending up to 4 KB to add one line.
- **Working directory.** Nothing saves or restores it around `run`, and DOS
  has one current directory for everything, so a `CD` in a command can move
  later relative tool paths (suspected, not traced). Either report it in
  `sysinfo` and tell the model to use absolute paths, or restore it.
- **Unit test gaps.** `dosunit.c` covers the parsers well but has no
  file-level tests for `read`, `write`, `dir`, `edit` or `search`: the 4,096
  cap, tabs through `targ_set`, CRLF preservation, Ctrl-Z, and the relaunch
  files' round trip are all untested. B1 and B2 would have been caught there.

## 6. Security

Proportionate to a hobby machine on a home LAN. The README already tells the
user to use a dedicated key with a spending limit, which is the right first
defence. I checked S1 to S3 myself; S4 is the host-side reviewer's.

### S1. Where the key lives (S-M, Read)

- The build writes the key from `.env` into CLAWDOS.CFG on the image
  (`scripts/build.py:420-428`), and the PC sends it in clear over WiFi to the
  tunnel in every request.
- Every image the build makes carries it, including the throwaway test
  images: six files in `build/` contain a `key sk-ant-` line right now
  (CLAWDOS.IMG, test.img, test8088.img, memshot.img, memtest.img,
  CLAUDEXT.IMG). The mock tests do not need a key; a `with_key=False` for
  test images removes five of the six copies.
- Nothing key-like is in tracked files or the build logs, and `/build/`,
  `*.img` and `/.env` are ignored. No commit risk found.

Decided: an opt-in tunnel mode (`tunnel.py --key-file`) in which the tunnel
adds the `x-api-key` header, so the image and the LAN never see the key. The
tunnel then has to find the end of the request head, which is the one step it
takes away from "copies bytes, nothing else"; the plain mode stays the
default, and socat or stunnel remain drop-in replacements for it.

### S2. The tunnel accepts anyone (S, Read)

`tunnel.py` listens on `0.0.0.0` by default with no allowlist
(`tunnel/tunnel.py:92`). Today that only offers LAN hosts a TLS relay to one
fixed host, which is harmless. With key injection (S1) it would let any LAN
host spend the key, so `--allow <the PC's IP>` has to come with it. The mock
also binds `0.0.0.0` (`tunnel/mockapi.py:327`); `127.0.0.1` would do for
everything except the real PC.

### S3. Certificate checking (Read)

`tunnel.py` verifies the upstream certificate (default SSL context with
`server_hostname`, `tunnel.py:67`, `:78`). Good.

### S4. The documented stunnel alternative (S, not re-checked)

The stunnel stanza in `tunnel.py`'s docstring and `docs/API.md` has no
`verifyChain` or `checkHost` line. The reviewer's reading is that stunnel does
not verify the server by default, which would make that variant open to a
man-in-the-middle. Worth confirming against the stunnel manual and adding the
two lines.

## 7. Tests, build and tooling

From the host-side review. I re-checked T2, T3 and the first two bullets of
T4; the rest are the reviewer's, traced by them. The host unit tests pass
(`python3 tunnel/test_mockapi.py`: 9 tests, OK).

### T1. Branch and CI (S, Measured)

`relaunch` is 51 commits ahead of `main` and 0 behind; `main` last moved on
2026-10-31. The branch now holds the launcher split, the clock change, the UI
themes and F5, so `main` no longer describes the project. CLAUDE.md says to
commit to main freely; merging (or fast-forwarding) would match that. There is
no `.github/` directory, so nothing runs the tests on a push. `make check` is
one command, but it needs the toolchains, a display and the internet, and
`setup-tools.py` has no Linux path. A first CI stage could be `make test`
plus pyright plus a compile-only build.

### T2. Updating the real PC's image is a manual step (S-M, Read)

`build_image` always starts from an empty filesystem
(`scripts/build.py:386-392`), so a new CLAWDOS.IMG cannot replace the one on
the SD card without discarding `C:\MEMORY`, the chats, the log and whatever
Claude built on the machine. The working practice is to build a new image and
copy the changed binaries into the old one with `fatimg`. That keeps the data,
but it depends on remembering every file that changed: a new HARNESS.EXE
beside an old TOOLS.JSN, PROMPT.TXT or NETUP.BAT is a mismatch nothing
detects.

Make it one command, `build.py --update IMG` (and `make update IMG=...`):

- Walk the manifest and replace everything the build owns inside the existing
  image, then print what changed.
- Leave alone what the PC owns: `C:\MEMORY`, `C:\CLAWDOS\CHATS`, the log, and
  the tunnel and key lines of CLAWDOS.CFG.
- Shipped memories such as DEVTOOLS.TXT sit on the line between the two,
  because the model may have rewritten them. Replace them only on request.
  F4 removes the problem: skills belong to the build, memories to the PC.

### T3. Tests that cannot fail, and tests that can lie (S, Read)

- `net.bat` always prints the done marker and its Makefile line has no
  `--expect` (`Makefile:53`), so it passes with no network. Expect a ping
  reply.
- `boottest.py` starts the test mock on port 18023, the same port `make mock`
  uses, and never checks that it started (`scripts/boottest.py:35`). With an
  interactive mock already running, the test's mock fails to bind and the
  guest silently talks to the stale one.
- Exit codes are echoed but not asserted, and there is no way to say "this
  text must not appear".
- `clock.bat` needs the internet and sits inside `boottests`; a
  `boottests-online` target would let the rest run offline.

### T4. Mock fidelity (M)

The mock is what makes the boot tests possible, so its blind spots are the
client's blind spots.

- **It validates almost nothing.** Only role alternation is checked
  (`tunnel/mockapi.py:58-73`): not `tool_use`/`tool_result` pairing or ids,
  not empty text blocks, not that thinking signatures come back. The
  history-repair logic after Esc or a dropped link is exactly the code that
  could produce a subtly invalid request, and the mock would accept it. A
  strict validator in the mock is the highest-value test change.
- **The tool tour is exactly 25 calls**, the same as `MAX_ROUNDS`
  (`tunnel/mockchat.py:98-132`, `chat.cpp:26`), and the mock ignores
  `tool_choice`. One more tour step would trip the cap, and the cap itself
  has no test.
- **Never sent:** `ping`, `thinking_delta`, `redacted_thinking`, two
  `tool_use` blocks in one reply, `max_tokens` inside a tool call.
  `apistream.c` has code for most of these that no test runs.
- **Thin error coverage:** one 529 with `retry-after: 1` and one mid-stream
  error. Missing: 400, 401, 429, retries exhausted, a large `retry-after`
  (`chat.cpp:480` uses the value uncapped, so a server asking for 600 seconds
  gets them), an HTML error body, a drop during a tool round.
- No boot test covers `!error`, `!refuse`, `!cutoff`, a denied y/n, or
  `-resume` after a failed resume, though the mock has the first three.

P1, P2, A1 and A2 all change the request, so the validator should come first.

### T5. The 8088 gate (M)

DOSBox-X emulates an 80186, and an 8088 does not trap on 186 instructions, so
only code that actually runs in 86Box is checked. Today that is the dev-tools
batch; DOSUNIT and the mock tool tour never run there (`test8088.py` has no
`--mock`). Two additions: run the unit tests and the tour in 86Box, and
disassemble each `.obj` with `wdis` at build time to reject 186+ mnemonics,
which checks every path whether a test reaches it or not.

### T6. Build (S-M)

- Every build recompiles all 69 units serially (`build.py:160-178`), and the
  build stamp is force-included into each of them. Time not measured.
- `setup-tools.py` downloads are not pinned or hashed; the PicoMEM drivers
  come from a `main` branch and run at boot (`setup-tools.py:48-56`, `:141`).
- A `.DS_Store` inside `image/clawdos/` fails the build with an uncaught
  error (`build.py:343-345`).
- Substituting the tunnel and key lines drops their `\r`, so those two lines
  of CLAWDOS.CFG end in a bare LF (reproduced by the reviewer). Harmless to
  the parser, odd in EDIT.
- Every file on the image is dated 1980-01-01.

### T7. Boot scripts (S)

- "Type NETUP to retry networking" probably cannot work: the Crynwr drivers
  refuse a second load at INT 60h, so a retry falls through to "No network
  card found" (suspected, not run). Remember that a driver is loaded and jump
  to DHCP.
- `TZ=EST5EDT` is hard-coded in AUTOEXEC.BAT; make it a build setting.
- Card detection in emulators hangs on one 5-second DHCP attempt
  (`NETUP.BAT:12-16`); a slow reply loads the wrong driver. A `NETCARD`
  override would make tests deterministic.
- When the tunnel cannot reach the API (DNS, certificate), it closes the
  client connection without a word (`tunnel.py:68-71`), and the PC cannot
  tell that from "tunnel not running". A startup self-test in the tunnel
  (resolve, handshake, print the LAN addresses to pass to `--tunnel`) would
  catch most of it before the PC is involved.

## 8. Documentation drift

Found while reading; none is serious, all are cheap.

| Where | What |
|---|---|
| `docs/ROADMAP.md` | M6.2 is unchecked although the relaunch works and is tested; memory figures (355 KB resident, ~285 KB for programs) disagree with the README (310 KB, ~190 KB) and DEVTOOLS.TXT (190 KB); the 19 s first-token figure predates buffered upload. |
| `README.md` | "Tools on DOS" has an empty gap where the tool table used to be (the table is now under "Available Tools"). The Features list is three words. |
| `src/harness/harness.cfg` | The comment about "short lines" describes the retired bridge protocol (see P3). |
| `build/` | Stale outputs from removed programs (`claw.exe`, `cxtunit.exe`, `hello.exe`, a `bridge` entry in `settings.json`); `make clean` clears them. |

## 9. Plan

The order agreed after the decisions above. Each batch can ship on its own
and ends with `make check`.

1. **Baseline.** Merge `relaunch` to `main` (T1). Then P8 (`up=` and `len=`
   in the log, the 86Box benchmark) and R4 (the stack high-water mark).
2. **Tool correctness.** B1, B2, B4 with the schema types from F8, B5 and B6,
   together with the file-level unit tests that would have caught them.
3. **Quick wins.** U1 (no hold, last lines of output in the transcript), U2,
   A1, R3, minified TOOLS.JSN, keyless test images (S1), T3.
4. **A mock that can say no.** The request validator from T4, before the
   request changes.
5. **The request.** P2 (the frozen prefix, which settles R1), P4, and A2 with
   the `drop_block` safety net. The mock and API.md change with them.
6. **Threads.** P1, measured against batch 1.
7. **Deployment and the key.** T2 (`build.py --update`), then S1 and S2 (the
   tunnel's key mode with an allowlist).
8. **Features.** F4 skills (DEMOFX is already heading there), F1 `/chats`,
   F3 screenshots. F2 `/compact` waits until sessions grow.
