#!/usr/bin/env python3
"""Download and extract the project toolchain into ./tools (gitignored).

  python3 scripts/setup-tools.py

Idempotent: skips anything already present. System-wide tools come from the
platform's package manager: Homebrew on macOS (nasm, dosbox-x, 86Box, pyright),
winget on Windows (NASM, 86Box; DOSBox-X is a portable download there).
"""

import glob
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.error
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
DL = os.path.join(TOOLS, 'downloads')

OW_BUILD = '2026-10-01-Build'
MTCP_VER = '2026-10-10'
FD_VER = '1.4'
ROMS_VER = 'v6.0'
DBX_VER = '2026.10.01'
GWB_VER = '20261005'

WINDOWS = os.name == 'nt'
MACOS = sys.platform == 'darwin'

PICOMEM_DRIVERS = ['ASTCLOCK.COM', 'NE2000.COM', 'PICOMEM.EXE', 'PM2000.COM', 'PMDFS.EXE',
                   'PMDFS3.EXE', 'PMEMM.EXE', 'PMINIT.EXE', 'PMMOUSE.EXE', 'README.md']
# FreeDOS 1.4 devel packages come from ibiblio or, when it is down, a mirror.
FD_MIRRORS = [f'https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/{FD_VER}/devel',
              'https://sunsite.icm.edu.pl/packages/freedos/repositories/latest/devel']
DEVEL_PACKAGES = ['tinyasm', 'wasm', 'ldebug', 'bcc']


def t(*parts):
    """Path under tools/."""
    return os.path.join(TOOLS, *parts)


def fetch(url, dest):
    """Download url to dest (via a temporary file, so a failure leaves nothing behind)."""
    print(f'Downloading {os.path.basename(dest)}')
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + '.part'
    req = urllib.request.Request(url, headers={'User-Agent': 'ClawDOS-setup'})
    with urllib.request.urlopen(req) as r, open(tmp, 'wb') as f:
        shutil.copyfileobj(r, f)
    os.replace(tmp, dest)


def get_file(url, name):
    """Path of tools/downloads/name, downloading it first if needed."""
    dest = os.path.join(DL, name)
    if not os.path.exists(dest):
        fetch(url, dest)
    return dest


def unzip(zpath, dest):
    with zipfile.ZipFile(zpath) as z:
        z.extractall(dest)


def system_packages():
    """NASM, DOSBox-X, 86Box (and pyright for the pyright-lsp plugin) from the package manager."""
    if MACOS:
        if not shutil.which('brew'):
            sys.exit('setup-tools: Homebrew is needed (https://brew.sh)')
        for formula in ('nasm', 'dosbox-x', 'pyright'):
            if subprocess.run(['brew', 'list', '--formula', formula], capture_output=True, check=False).returncode:
                subprocess.run(['brew', 'install', formula], check=True)
        if subprocess.run(['brew', 'list', '--cask', '86box'], capture_output=True, check=False).returncode:
            subprocess.run(['brew', 'install', '--cask', '86box'], check=True)
    elif WINDOWS:
        for pkg in ('NASM.NASM', '86Box.86Box'):
            if subprocess.run(['winget', 'list', '--id', pkg, '-e', '--accept-source-agreements'],
                              capture_output=True, check=False).returncode:
                subprocess.run(['winget', 'install', '--id', pkg, '-e', '--silent',
                                '--accept-source-agreements', '--accept-package-agreements'], check=True)
        # DOSBox-X, MinGW portable build: unlike the winget (Visual Studio) build it
        # has the SLiRP network backend, so the emulated NE2000 works without Npcap.
        if not os.path.exists(t('dosbox-x', 'mingw-build', 'mingw', 'dosbox-x.exe')):
            unzip(get_file(f'https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v{DBX_VER}/'
                           f'dosbox-x-mingw64-{DBX_VER}-portable.zip', f'dosbox-x-mingw64-{DBX_VER}.zip'),
                  t('dosbox-x'))
    else:
        print('setup-tools: install nasm, dosbox-x (with SLiRP) and 86Box with your package manager')


def openwatcom():
    """OpenWatcom 2 portable snapshot (has binnt64, binl64, armo64/bino64 for macOS, ...)."""
    if glob.glob(t('openwatcom', '*', 'wcc*')):
        return
    f = get_file(f'https://github.com/open-watcom/open-watcom-v2/releases/download/{OW_BUILD}/ow-snapshot.tar.xz',
                 'ow-snapshot.tar.xz')
    print('Extracting OpenWatcom')
    with tarfile.open(f) as tf:
        tf.extractall(t('openwatcom'), filter='data')


