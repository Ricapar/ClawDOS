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
   `ANTHROPIC_API_KEY` (or `ant auth login`), then `python bridge/xtbridge.py`.
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

## Layout

| Path | What |
|---|---|
| `src/claude/` | CLAUDE.EXE |
| `lib/` | Reusable DOS pieces: `screen` (snow-free CGA, hardware scrolling, pinned footer), `console` (word-wrapping text stream with margins), `xtbridge` (bridge protocol on mTCP) |
| `bridge/` | XTBridge server and a test client (`chat.py`) |
| `src/boot/` | MBR and FreeDOS boot sector |
| `image/` | What goes on the disk image (`manifest.txt`, CONFIG/AUTOEXEC, tests) |
| `scripts/` | Build, image tool (`fatimg.py`), boot test, 86Box helpers |
| `docs/` | Roadmap and protocol |

Programs linking mTCP (CLAUDE.EXE, anything using `lib/xtbridge`) are GPLv3.
