#!/usr/bin/env python3
"""Launch DOSBox-X as a fast stand-in XT (CGA; 80186 CPU, see boottest.CPUTYPE) booting
build/CLAUDEXT.IMG, with networking.

  python scripts/rundosbox.py [--image PATH] [--test image/tests/x.bat] [--cycles max]

The NIC is DOSBox-X's NE2000 at I/O 340h, IRQ 3, with SLiRP user-mode
networking: the guest gets 10.0.2.x via DHCP and reaches the host at 10.0.2.2.
--test boots a copy of the image (build/run.img) that runs the batch file as
C:\\AUTOTEST.BAT instead of the normal startup. For accurate timing use run86box.py.
"""

import argparse
import os
import shutil
import subprocess

import boottest
import build


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--image', default=os.path.join(build.BUILD, build.IMAGE_NAME))
    ap.add_argument('--test', help='batch file to run instead of the normal startup')
    ap.add_argument('--cycles', default='max', help='DOSBox-X cpu cycles (default max)')
    a = ap.parse_args()

    image = os.path.abspath(a.image)
    if a.test:
        built = build.build_image(os.path.abspath(a.test))
        image = os.path.join(build.BUILD, 'run.img')
        shutil.copyfile(built, image)
        build.build_image()  # leave a clean CLAUDEXT.IMG behind
    # DOSBox-X exits immediately when given an absolute image path, so mount
    # by a path relative to its working directory.
    cwd = os.path.dirname(image)
    cmd = [boottest.dosbox(),
           '-set', f'cpu cputype={boottest.CPUTYPE}', '-set', f'cpu cycles={a.cycles}', '-set', 'dosbox machine=cga',
           '-set', 'ne2000 ne2000=true', '-set', 'ne2000 nicbase=340', '-set', 'ne2000 nicirq=3',
           '-set', 'ne2000 backend=slirp', '-set', 'serial serial2=disabled',  # COM2 would share IRQ 3
           '-c', f'imgmount 2 {os.path.basename(image)} -t hdd -size 512,63,16,{build.CYLINDERS}',
           '-c', 'boot -l c']
    subprocess.Popen(cmd, cwd=cwd, stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f'DOSBox-X started ({image})')


if __name__ == '__main__':
    main()
