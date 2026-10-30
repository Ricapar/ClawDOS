#!/usr/bin/env python3
"""Capture a screenshot of a program's main window (default: 86Box) to a PNG.

  python scripts/shot.py [--process 86Box] [--out build/shot.png] [--delay N]

macOS: screencapture on the window found through CoreGraphics. The terminal
needs Screen Recording permission (System Settings > Privacy & Security), or
the picture shows only the desktop. Windows: scripts/winshot.ps1.
"""

import argparse
import json
import os
import subprocess
import sys
import time

import build

DEFAULT_OUT = os.path.join(build.BUILD, 'shot.png')

# JXA: the id of the process's largest normal (layer 0) on-screen window.
FIND_WINDOW = """
ObjC.import('CoreGraphics');
function run(argv) {
    const info = ObjC.deepUnwrap(ObjC.castRefToObject(
        $.CGWindowListCopyWindowInfo($.kCGWindowListOptionOnScreenOnly, 0)));
    const wins = info.filter(w => w.kCGWindowOwnerName === argv[0] && w.kCGWindowLayer === 0);
    wins.sort((a, b) => b.kCGWindowBounds.Width * b.kCGWindowBounds.Height
                        - a.kCGWindowBounds.Width * a.kCGWindowBounds.Height);
    return JSON.stringify(wins.length ? wins[0].kCGWindowNumber : null);
}
"""


def window_id(process):
    """macOS window number of the process's main window, or None."""
    p = subprocess.run(['osascript', '-l', 'JavaScript', '-e', FIND_WINDOW, process],
                       capture_output=True, text=True, check=False)
    return json.loads(p.stdout) if p.returncode == 0 and p.stdout.strip() else None


def screenshot(process='86Box', out=DEFAULT_OUT, delay=0):
    """Save the window's picture to out; returns out, or None if it couldn't."""
    if delay:
        time.sleep(delay)
    if os.name == 'nt':
        p = subprocess.run(['powershell', '-NoProfile', '-File',
                            os.path.join(build.ROOT, 'scripts', 'winshot.ps1'), '-Process', process, '-Out', out],
                           capture_output=True, check=False)
        return out if p.returncode == 0 else None
    if sys.platform != 'darwin':
        return None
    wid = window_id(process)
    if wid is None:
        return None
    # -o: no window shadow, -x: no shutter sound
    p = subprocess.run(['screencapture', '-o', '-x', '-l', str(wid), out], capture_output=True, check=False)
    return out if p.returncode == 0 and os.path.exists(out) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--process', default='86Box')
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--delay', type=int, default=0, help='seconds to wait first')
    a = ap.parse_args()
    out = screenshot(a.process, a.out, a.delay)
    if not out:
        sys.exit(f'shot: no window for {a.process}')
    print(out)


if __name__ == '__main__':
    main()
