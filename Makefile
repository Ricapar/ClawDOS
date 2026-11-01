# ClawDOS: shortcuts for the scripts in scripts/ and tunnel/. `make` lists them.
# The scripts do the real work (build.py decides what to rebuild), so most
# targets just run one. Works with the GNU Make 3.81 macOS ships.
#
#   make build TUNNEL=172.16.0.100:8443   set the DOS PC's tunnel address (remembered)
#   make boot TEST=image/tests/net.bat     one boot test in DOSBox-X
#   make tunnel ARGS="--map 8444=openrouter.ai:443"   extra tunnel flags

ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif

BOOTTEST := $(PYTHON) scripts/boottest.py --no-build

.DEFAULT_GOAL := help
.PHONY: help setup build compiledb clean test check boot boottests test8088 \
        run dosbox shot tunnel mock

help:
	@echo "make <target>   (see the top of the Makefile for variables)"
	@grep -E '^[a-z][a-z0-9-]*:.*## ' $(MAKEFILE_LIST) | \
	    awk 'BEGIN {FS = ":.*## "} {printf "  %-10s %s\n", $$1, $$2}'

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

test: ## unit tests of the mock API (fast, no emulator)
	$(PYTHON) tunnel/test_mockapi.py

check: test boottests ## everything: the mock API tests and every DOSBox-X boot test

boot: build ## one boot test in DOSBox-X (TEST=image/tests/x.bat, default smoke)
	$(BOOTTEST) $(or $(TEST),image/tests/smoke.bat)

boottests: build ## every DOSBox-X boot test
	$(BOOTTEST) image/tests/smoke.bat
	$(BOOTTEST) image/tests/units.bat --expect UNITS-OK
	$(BOOTTEST) image/tests/net.bat
	$(BOOTTEST) image/tests/chat.bat --mock --expect "stop=end_turn"
	$(BOOTTEST) image/tests/tools.bat --mock --expect "tool tour: PASS"
	$(BOOTTEST) image/tests/drop.bat --mock --expect "connection #2"
	$(BOOTTEST) image/tests/memory.bat --mock --expect "memory_write -> ok"
	$(BOOTTEST) image/tests/clock.bat --mock --expect "Clock set from the API"
	$(BOOTTEST) image/tests/retry.bat --mock --expect "trying again" --expect "stop=end_turn"
	$(BOOTTEST) image/tests/chunks.bat --mock --expect "stop=end_turn"
	$(BOOTTEST) image/tests/bigevent.bat --mock --expect "event too long" --expect "stop=end_turn"
	$(BOOTTEST) image/tests/relaunch.bat --mock --timeout 150 --expect "tool tour: PASS" --expect "Largest executable program size" \
	    --expect "Clock set from the API" --expect "CLAWDOS.EXE exit code 0" --expect "resume state consumed"

test8088: ## a boot test on a real 8088 in 86Box (TEST=..., default devtools)
	$(PYTHON) scripts/test8088.py $(or $(TEST),image/tests/devtools.bat)

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
