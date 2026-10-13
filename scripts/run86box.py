#!/usr/bin/env python3
"""Launch 86Box as an IBM PC/XT (8088 @ 4.77 MHz, 640K, CGA) booting build/CLAUDEXT.IMG.

  python scripts/run86box.py [--image PATH] [--speed 4772728]

The VM lives in build/86box/ (86Box rewrites its config on exit). The disk is
attached to an XTIDE card, because 86Box has no PicoMEM; XTIDE's BIOS presents
the same 16-head/63-sector geometry PicoMEM uses. The NIC is a Novell NE1000 (8-bit) at
I/O 340h, IRQ 3 (XTIDE owns 300h), with SLiRP user-mode networking: the guest
gets 10.0.2.x via DHCP and reaches the host at 10.0.2.2.
"""

import argparse
import glob
import os
import subprocess
import sys

import build

CFG = """\
[General]
vid_renderer = qt_software
confirm_exit = 0
confirm_reset = 0
confirm_save = 0

[Machine]
machine = ibmxt86
cpu_family = 8088
cpu_speed = {speed}
cpu_multi = 1
cpu_use_dynarec = 0
fpu_type = none
mem_size = 640
time_sync = local

[Video]
gfxcard = cga

[Input devices]
mouse_type = none

[Storage controllers]
hdc_1 = xtide

[Hard disks]
hdd_01_parameters = 63, 16, {cyl}, 0, ide
hdd_01_fn = {image}
hdd_01_ide_channel = 0:0

[Network]
net_01_card = ne1k
net_01_net_type = slirp
net_01_link = 0

[NE1000 Compatible #1]
base = 0340
irq = 3

[Ports (COM & LPT)]
serial1_enabled = 1
"""


def find_86box():
    hits = glob.glob(os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'WinGet',
                                  'Packages', '86Box.86Box_*', '86Box.exe'))
    if not hits:
        sys.exit('run86box: 86Box not found; run scripts/setup-tools.ps1')
    return hits[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--image', default=os.path.join(build.BUILD, build.IMAGE_NAME))
    ap.add_argument('--speed', type=int, default=4772728, help='CPU clock in Hz')
    a = ap.parse_args()

    vm = os.path.join(build.BUILD, '86box')
    os.makedirs(vm, exist_ok=True)
    with open(os.path.join(vm, '86box.cfg'), 'w') as f:
        f.write(CFG.format(speed=a.speed, cyl=build.CYLINDERS, image=os.path.abspath(a.image)))
    subprocess.Popen([find_86box(), '-P', vm])
    print(f'86Box started (VM: {vm})')


if __name__ == '__main__':
    main()
