# ClawDOS: shortcuts for the scripts in scripts/ and tunnel/. `make` lists them.
# The scripts do the real work (build.py decides what to rebuild), so most
# targets just run one. Works with the GNU Make 3.81 macOS ships.
#
#   make build TUNNEL=172.16.0.100:8443   set the DOS PC's tunnel address (remembered)
#   make update IMG=/path/CLAWDOS.IMG ARGS=--dry-run   bring an existing image up to date in place
#   make tunnel ARGS="--key-file ~/clawdos.key --allow 172.16.0.55"   the tunnel supplies the API key
#   make boot TEST=image/tests/net.bat     one boot test in DOSBox-X
#   make tunnel ARGS="--map 8444=openrouter.ai:443"   extra tunnel flags
#   make bench ARGS=--dosbox               the benchmark in DOSBox-X (a quick check)

ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif

BOOTTEST := $(PYTHON) scripts/boottest.py --no-build

.DEFAULT_GOAL := help
.PHONY: help setup build update compiledb clean test check boot boottests boottests-online test8088 \
        bench run dosbox shot tunnel mock

help:
	@echo "make <target>   (see the top of the Makefile for variables)"
	@grep -E '^[a-z][a-z0-9-]*:.*## ' $(MAKEFILE_LIST) | \
	    awk 'BEGIN {FS = ":.*## "} {printf "  %-16s %s\n", $$1, $$2}'

# --- setup and build ----------------------------------------------------------

setup: ## fetch the toolchains into tools/
	$(PYTHON) scripts/setup-tools.py

build: ## build the apps and build/CLAWDOS.IMG (TUNNEL=host:port sets the address)
	$(PYTHON) scripts/build.py $(if $(TUNNEL),--tunnel $(TUNNEL))

update: ## build, then update an existing image in place, keeping its memories, chats, log and config (IMG=path; ARGS=--dry-run --memories --cfg --no-backup --no-key)
	@[ -n "$(IMG)" ] || { echo "usage: make update IMG=path/to/CLAWDOS.IMG [ARGS=--dry-run]"; exit 2; }
	$(PYTHON) scripts/build.py --update "$(IMG)" $(if $(TUNNEL),--tunnel $(TUNNEL)) $(ARGS)

compiledb: ## write build/compile_commands.json for clangd
	$(PYTHON) scripts/build.py --compiledb

clean: ## delete build/ except settings.json (the remembered tunnel address)
	@[ -d build ] && find build -mindepth 1 -maxdepth 1 ! -name settings.json \
	    -exec rm -rf {} + || true

# --- tests --------------------------------------------------------------------

test: ## host unit tests: the mock API, the tunnel, the image updater and the benchmark's table (fast, no emulator)
	$(PYTHON) tunnel/test_mockapi.py
	$(PYTHON) tunnel/test_tunnel.py
	$(PYTHON) scripts/test_imgupdate.py
	$(PYTHON) scripts/test_bench.py

check: test boottests ## everything offline: the mock API tests and the offline DOSBox-X boot tests

boot: build ## one boot test in DOSBox-X (TEST=image/tests/x.bat, default units)
	$(BOOTTEST) $(or $(TEST),image/tests/units.bat --expect UNITS-OK)

