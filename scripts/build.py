#!/usr/bin/env python3
"""Build Claude-XT: boot code, DOS apps, and the bootable hard disk image.

  python scripts/build.py              build everything -> build/CLAUDEXT.IMG
  python scripts/build.py --autotest image/tests/smoke.bat
                                       also install C:\\AUTOTEST.BAT (run at boot
                                       instead of the normal startup)
"""

import argparse
import fnmatch
import os
import shutil
import subprocess
import sys
import zipfile

import fatimg

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
BUILD = os.path.join(ROOT, 'build')
WATCOM = os.path.join(TOOLS, 'openwatcom')

# PicoMEM presents hard disk images as 16 heads x 63 sectors. 65 cylinders keeps
# the partition just under 32 MB (type 04 FAT16), readable by every DOS >= 3.0.
CYLINDERS = 65
IMAGE_NAME = 'CLAUDEXT.IMG'

# DOS programs: output name -> list of sources (built with OpenWatcom, 8088 code)
APPS = {
    'hello.exe': ['src/hello/hello.c'],
}

TEXT_EXTS = {'.bat', '.sys', '.cfg', '.txt', '.ini'}


def find_tool(name, extra_dirs=()):
    for d in extra_dirs:
        p = os.path.join(d, name + ('.exe' if os.name == 'nt' else ''))
        if os.path.exists(p):
            return p
    p = shutil.which(name)
    if not p:
        sys.exit(f'build: cannot find {name}; run scripts/setup-tools.ps1')
    return p


def nasm():
    return find_tool('nasm', [os.path.join(os.environ.get('LOCALAPPDATA', ''), 'bin', 'NASM')])


WATCOM_BINS = [os.path.join(WATCOM, d) for d in
               (['binnt64', 'binnt'] if os.name == 'nt' else ['binl64', 'binl'])]


def watcom_env():
    env = dict(os.environ)
    env['WATCOM'] = WATCOM
    env['INCLUDE'] = os.path.join(WATCOM, 'h')
    env['PATH'] = os.pathsep.join(WATCOM_BINS + [env['PATH']])
    return env


def run(cmd, **kw):
    print('  ' + ' '.join(cmd))
    subprocess.run(cmd, check=True, **kw)


def build_boot():
    os.makedirs(BUILD, exist_ok=True)
    run([nasm(), '-f', 'bin', '-o', os.path.join(BUILD, 'mbr.bin'),
         os.path.join(ROOT, 'src', 'boot', 'mbr.asm')])
    fd = os.path.join(ROOT, 'src', 'boot', 'freedos')
    run([nasm(), '-dISFAT16', '--before', 'cpu 8086', '-i', fd + os.sep,
         '-o', os.path.join(BUILD, 'fat16.bin'), os.path.join(fd, 'boot.asm')])


def build_apps():
    env = watcom_env()
    wcl = find_tool('wcl', WATCOM_BINS)
    for out, srcs in APPS.items():
        run([wcl, '-q', '-0', '-ms', '-bt=dos', '-os', '-s', f'-fe={out}'] +
            [os.path.join(ROOT, s) for s in srcs], cwd=BUILD, env=env)


def resolve(source):
    """Return [(filename, bytes)] for a manifest source spec."""
    kind, _, spec = source.partition(':')
    if kind == 'fdpkg':
        zpath, _, member = spec.partition('!')
        with zipfile.ZipFile(os.path.join(TOOLS, 'freedos', 'packages', zpath)) as z:
            return [(os.path.basename(member), z.read(member))]
    base = {'repo': ROOT, 'build': BUILD, 'mtcp': os.path.join(TOOLS, 'mtcp'),
            'picomem': os.path.join(TOOLS, 'picomem')}.get(kind)
    if base is None:
        sys.exit(f'build: unknown source kind in {source!r}')
    d, pat = os.path.split(os.path.join(base, spec))
    names = sorted(n for n in os.listdir(d) if fnmatch.fnmatch(n.lower(), pat.lower()))
    if not names:
        sys.exit(f'build: nothing matches {source!r}')
    out = []
    for n in names:
        with open(os.path.join(d, n), 'rb') as f:
            data = f.read()
        if kind == 'repo' and os.path.splitext(n)[1].lower() in TEXT_EXTS:
            data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
        out.append((n, data))
    return out


def build_image(autotest=None):
    with open(os.path.join(BUILD, 'mbr.bin'), 'rb') as f:
        mbr = f.read()
    with open(os.path.join(BUILD, 'fat16.bin'), 'rb') as f:
        vbr = f.read()
    img = fatimg.make_hdd(CYLINDERS, mbr=mbr, vbr=vbr, label='CLAUDEXT')
    fs = fatimg.open_fs(img)

    entries = []
    with open(os.path.join(ROOT, 'image', 'manifest.txt')) as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if line:
                dest, source = line.split()
                entries.append((dest, source))
    if autotest:
        entries.append(('AUTOTEST.BAT', 'repo:' + os.path.relpath(autotest, ROOT)))

    for dest, source in entries:
        for name, data in resolve(source):
            path = dest + name.upper() if dest.endswith('/') else dest
            parent = path.rpartition('/')[0]
            if parent:
                fs.makedirs(parent)
            attr = fatimg.ATTR_ARCHIVE
            if path in ('KERNEL.SYS',):
                attr |= fatimg.ATTR_SYSTEM | fatimg.ATTR_HIDDEN | fatimg.ATTR_RO
            fs.write_file(path, data, attr=attr)

    out = os.path.join(BUILD, IMAGE_NAME)
    with open(out, 'wb') as f:
        f.write(img)
    print(f'{out}: {len(img) // 1024} KB, {fs.free_bytes() // 1024} KB free, '
          f'CHS {CYLINDERS}/16/63')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--autotest', help='batch file installed as C:\\AUTOTEST.BAT')
    a = ap.parse_args()
    print('[boot]')
    build_boot()
    print('[apps]')
    build_apps()
    print('[image]')
    build_image(a.autotest)


if __name__ == '__main__':
    main()
