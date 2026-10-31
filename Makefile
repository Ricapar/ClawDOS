# ClawDOS: shortcuts for the scripts in scripts/ and bridge/. `make` lists them.
# The scripts do the real work (build.py decides what to rebuild), so most
# targets just run one. Works with the GNU Make 3.81 macOS ships.
#
#   make build BRIDGE=172.16.0.100:8023   set the DOS PC's bridge address (remembered)
#   make boot TEST=image/tests/net.bat     one boot test in DOSBox-X
#   make bridge ARGS="--prompt dos"        extra bridge flags

ifeq ($(OS),Windows_NT)
PYTHON ?= python
VENV_PY := .venv/Scripts/python.exe
else
PYTHON ?= python3
VENV_PY := .venv/bin/python
endif

BOOTTEST := $(PYTHON) scripts/boottest.py --no-build

.DEFAULT_GOAL := help
.PHONY: help setup venv build compiledb clean test check boot boottests test8088 \
        run dosbox shot tunnel mock bridge chat

help:
	@echo "make <target>   (see the top of the Makefile for variables)"
	@grep -E '^[a-z][a-z0-9-]*:.*## ' $(MAKEFILE_LIST) | \
	    awk 'BEGIN {FS = ":.*## "} {printf "  %-10s %s\n", $$1, $$2}'

# --- setup and build ----------------------------------------------------------

setup: ## fetch the toolchains into tools/ and make the bridge's .venv
	$(PYTHON) scripts/setup-tools.py
	$(MAKE) venv

venv: $(VENV_PY) ## (re)install the bridge's Python packages into .venv

$(VENV_PY): bridge/requirements.txt
	$(PYTHON) -m venv .venv
	$(VENV_PY) -m pip install -q -r bridge/requirements.txt
	@touch $@

build: ## build the apps and build/CLAWDOS.IMG (BRIDGE=host:port sets the address)
	$(PYTHON) scripts/build.py $(if $(BRIDGE),--bridge $(BRIDGE))

compiledb: ## write build/compile_commands.json for clangd
	$(PYTHON) scripts/build.py --compiledb

clean: ## delete build/ except settings.json (the remembered bridge address)
	@[ -d build ] && find build -mindepth 1 -maxdepth 1 ! -name settings.json \
	    -exec rm -rf {} + || true

# --- tests --------------------------------------------------------------------

test: $(VENV_PY) ## unit tests of the Python (fast, no emulator)
	$(PYTHON) tunnel/test_mockapi.py
	$(VENV_PY) bridge/test_tools.py
	$(VENV_PY) bridge/test_bridge.py

check: test boottests ## everything: bridge tests and every DOSBox-X boot test

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

bridge: $(VENV_PY) ## run the old DOS Bridge (goes away with the native client)
	$(VENV_PY) bridge/dosbridge.py $(ARGS)

chat: $(VENV_PY) ## chat with a running bridge from this terminal
	$(VENV_PY) bridge/chat.py $(ARGS)
