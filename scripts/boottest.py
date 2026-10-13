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


def dosbox():
    p = shutil.which('dosbox-x') or r'C:\DOSBox-X\dosbox-x.exe'
    if not os.path.exists(p):
        sys.exit('boottest: dosbox-x not found')
    return p


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
    a = ap.parse_args()

    if not a.no_build:
        build.build_boot()
        build.build_apps()
    image = build.build_image(os.path.abspath(a.test))
    test_img = os.path.join(build.BUILD, 'test.img')
    shutil.copyfile(image, test_img)
    build.build_image()  # leave a clean CLAUDEXT.IMG behind

    cmd = [dosbox(), '-silent', '-nogui',
           '-set', 'cpu cputype=8086', '-set', 'cpu cycles=max', '-set', 'dosbox machine=cga',
           '-set', 'serial serial1=file file:serial.log',
           '-c', f'imgmount 2 test.img -t hdd -size 512,63,16,{build.CYLINDERS}',
           '-c', 'boot -l c']
    # DOSBox-X exits immediately when given an absolute image path from here, so
    # mount by relative path from build/.
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
    log = read_log(serial)
    sys.stdout.write(log.decode('cp437', 'replace').replace('\r\n', '\n'))
    if DONE not in log:
        print(f'boottest: FAILED (no {DONE.decode()} within {a.timeout}s)')
        sys.exit(1)
    print('boottest: PASS')


if __name__ == '__main__':
    main()
