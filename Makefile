# ClawDOS: shortcuts for the scripts in scripts/ and tunnel/. `make` lists them.
# The scripts do the real work (build.py decides what to rebuild), so most
# targets just run one. Works with the GNU Make 3.81 macOS ships.
#
#   make build TUNNEL=172.16.0.100:8443   set the DOS PC's tunnel address (remembered)
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
.PHONY: help setup build compiledb clean test check boot boottests boottests-online test8088 \
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

compiledb: ## write build/compile_commands.json for clangd
	$(PYTHON) scripts/build.py --compiledb

clean: ## delete build/ except settings.json (the remembered tunnel address)
	@[ -d build ] && find build -mindepth 1 -maxdepth 1 ! -name settings.json \
	    -exec rm -rf {} + || true

# --- tests --------------------------------------------------------------------

test: ## host unit tests: the mock API and the benchmark's table (fast, no emulator)
	$(PYTHON) tunnel/test_mockapi.py
	$(PYTHON) scripts/test_bench.py

check: test boottests ## everything offline: the mock API tests and the offline DOSBox-X boot tests

boot: build ## one boot test in DOSBox-X (TEST=image/tests/x.bat, default units)
	$(BOOTTEST) $(or $(TEST),image/tests/units.bat --expect UNITS-OK)

boottests: build ## the DOSBox-X boot tests that need no internet
	$(BOOTTEST) image/tests/units.bat --expect UNITS-OK
	$(BOOTTEST) image/tests/net.bat --expect "received from 10.0.2.2"
	$(BOOTTEST) image/tests/chat.bat --mock --expect "stop=end_turn" --reject "trying again" --reject "link lost" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/tools.bat --mock --expect "tool tour: PASS" --expect "progress note: Tour step 1 of" \
	    --expect "progress note: Tour done" --expect "tool tour threads: PASS" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/notes.bat --mock --expect "progress note: Two lines" --expect "stop=end_turn" \
	    --reject "VALIDATION:"
	$(BOOTTEST) image/tests/threads.bat --mock --timeout 240 --expect "thread replay after thread_not_found: PASS" \
	    --expect "thread resend: PASS" --expect "thread replay after thread_already_continued: PASS" \
	    --expect "thread replay after thread_unsupported_request: PASS" --expect "thread unsupported: PASS" \
	    --expect "Thread not found (simulated by !threadfail)" --expect "threadlost run: the result came by thread create" \
	    --reject ": FAIL" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/drop.bat --mock --expect "connection #2" --reject "VALIDATION:"
	$(BOOTTEST) image/tests/memory.bat --mock --expect "memory_write -> ok" --reject "VALIDATION:"
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

tunnel: ## run the TLS tunnel the DOS PC talks through (ARGS=--map PORT=HOST:443 ...)
	$(PYTHON) tunnel/tunnel.py $(ARGS)

mock: ## run the mock Messages API (plain HTTP on 18023) instead of the real one
	$(PYTHON) tunnel/mockapi.py $(ARGS)
