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

**Safe mode** (on at startup, F3 toggles it, shown in the bottom row): tools
that change files, memory or ports ask `Allow ...? (y/n)` first; looking,
drawing and playing music never ask. `-unsafe` starts with it off, and `-q`
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
| `lib/` | Reusable DOS pieces: `screen` (snow-free CGA, hardware scrolling, pinned footer), `console` (word-wrapping text stream with margins), `xtbridge` (bridge protocol on mTCP), and the tool executors: `toolapi`, `dosfs`, `sysinfo`, `shell`, `basdraw` + `raster` + `gfx` (drawing), `play` + `sound` (music) |
| `tests/unit/` | CXTUNIT.EXE: unit tests for the hardware-free parsers, run in DOSBox-X |
| `bridge/` | XTBridge server and a test client (`chat.py`) |
| `src/boot/` | MBR and FreeDOS boot sector |
| `image/` | What goes on the disk image (`manifest.txt`, CONFIG/AUTOEXEC, tests) |
| `scripts/` | Build, image tool (`fatimg.py`), boot test, 86Box helpers |
| `docs/` | Roadmap and protocol |

Programs linking mTCP (CLAUDE.EXE, anything using `lib/xtbridge`) are GPLv3.
