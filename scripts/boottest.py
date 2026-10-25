#!/usr/bin/env python3
"""Boot the Claude-XT image headless in DOSBox-X with a test batch file and print its log.

  python scripts/boottest.py [image/tests/smoke.bat] [--timeout 90]

The test batch file runs as C:\\AUTOTEST.BAT instead of the normal startup. It
should write its results to COM1 (captured to build/serial.log) and finish with
ECHO CLAUDEXT-TEST-DONE > COM1; this script waits for that marker, then stops
DOSBox-X. Files written inside the guest are not a reliable result channel: DOS
may not have flushed them to the image yet. Exit status is 0 only if the marker was seen.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

import build

DONE = b'CLAUDEXT-TEST-DONE'

# Not 8086: on an 8088/8086 the Crynwr NE2000 packet driver moves packet data with
# 8-bit IN/OUT loops (it only uses REP INSW/OUTSW on a 186+) while keeping the card in
# 16-bit word mode. DOSBox-X's NE2000 is a 16-bit card that consumes a whole word per
# data-port access, so every received frame is read half-length and garbled: the
# driver drops it and mTCP DHCP times out. 80186 keeps real-mode-only, 20-bit memory
# aliasing behaviour; 86Box (run86box.py) remains the accurate 8088 check.
CPUTYPE = '80186'

# The mock bridge gets its own port so a real bridge on 8023 can keep running
# (and isn't reached, with its API key, by test traffic). Tests use 10.0.2.2:18023.
TEST_BRIDGE_PORT = 18023


def dosbox():
    # Prefer the portable MinGW build from setup-tools.ps1: it has SLiRP networking.
    for p in (os.path.join(build.ROOT, 'tools', 'dosbox-x', 'mingw-build', 'mingw', 'dosbox-x.exe'),
              shutil.which('dosbox-x'), r'C:\DOSBox-X\dosbox-x.exe'):
        if p and os.path.exists(p):
            return p
    sys.exit('dosbox-x not found; run scripts/setup-tools.ps1')


def read_log(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return b''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('test', nargs='?', default=os.path.join(build.ROOT, 'image', 'tests', 'smoke.bat'))
    ap.add_argument('--timeout', type=int, default=90)
    ap.add_argument('--no-build', action='store_true', help='reuse build/ outputs')
    ap.add_argument('--set', action='append', default=[], metavar='"SECTION KEY=VALUE"',
                    help='extra DOSBox-X -set options, e.g. "log misc=true"')
    ap.add_argument('--expect', action='append', default=[], metavar='TEXT',
                    help='TEXT must appear in the serial or bridge log (repeatable)')
    ap.add_argument('--bridge', action='store_true',
                    help=f'run the mock bridge (xtbridge.py --mock) on port {TEST_BRIDGE_PORT} during the test')
    a = ap.parse_args()

    if not a.no_build:
        build.build_boot()
        build.build_apps()
    image = build.build_image(os.path.abspath(a.test))
    test_img = os.path.join(build.BUILD, 'test.img')
    shutil.copyfile(image, test_img)
    build.build_image()  # leave a clean CLAUDEXT.IMG behind

    cmd = [dosbox(), '-silent', '-nogui',
           '-set', f'cpu cputype={CPUTYPE}', '-set', 'cpu cycles=max', '-set', 'dosbox machine=cga',
           '-set', 'serial serial1=file file:serial.log',
           '-set', 'ne2000 ne2000=true', '-set', 'ne2000 nicbase=340', '-set', 'ne2000 nicirq=3',
           '-set', 'ne2000 backend=slirp', '-set', 'serial serial2=disabled',  # COM2 would share IRQ 3
           '-set', 'log logfile=dbx.log',
           *[x for s in a.set for x in ('-set', s)],
           '-c', f'imgmount 2 test.img -t hdd -size 512,63,16,{build.CYLINDERS}',
           '-c', 'boot -l c']
    # DOSBox-X exits immediately when given an absolute image path from here, so
    # mount by relative path from build/.
    bridge = None
    bridge_log = os.path.join(build.BUILD, 'bridge.log')
    if a.bridge:
        with open(bridge_log, 'wb') as f:
            bridge = subprocess.Popen([sys.executable, os.path.join(build.ROOT, 'bridge', 'xtbridge.py'),
                                       '--mock', '--speed', '0', '--port', str(TEST_BRIDGE_PORT),
                                       '--log', '-', '-v'],
                                      stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    proc = subprocess.Popen(cmd, cwd=build.BUILD, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    serial = os.path.join(build.BUILD, 'serial.log')
    if os.path.exists(serial):
        os.remove(serial)
    deadline = time.time() + a.timeout
    log = b''
    try:
        while time.time() < deadline and proc.poll() is None:
            time.sleep(1)
            log = read_log(serial)
            if DONE in log:
                break
    finally:
        proc.kill()
        proc.wait()
        if bridge:
            bridge.kill()
            bridge.wait()
    log = read_log(serial)
    sys.stdout.write(log.decode('cp437', 'replace').replace('\r\n', '\n'))
    if bridge:
        print('--- bridge log ---')
        sys.stdout.write(read_log(bridge_log).decode('utf-8', 'replace'))
        log += read_log(bridge_log)
    if DONE not in log:
        print(f'boottest: FAILED (no {DONE.decode()} within {a.timeout}s)')
        sys.exit(1)
    missing = [t for t in a.expect if t.encode('cp437', 'replace') not in log]
    if missing:
        print('boottest: FAILED (missing: ' + ', '.join(repr(t) for t in missing) + ')')
        sys.exit(1)
    print('boottest: PASS')


if __name__ == '__main__':
    main()
