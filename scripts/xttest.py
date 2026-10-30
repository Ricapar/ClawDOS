#!/usr/bin/env python3
"""Run a test batch file on the accurate XT (86Box: 8088 at 4.77 MHz, CGA) and print its log.

  python scripts/xttest.py image/tests/devtools.bat [--log DEVTEST.LOG] [--done DEVTEST-DONE]
                           [--timeout 900] [--no-build]

The batch file runs as C:\\AUTOTEST.BAT (no networking) and should append its
results to C:\\<log>, ending with the --done marker. DOSBox-X boot tests run as
an 80186 and can't catch 186/286 code; this is the check that a program really
runs on an 8088. The log is read out of the disk image while 86Box runs, so
once the marker is in it the result is safe however 86Box is closed. At the end
this takes a screenshot (build/shot.png), closes 86Box and prints the log.
Exit status 0 only if the marker was found in the log.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

import fatimg
import run86box
import build

IMAGE = os.path.join(build.BUILD, 'xttest.img')


def read_log(name):
    """The test's log as text, read from the disk image (None if not there yet)."""
    with open(IMAGE, 'rb') as f:
        fs = fatimg.open_fs(bytearray(f.read()))
    try:
        data = fs.read_file(name.upper())
    except fatimg.FatError:
        return None
    return data.decode('cp437', 'replace').replace('\r\n', '\n')


def close_86box(proc):
    """Ask 86Box to quit, killing it if it won't."""
    subprocess.run(['taskkill', '/PID', str(proc.pid)], check=False, capture_output=True)
    try:
        proc.wait(20)
    except subprocess.TimeoutExpired:
        proc.kill()


def main():
    """Build the image, run the test batch file in 86Box and report whether its marker appeared."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('test')
    ap.add_argument('--log', default='DEVTEST.LOG', help='log file in C:\\ written by the test')
    ap.add_argument('--done', default='DEVTEST-DONE', help='marker the test writes last')
    ap.add_argument('--timeout', type=int, default=900, help='seconds to wait for the marker')
    ap.add_argument('--no-build', action='store_true', help='reuse build/ outputs')
    a = ap.parse_args()

    if not a.no_build:
        build.build_boot()
        build.build_apps()
    shutil.copyfile(build.build_image(os.path.abspath(a.test)), IMAGE)
    build.build_image()  # leave a clean CLAWDOS.IMG behind

    running = subprocess.run(['powershell', '-NoProfile', '-Command',
                              'Get-Process 86Box -ErrorAction SilentlyContinue'],
                             capture_output=True, text=True, check=False).stdout.strip()
    if running:
        sys.exit('xttest: 86Box is already running; close it first')
    vm = os.path.join(build.BUILD, '86box')
    os.makedirs(vm, exist_ok=True)
    with open(os.path.join(vm, '86box.cfg'), 'w', encoding='utf-8') as f:
        f.write(run86box.CFG.format(speed=4772728, cyl=build.CYLINDERS, image=IMAGE))
    # Not a context manager: 86Box is closed by close_86box() once the log has the marker.
    proc = subprocess.Popen([run86box.find_86box(), '-P', vm])  # pylint: disable=consider-using-with

    started = time.time()
    log = None
    while time.time() - started < a.timeout:
        time.sleep(10)
        log = read_log(a.log)
        if log and a.done in log:
            break
    elapsed = int(time.time() - started)
    subprocess.run(['powershell', '-NoProfile', '-File', os.path.join(build.ROOT, 'scripts', 'winshot.ps1')],
                   check=False, capture_output=True)
    close_86box(proc)
    log = read_log(a.log) or log
    print(log if log is not None else f'(no {a.log} on the image)')
    found = bool(log) and a.done in log
    print(f'xttest: {"PASS" if found else "FAILED (no " + a.done + " in the log)"} after ~{elapsed}s; '
          f'screenshot in build/shot.png')
    sys.exit(0 if found else 1)


if __name__ == '__main__':
    main()