def mtcp():
    if not os.path.exists(t('mtcp', 'htget.exe')):
        unzip(get_file(f'https://www.brutman.com/mTCP/download/mTCP_{MTCP_VER}.zip', f'mTCP_{MTCP_VER}.zip'),
              t('mtcp'))
    if not os.path.exists(t('mtcp-src')):
        unzip(get_file(f'https://www.brutman.com/mTCP/download/mTCP-src_{MTCP_VER}.zip',
                       f'mTCP-src_{MTCP_VER}.zip'), t('mtcp-src'))


def freedos():
    if not os.path.exists(t('freedos', 'floppy')):
        unzip(get_file(f'https://download.freedos.org/{FD_VER}/FD14-FloppyEdition.zip', 'FD14-FloppyEdition.zip'),
              t('freedos', 'floppy'))
    if not os.path.exists(t('freedos', 'liteusb')):
        unzip(get_file(f'https://download.freedos.org/{FD_VER}/FD14-LiteUSB.zip', 'FD14-LiteUSB.zip'),
              t('freedos', 'liteusb'))
    # FreeDOS packages (zips), pulled out of the LiteUSB image
    if not os.path.exists(t('freedos', 'packages', 'BASE', 'KERNEL.ZIP')):
        subprocess.run([sys.executable, os.path.join(ROOT, 'scripts', 'fatimg.py'), 'get',
                        t('freedos', 'liteusb', 'FD14LITE.img'), 'PACKAGES', t('freedos', 'packages')], check=True)
    # Crynwr packet drivers (generic NE2000 for emulators), from the FreeDOS net repo
    crynwr = t('freedos', 'packages', 'NET', 'CRYNWR.ZIP')
    if not os.path.exists(crynwr):
        os.makedirs(os.path.dirname(crynwr), exist_ok=True)
        shutil.copyfile(get_file('https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/'
                                 f'{FD_VER}/net/crynwr.zip', 'crynwr.zip'), crynwr)


def picomem():
    for d in PICOMEM_DRIVERS:
        dest = t('picomem', d)
        if not os.path.exists(dest):
            fetch(f'https://raw.githubusercontent.com/FreddyVRetro/ISA-PicoMEM/main/drivers/{d}', dest)


def roms_dir():
    """Where 86Box looks for its ROM set; None if 86Box isn't installed."""
    if MACOS:
        return os.path.expanduser('~/Library/Application Support/net.86box.86Box/roms')
    if WINDOWS:
        box = glob.glob(os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'WinGet', 'Packages',
                                     '86Box.86Box_*'))
        return os.path.join(box[0], 'roms') if box else None
    return os.path.expanduser('~/.local/share/86Box/roms')


def roms_86box():
    roms = roms_dir()
    if not roms or os.path.exists(os.path.join(roms, 'machines')):
        return
    f = get_file(f'https://github.com/86Box/roms/archive/refs/tags/{ROMS_VER}.zip', f'86box-roms-{ROMS_VER}.zip')
    tmp = os.path.join(DL, 'roms-tmp')
    shutil.rmtree(tmp, ignore_errors=True)
    unzip(f, tmp)
    top = next(e.path for e in os.scandir(tmp) if e.is_dir())
    if os.path.isdir(roms):     # an empty roms folder left by 86Box
        shutil.rmtree(roms)
    os.makedirs(os.path.dirname(roms), exist_ok=True)
    shutil.move(top, roms)
    shutil.rmtree(tmp)


def devel():
    """Development tools for the DOS PC image (they run on the 8088; see
    docs/research/8088 development tools.md)."""
    for p in DEVEL_PACKAGES:
        dest = t('devel', f'{p}.zip')
        if os.path.exists(dest):
            continue
        for m in FD_MIRRORS:
            try:
                fetch(f'{m}/{p}.zip', dest)
                break
            except urllib.error.URLError as e:
                print(f'  {m}: {e}')
        else:
            sys.exit(f'setup-tools: could not download FreeDOS package {p}')
    # GW-BASIC assembled from Microsoft's MIT-licensed source (TK Chia)
    gwb = t('devel', 'gwbasic.zip')
    if not os.path.exists(gwb):
        shutil.copyfile(get_file(f'https://codeberg.org/tkchia/GW-BASIC/releases/download/{GWB_VER}/GWB26A05.ZIP',
                                 'GWB26A05.ZIP'), gwb)


def main():
    os.makedirs(DL, exist_ok=True)
    system_packages()
    openwatcom()
    mtcp()
    freedos()
    picomem()
    roms_86box()
    devel()
    print('Toolchain ready.')


if __name__ == '__main__':
    main()
