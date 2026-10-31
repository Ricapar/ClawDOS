# ClawDOS: shortcuts for the scripts in scripts/ and bridge/. `make` lists them.
# The scripts do the real work (build.py decides what to rebuild), so most
# targets just run one. Works with the GNU Make 3.81 macOS ships.
#
#   make build BRIDGE=172.16.0.100:8023   set the XT's bridge address (remembered)
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
.PHONY: help setup venv build compiledb clean test check boot boottests xttest \
        run dosbox shot bridge mock chat

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

test: $(VENV_PY) ## bridge unit tests (fast, no emulator)
	$(VENV_PY) bridge/test_tools.py
	$(VENV_PY) bridge/test_bridge.py

check: test boottests ## everything: bridge tests and every DOSBox-X boot test

boot: build ## one boot test in DOSBox-X (TEST=image/tests/x.bat, default smoke)
	$(BOOTTEST) $(or $(TEST),image/tests/smoke.bat)

boottests: build ## every DOSBox-X boot test
	$(BOOTTEST) image/tests/smoke.bat
	$(BOOTTEST) image/tests/units.bat --expect UNITS-OK
	$(BOOTTEST) image/tests/net.bat
	$(BOOTTEST) image/tests/tools.bat --bridge --expect "tool tour: PASS"
	$(BOOTTEST) image/tests/drop.bat --bridge --expect "connection #2"
	$(BOOTTEST) image/tests/memory.bat --bridge --expect "memory_write -> ok"
	$(BOOTTEST) image/tests/clock.bat --bridge --expect "Clock set from the bridge"

xttest: ## a boot test on a real 8088 in 86Box (TEST=..., default devtools)
	$(PYTHON) scripts/xttest.py $(or $(TEST),image/tests/devtools.bat)

# --- run ----------------------------------------------------------------------

run: build ## boot the image in 86Box (an accurate XT)
	$(PYTHON) scripts/run86box.py

dosbox: build ## boot the image in DOSBox-X with networking (faster, less exact)
	$(PYTHON) scripts/rundosbox.py

shot: ## screenshot 86Box to build/shot.png
	$(PYTHON) scripts/shot.py

bridge: $(VENV_PY) ## run the bridge to Claude (ARGS=... for more flags)
	$(VENV_PY) bridge/xtbridge.py $(ARGS)

mock: $(VENV_PY) ## run the bridge with the offline mock instead of Claude
	$(VENV_PY) bridge/xtbridge.py --mock $(ARGS)

chat: $(VENV_PY) ## chat with a running bridge from this terminal
	$(VENV_PY) bridge/chat.py $(ARGS)
