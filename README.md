# Claude-XT: Claude on the IBM PC XT

DOS software that connects a real IBM 5160 (8088 @ 4.77 MHz, 640K, CGA,
PicoMEM WiFi) to the modern world. First app: **CLAUDE.EXE**, a Claude chat
client that runs on the XT.

```
 IBM XT  CLAUDE.EXE ──TCP (PicoMEM WiFi)──> bridge/xtbridge.py ──HTTPS──> Claude API
```

## Quick start

1. **Tools** (once): `.\scripts\setup-tools.ps1`
2. **Build** the disk image, telling it where the bridge will run:
   `python scripts/build.py --bridge 172.16.0.100:8023` → `build\CLAUDEXT.IMG`
   (the address is remembered; on the XT it lives in `C:\NET\MTCP.CFG` as `CLAUDE_BRIDGE`)
3. **Bridge** on the PC: `pip install -r bridge/requirements.txt`, set
   `ANTHROPIC_API_KEY` (in the environment or a `.env` file in the repo root, or
   `ant auth login`), then `python bridge/xtbridge.py`.
   Other providers: `--provider openrouter`, `zai`, `openai` or `ollama` (keys in
   `OPENROUTER_API_KEY`, `ZAI_API_KEY`, `OPENAI_API_KEY`); see "Other models" below.
   No API key yet? `--mock` runs an offline stand-in for Claude (see below). The XT connects in on TCP port 8023,
   so allow it through the firewall (admin PowerShell):
   `New-NetFirewallRule -DisplayName XTBridge -Direction Inbound -Protocol TCP -LocalPort 8023 -Action Allow`
4. **Real XT**: copy `CLAUDEXT.IMG` into the `HDD` folder on the PicoMEM's microSD,
   pick it as HDD0 in the PicoMEM BIOS menu (Disk), make sure NE2000 is enabled
   (Other menu), and boot. Networking comes up automatically; type `CLAUDE`.
5. **Emulator**: `python scripts/run86box.py` boots the same image on an
   emulated XT (8088, CGA, XTIDE, NE1000 with NAT networking).

### System prompts

The system prompt comes from a file: `bridge/prompts/NAME.txt`, chosen with
`--prompt NAME` (default: `default.txt`). The bridge appends its add-ons after
it: how the XT's tools work, and the index of the XT's memories. Since
the file is the whole prompt, it must tell the model about the screen (plain
text, 80 columns, no Markdown): copy `default.txt` to start a new one. `{name}`
becomes the assistant's name. Files are re-read at the start of every
conversation, so an edit applies from the next chat (F2 on the XT) without
restarting the bridge.

### Programming on the XT

The image carries development tools that were tested on an 8088 at 4.77 MHz
(`python scripts/xttest.py image/tests/devtools.bat` re-runs that test in 86Box):

| Tool | What | Hello world |
|---|---|---|
| `TINYASM` | NASM-syntax assembler, makes .COM files (BSD) | ~5 s |
| `WASM` | Wolfware assembler, MASM-like (MIT) | ~5 s |
| `JWASMR` | JWasm, full MASM syntax, .COM or .EXE (Open Watcom licence) | ~18 s |
| `TCC` | Turbo C 2.0, from your own disks (see below) | ~27 s |
| `BCC` | Bruce's C, K&R C to .COM (GPL); lowercase file names only | ~80 s |
| `GWBASIC` | GW-BASIC built from Microsoft's MIT-licensed source | ~6 s |
| `LDEBUG` | lDebug, a scriptable FreeDOS DEBUG (Fair License) | |

They live in `C:\DEVEL` (licences in `C:\DEVEL\DOC`) and `C:\TC`, and
`C:\MEMORY\DEVTOOLS.TXT`, Claude's first memory, tells it how to use them.
Examples are in `C:\TESTS\DEV`. Turbo C is commercial: the build copies it from
your own floppy images in `external/` (e.g. `Borland - Turbo C v2.0 - Disk 3 of
6 - Command Line and Utilities.img`) and leaves it out if they aren't there.
Pacific C was tried and hangs on an 8088. `docs/research/` has the survey of
what else runs on an XT and what a licensed copy would add.

### Memories

