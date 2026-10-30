# XTBridge protocol (version 1)

Plain TCP, default port **8023**. Every message is one line ending in `\n`
(a preceding `\r` is ignored). Text is **code page 437** bytes, so the DOS side
can put bytes straight on screen. A line is `VERB` or `VERB payload`. Client lines
may be up to 4096 bytes; server payloads are at most 500 bytes so a small
receive buffer on the XT always holds a whole line.

## Server → client

| Line | Meaning |
|---|---|
| `HELLO XTBRIDGE 1 [AUTH] SESSION <id> TIME <time>` | Greeting on connect. ` AUTH` if the client must authenticate first; `<id>` names this connection's conversation for a later `RESUME`; `<time>` is the bridge's local time as `2026-10-28T14:03:07`, for a client without a clock of its own (CLAUDE.EXE sets DOS's). Clients look for the words they know and skip the rest. |
| `OK` | Command accepted (`AUTH`, `NEW`, `RESUME` as `OK resumed`). |
| `INFO <text>` | Backend description (reply to `INFO`). |
| `TXT <text>` | A piece of the reply. Never contains a newline; append it to the current line. |
| `NL` | Line break in the reply. |
| `END` / `END stopped` | Reply finished (or was cancelled with `STOP`). |
| `ERR <text>` | Something went wrong; the reply (if any) is over. |
| `PONG`, `BYE` | Replies to `PING`, `QUIT`. |

## Client → server

| Line | Meaning |
|---|---|
| `AUTH <token>` | Shared secret, when the bridge was started with `--token`. |
| `RESUME <id>` | After a lost link: carry on the conversation the earlier `HELLO` named (history, backend, memories). The bridge keeps a dropped connection's conversation for 30 minutes, and takes it over from a connection it still thinks is open. `ERR unknown session` otherwise: carry on with the new one. |
| `MSG <text>` | Send a user message. The reply streams back as `TXT`/`NL` lines, then `END` or `ERR`. One at a time (`ERR busy` otherwise). |
| `STOP` | Cancel the reply in progress. |
| `NEW` | Start a new conversation. |
| `LOG text` | Diagnostics for the bridge's log (CLAUDE.EXE's link-loss and clock reports); no reply. |
| `INFO`, `PING`, `QUIT` | Housekeeping. CLAUDE.EXE pings a bridge that has been quiet for 60 s (20 s while waiting for a reply) and treats no answer within 15 s as a lost link. |

A `MSG` starting with `/model` (or `/provider`) is answered by the bridge
itself rather than the model: `/model` lists the AI providers it knows, and
`/model provider[:model]` or `/model model` switches the conversation to another
backend, keeping its history. The reply arrives as ordinary `TXT`/`NL`/`END`
lines, so clients need no special support; `INFO` reflects the new backend.

## Tool calls

The model can use tools that run on the XT. The bridge holds their schemas
(`bridge/tools.py`) and runs the tool loop with the model; the client only
executes calls. While a reply is in progress the bridge may send a call:

| Server → client | Meaning |
|---|---|
| `TOOL <id> <name>` | A call begins. `id` is short (`t1`, `t2`, ...). |
| `ARG <key> <chunk>` | An argument, escaped. Repeated lines for the same key append. |
| `CALL` | All arguments sent: run it now. |

| Client → server | Meaning |
|---|---|
| `OUT <chunk>` | Part of the result, escaped. Repeats. |
| `DONE <id> <status>` | Finished: `ok`, `error`, `denied` (the user said no) or `stopped` (Esc). |

Escaping: `\n` is a newline and `\\` a backslash; chunks never split an escape.
Every argument is a string (numbers in decimal, booleans `1`/`0`). Server lines
keep the 500-byte payload limit; a result is at most 4 KB. Text the model wrote
before a call is always ended with `NL`, so the call starts on a fresh line.

Calls run one at a time, then the reply continues with more `TXT`, `TOOL` or
`END`. After `DONE <id> stopped` the client also sends `STOP`; the bridge ends
the turn (`END stopped`) and tells the model the call was interrupted. At most
25 rounds of tool calls happen per message. `OUT`/`DONE` with no call pending
are ignored. A client that runs no tools answers every `CALL` with
`DONE <id> error`.

```
S: TXT Let me look at your startup file.
S: NL
S: TOOL t1 read
S: ARG path C:\AUTOEXEC.BAT
S: CALL
C: OUT     1 @ECHO OFF\n    2 PATH C:\DOS;C:\NET\n
C: DONE t1 ok
S: TXT Your PATH is missing ...
S: END
```

The bridge keeps the conversation history, so the XT only ever holds what is on
screen. Word wrapping is the client's job: the bridge doesn't know the screen
width.

Try it from any modern machine: `python bridge/chat.py <bridge-host>`.
