# XTBridge protocol (version 1)

Plain TCP, default port **8023**. Every message is one line ending in `\n`
(a preceding `\r` is ignored). Text is **code page 437** bytes, so the DOS side
can put bytes straight on screen. A line is `VERB` or `VERB payload`. Client lines
may be up to 4096 bytes; server payloads are at most 500 bytes so a small
receive buffer on the XT always holds a whole line.

## Server → client

| Line | Meaning |
|---|---|
| `HELLO XTBRIDGE 1` | Greeting on connect. Ends in ` AUTH` if the client must authenticate first. |
| `OK` | Command accepted (`AUTH`, `NEW`). |
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
| `MSG <text>` | Send a user message. The reply streams back as `TXT`/`NL` lines, then `END` or `ERR`. One at a time (`ERR busy` otherwise). |
| `STOP` | Cancel the reply in progress. |
| `NEW` | Start a new conversation. |
| `INFO`, `PING`, `QUIT` | Housekeeping. |

A `MSG` starting with `/model` (or `/provider`) is answered by the bridge
itself rather than the model: `/model` lists the AI providers it knows, and
`/model provider[:model]` or `/model model` switches the conversation to another
backend, keeping its history. The reply arrives as ordinary `TXT`/`NL`/`END`
lines, so clients need no special support; `INFO` reflects the new backend.

The bridge keeps the conversation history, so the XT only ever holds what is on
screen. Word wrapping is the client's job: the bridge doesn't know the screen
width.

Try it from any modern machine: `python bridge/chat.py <bridge-host>`.