boottests: build ## the DOSBox-X boot tests that need no internet
	$(BOOTTEST) image/tests/units.bat --expect UNITS-OK
	$(BOOTTEST) image/tests/net.bat --expect "received from 10.0.2.2"
	$(BOOTTEST) image/tests/chat.bat --mock --expect "stop=end_turn" --reject "trying again" --reject "link lost" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/tools.bat --mock --expect "tool tour: PASS" --expect "STDINEOF under COMMAND.COM: ok" --expect "progress note: Tour step 1 of" \
	    --expect "progress note: Tour done" --expect "tool tour threads: PASS" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/notes.bat --mock --expect "progress note: Two lines" --expect "stop=end_turn" \
	    --expect "tool header column: 0 (PASS)" --expect "headercol exit code 0" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/threads.bat --mock --timeout 300 --expect "thread replay after thread_not_found: PASS" \
	    --expect "thread resend: PASS" --expect "thread replay after thread_already_continued: PASS" \
	    --expect "thread replay after thread_unsupported_request: PASS" --expect "thread unsupported: PASS" \
	    --expect "Thread not found (simulated by !threadfail)" --expect "threadlost run: the result came by thread create" \
	    --expect "threadretry 529: the result came again by thread continue" \
	    --expect "threadretry overload: the result came again by thread continue" \
	    --expect "API error 404: Not found (simulated, on a continue)" \
	    --expect "API error 400: Bad request (simulated, on a continue)" \
	    --expect "threadlost exit code 0" --expect "threaddrop exit code 0" --expect "threaddrop burn exit code 0" \
	    --expect "nothreads exit code 0" --expect "threadfail exit code 1" --expect "threadretry 529 exit code 0" \
	    --expect "threadretry overload exit code 0" --expect "threadretry 404 exit code 1" \
	    --expect "threadretry 400 exit code 1" --expect "threadlost run exit code 0" --reject ": FAIL" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/chats.bat --mock --timeout 240 --expect "tail screen: PASS (4 of 4 rows; 4 messages before this one)" \
	    --expect "tail screen: PASS (2 of 2 rows; 2 messages before this one)" --expect "request: 5 messages, last role user" \
	    --expect "KB  !tool sysinfo" --expect "KB  Hello again, a second chat" --expect "chats: deleted FIRST" \
	    --expect "first exit code 0" --expect "second exit code 0" --expect "reopen exit code 0" --expect "list exit code 0" \
	    --expect "delete exit code 0" --reject "is still there" --reject "tail screen: FAIL" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/chatends.bat --mock \
	    --expect "reopened unans: 3 messages: user[text] assistant[text] user[text]; text: Are you still there? | !reopened unans" \
	    --expect "reopened call: 3 messages: user[text] assistant[text,tool_use] user[tool_result,text]; text: !reopened call; results: error Error: no result: ClawDOS stopped" \
	    --expect "reopened result: 3 messages: user[text] assistant[tool_use] user[tool_result,text]; text: !reopened result; results: error Interrupted by the user." \
	    --expect "reopened half: 1 messages: user[text]; text: Half a reply came. | !reopened half" \
	    --expect "unans exit code 0" --expect "call exit code 0" --expect "result exit code 0" --expect "half exit code 0" \
	    --expect "BIG.JSN: 206060 bytes (0 cut), 18 typed, ends 1" --expect "reopened big: 37 messages" --expect "big exit code 0" \
	    --expect "NOTCHAT.JSN is not a chat file" --expect "notchat exit code 1" --expect "and its second line stays" \
	    --expect "path exit code 1" --expect "AUTOEXEC.BAT still there" --expect "HALF.JSN: 46 bytes (66 cut)" \
	    --expect "NOSUCH.JSN (or it is empty)" --expect "nosuch exit code 1" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/drop.bat --mock --expect "connection #2" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/memory.bat --mock --expect "memory_write -> ok" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/skills.bat --mock --expect "skills: PASS" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/retry.bat --mock --expect "trying again" --expect "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/overload.bat --mock --expect "before any text); trying again" --expect "stop=end_turn" \
	    --reject "link lost" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/streamerr.bat --mock --expect "API error 529: Overloaded (simulated)" \
	    --expect "Bad request (simulated, before any text)" --reject "trying again" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/chunks.bat --mock --expect "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/bigevent.bat --mock --expect "event too long" --expect "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/bigsig.bat --mock --expect "big signature round trip: PASS" --expect "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/loop.bat --mock --timeout 240 --expect "loop: 25 tool calls, then tool_choice none" \
	    --expect "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/think.bat --mock --expect "I should answer with a thinking block" \
	    --expect "sends it back unchanged on the next request" --expect "Ping events between the text deltas." \
	    --expect "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/thinktool.bat --mock --expect "thinking round trip: PASS" --expect "stop=end_turn" \
	    --reject "VALIDATION:"
	$(BOOTTEST) image/tests/redacted.bat --mock --expect "redacted thinking round trip: PASS" \
	    --expect "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/busy.bat --mock --expect "attempt 2 of 3); trying again" --expect "attempt 3 of 3)" \
	    --reject "attempt 3 of 3); trying again" --reject "stop=end_turn" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/ends.bat --mock --expect "stop=refusal" --expect "The model declined" \
	    --expect "stop=max_tokens" --expect "reply cut off" --expect '"content":"!cutoff"' \
	    --reject '"content":"!refuse"' --reject "VALIDATION:"
	$(BOOTTEST) image/tests/relaunch.bat --mock --timeout 150 --expect "tool tour: PASS" --expect "Largest executable program size" \
	    --expect "CLAWDOS.EXE exit code 0" --expect "plain run stayed in the harness" --expect "resume state consumed" \
	    --expect "bigrun: the result came by thread continue" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/fixed.bat --mock --timeout 150 --expect "tool run -> ok" --expect "CLAWDOS.EXE exit code 0" \
	    --expect "1 messages, last role user, thinking none, cache 1h" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/runrefuse.bat --mock --timeout 120 --expect "stop=refusal" --expect "relaunch:" \
	    --reject '"content":"!runrefuse"' --reject toolu_mock --reject "VALIDATION:"

boottests-online: build ## the DOSBox-X boot tests that need the internet (NTP)
	$(BOOTTEST) image/tests/clock.bat --expect "System time set" --expect "PING after the clock: 0"

test8088: ## a boot test on a real 8088 in 86Box (TEST=..., default devtools)
	$(PYTHON) scripts/test8088.py $(or $(TEST),image/tests/devtools.bat)

bench: ## the benchmark in 86Box against the mock: a table per round (docs/ROADMAP.md)
	$(PYTHON) scripts/bench.py $(ARGS)

# --- run ----------------------------------------------------------------------

run: build ## boot the image in 86Box (an accurate 8088 PC)
	$(PYTHON) scripts/run86box.py

dosbox: build ## boot the image in DOSBox-X with networking (faster, less exact)
	$(PYTHON) scripts/rundosbox.py

shot: ## screenshot 86Box to build/shot.png
	$(PYTHON) scripts/shot.py

tunnel: ## run the TLS tunnel the DOS PC talks through (ARGS=--map PORT=HOST:443 --allow ADDR --key-file PATH ...)
	$(PYTHON) tunnel/tunnel.py $(ARGS)

mock: ## run the mock Messages API (plain HTTP on 18023, 127.0.0.1; ARGS=--listen 0.0.0.0 for a real PC) instead of the real one
	$(PYTHON) tunnel/mockapi.py $(ARGS)