Claude keeps notes on the XT itself, in `C:\MEMORY\NAME.TXT` (first line: a
one-line summary; at most 4000 bytes each). At the start of every conversation
the bridge runs `memory_list` on the XT (you'll see a `Memories` line) and puts
the list into the system prompt, so Claude knows what it saved before and reads
a memory in full when it's relevant. The memory tools never ask in safe mode.
Delete a memory with `DEL C:\MEMORY\NAME.TXT`, or ask Claude to (that uses
`delete`, which does ask). The mock and replay backends don't list memories.

### Logs

The bridge keeps two logs, both gitignored:

- `bridge/xtbridge.log`: a debug log with every protocol line (rotating; `--log FILE`).
- `bridge/logs/YYYY-MM-DD/*.jsonl`: one file per XT connection, one JSON event per
  line: what you asked, Claude's replies, tool calls and results, and each API
  call's model, stop reason and token usage (`--convlog DIR`, `-` for none). Load
  it with `jq`, pandas or a few lines of Python.

### Testing without an API key

`python bridge/xtbridge.py --mock` answers like Claude would: a pause, then
text streamed in small pieces, with conversation memory. Nothing leaves your
machine. Messages starting with `!` run test scenarios; send `!help` on the XT
for the list. Among them: `!scroll` (60 lines), `!wide` (over-long word and
line), `!cp437` (character set), `!slow` and `!stall` (try Esc), and `!error`,
`!refuse`, `!cutoff`, `!drop` (failure modes). `--speed 3` makes it faster.
`python bridge/test_bridge.py` runs the protocol tests against it.

### Other models

The bridge isn't tied to Claude. Besides Anthropic's API it speaks the
OpenAI-compatible chat API that OpenRouter, Z.ai, OpenAI, Ollama, LM Studio and
most others offer, so nothing changes on the XT. Built-in providers:
`anthropic` (default), `openrouter`, `zai`, `openai`, `ollama`, `mock`.
`--provider NAME --model MODEL` picks where new chats start. On the XT, type
`/model` to list providers and `/model openrouter:z-ai/glm-4.6` (or
`/model zai`, or just `/model some-model` on the current provider) to switch
mid-conversation; the history carries over. To add or change providers, copy
`bridge/providers.example.toml` to `bridge/providers.toml` (format in
`bridge/providers.py`). API keys only ever come from environment variables.

CLAUDE looks like Claude Code: a welcome panel with Clawd, the conversation
scrolling above an input box pinned to the bottom, and a spinner while Claude
works. Enter sends (typing ahead while Claude replies queues the message), Esc
stops a reply or clears the input, arrows/Home/End/Del edit, Up/Down recall
earlier messages, PgUp/PgDn scroll back, F1 help, F2 new chat, F10 quits.
`CLAUDE host:port` overrides the bridge address; `-m text` sends a message at
startup and `-i text` pre-fills the input box (handy for hands-free tests).

### Tools on the XT

Claude can also act on the machine it's running on. The bridge offers the model
a fixed set of tools; CLAUDE.EXE runs them and shows each call in the
transcript (`● Read(C:\AUTOEXEC.BAT)` / `└ 24 lines`):

| Tool | What it does |
|---|---|
| `sysinfo`, `dir`, `read`, `peek` | Look at the system, directories, files (text or hex) and memory |
| `write`, `delete`, `mkdir`, `rename` | Change files |
| `run` | Run a DOS command on a plain DOS screen and read back what it printed |
| `poke`, `port` | Write memory, read or write I/O ports |
| `draw` | Draw a CGA picture from a GW-BASIC-style script (SCREEN, LINE, CIRCLE, PAINT, DRAW, PRINT...) |
| `play` | Play a GW-BASIC `PLAY` tune on the PC speaker |
| `textart` | Print coloured CP437 text art into the chat |
| `memory_list`, `memory_read`, `memory_write` | Claude's own notes across conversations, in `C:\MEMORY\NAME.TXT` |

**Safe mode** (on at startup, F3 toggles it, shown in the bottom row): tools
that change files, memory or ports ask `Allow ...? (y/n)` first; looking,
drawing, playing music and memories never ask. `-unsafe` starts with it off, and `-q`
quits once the `-m` message is answered (both for unattended tests). Esc
during a tool stops the reply. Programs started by `run` can't use the network
or be stopped with Ctrl-C, and get about 210K of memory.

With the mock bridge, `!tool NAME key=value ...` calls any tool (e.g.
`!tool read path=C:\AUTOEXEC.BAT`), `!draw` and `!play` are demos, and
`!tooltour` exercises every tool.

## Layout

| Path | What |
|---|---|
| `src/claude/` | CLAUDE.EXE |
| `lib/` | Reusable DOS pieces: `screen` (snow-free CGA, hardware scrolling, pinned footer), `console` (word-wrapping text stream with margins), `xtbridge` (bridge protocol on mTCP), and the tool executors: `toolapi`, `dosfs`, `memory`, `sysinfo`, `shell`, `basdraw` + `raster` + `gfx` (drawing), `play` + `sound` (music) |
| `tests/unit/` | CXTUNIT.EXE: unit tests for the hardware-free parsers, run in DOSBox-X |
| `bridge/` | XTBridge server and a test client (`chat.py`) |
| `src/boot/` | MBR and FreeDOS boot sector |
| `image/` | What goes on the disk image (`manifest.txt`, CONFIG/AUTOEXEC, tests) |
| `scripts/` | Build, image tool (`fatimg.py`), boot test, 86Box helpers |
| `docs/` | Roadmap and protocol |

Programs linking mTCP (CLAUDE.EXE, anything using `lib/xtbridge`) are GPLv3.
