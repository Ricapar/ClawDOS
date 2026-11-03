#!/usr/bin/env python3
"""Build ClawDOS: boot code, DOS apps, and the bootable hard disk image.

  python scripts/build.py              build everything -> build/CLAWDOS.IMG
  python scripts/build.py --tunnel 192.168.1.50:8443
                                       set the TLS tunnel's address (remembered)
  python scripts/build.py --autotest image/tests/smoke.bat
                                       also install C:\\AUTOTEST.BAT (run at boot
                                       instead of the normal startup)
"""

import argparse
import fnmatch
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
import zipfile

import fatimg

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
BUILD = os.path.join(ROOT, 'build')
# The near data segment (statics, string literals of the C modules, the stack) is
# addressed with 16-bit offsets, so it must stay under 64 KB; fail before it gets
# close enough for the linker to be the one to say so.
DGROUP_BUDGET = 56 * 1024
EXTERNAL = os.path.join(ROOT, 'external')   # the user's own disk images (gitignored)
WATCOM = os.path.join(TOOLS, 'openwatcom')

# PicoMEM presents hard disk images as 16 heads x 63 sectors. 65 cylinders keeps
# the partition just under 32 MB (type 04 FAT16), readable by every DOS >= 3.0.
CYLINDERS = 65
IMAGE_NAME = 'CLAWDOS.IMG'

MTCP_SRC = os.path.join(TOOLS, 'mtcp-src', 'mTCP-src_2026-10-10')
MTCP_LIB = ['packet.cpp', 'arp.cpp', 'eth.cpp', 'ip.cpp', 'tcp.cpp', 'tcpsockm.cpp', 'udp.cpp',
            'utils.cpp', 'dns.cpp', 'timer.cpp', 'trace.cpp', 'ipasm.asm']

# DOS programs, built with OpenWatcom for the 8088.
#   sources: files in this repo (.c, .cpp, .asm)
#   model:   memory model letter (s, c, m, l)
#   mtcp:    mTCP compile-time config (.cfg) if the program uses TCP/IP
APPS = {
    'hello.exe': {'sources': ['src/hello/hello.c'], 'model': 's'},
    # The launcher: starts CLAWDOS and runs the model's programs while CLAWDOS is out of memory (lib/relaunch.h).
    'clawdos.exe': {'sources': ['src/clawdos/clawdos.c', 'lib/shell.c', 'lib/toolapi.c', 'lib/utf8.c', 'lib/ticks.c',
                             'lib/relaunch.c'], 'model': 's'},
    'harness.exe': {'sources': ['src/harness/harness.cpp', 'src/harness/chat.cpp', 'src/harness/tools.cpp', 'lib/screen.c',
                               'lib/console.c', 'lib/toolapi.c', 'lib/dosfs.c', 'lib/edit.c',
                               'lib/search.c', 'lib/sysinfo.c', 'lib/play.c', 'lib/sound.c', 'lib/shell.c',
                               'lib/memory.c', 'lib/ems.c', 'lib/ticks.c', 'lib/relaunch.c', 'lib/mtcpcfg.c',
                               'lib/utf8.c', 'lib/sink.c', 'lib/json.c', 'lib/chunked.c', 'lib/sse.c',
                               'lib/httpp.c', 'lib/cfg.c', 'lib/history.c', 'lib/apireq.c', 'lib/apistream.c', 'lib/convo.c',
                               'lib/net.cpp', 'lib/http.cpp'],
                   'model': 'l', 'mtcp': 'src/harness/harness.cfg'},
    'dosunit.exe': {'sources': ['tests/unit/dosunit.c', 'lib/play.c', 'lib/memory.c', 'lib/toolapi.c', 'lib/console.c',
                                'lib/edit.c', 'lib/search.c', 'lib/ticks.c', 'lib/utf8.c', 'lib/sink.c', 'lib/json.c', 'lib/chunked.c', 'lib/sse.c', 'lib/httpp.c', 'lib/cfg.c', 'lib/history.c', 'lib/apireq.c', 'lib/apistream.c', 'lib/convo.c', 'lib/dosfs.c', 'lib/mtcpcfg.c'],
                    'model': 'l'},
}

TEXT_EXTS = {'.bat', '.sys', '.cfg', '.txt', '.ini', '.bas', '.asm', '.c', '.h'}


def find_tool(name, extra_dirs=()):
    """Path of the tool `name`: in extra_dirs, else on PATH (exits if missing)."""
    for d in extra_dirs:
        p = os.path.join(d, name + ('.exe' if os.name == 'nt' else ''))
        if os.path.exists(p):
            return p
    p = shutil.which(name)
    if not p:
        sys.exit(f'build: cannot find {name}; run scripts/setup-tools.py')
    return p


def nasm():
    """Path of the NASM assembler."""
    return find_tool('nasm', [os.path.join(os.environ.get('LOCALAPPDATA', ''), 'bin', 'NASM')])


def watcom_bin_dirs():
    """The snapshot's host binary folders for this machine, best first."""
    if os.name == 'nt':
        return ['binnt64', 'binnt']
    if sys.platform == 'darwin':
        return ['armo64', 'bino64'] if platform.machine() == 'arm64' else ['bino64']
    return ['arml64'] if platform.machine() in ('aarch64', 'arm64') else ['binl64', 'binl']


WATCOM_BINS = [os.path.join(WATCOM, d) for d in watcom_bin_dirs()]


def watcom_env():
    """The environment OpenWatcom's tools need."""
    env = dict(os.environ)
    env['WATCOM'] = WATCOM
    env['INCLUDE'] = os.path.join(WATCOM, 'h')
    env['PATH'] = os.pathsep.join(WATCOM_BINS + [env['PATH']])
    return env


def run(cmd, show_output=True, **kw):
    """Run a tool. Output is shown if asked for, and always on failure."""
    print(f'  {os.path.basename(cmd[0])} {os.path.basename(cmd[-1])}')
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False, **kw)
    out = '\n'.join(l for l in p.stdout.splitlines()
                    if not l.startswith(('Open Watcom', 'Version 2.0', 'Copyright', 'Portions',
                                         'Source code', 'See https')))
    if out.strip() and (show_output or p.returncode):
        print(out)
    if p.returncode:
        sys.exit(f'build: {os.path.basename(cmd[0])} failed')


def build_boot():
    """Assemble the MBR and the FAT16 boot sector."""
    os.makedirs(BUILD, exist_ok=True)
    run([nasm(), '-f', 'bin', '-o', os.path.join(BUILD, 'mbr.bin'),
         os.path.join(ROOT, 'src', 'boot', 'mbr.asm')])
    fd = os.path.join(ROOT, 'src', 'boot', 'freedos')
    run([nasm(), '-dISFAT16', '--before', 'cpu 8086', '-i', fd + os.sep,
         '-o', os.path.join(BUILD, 'fat16.bin'), os.path.join(fd, 'boot.asm')])


# yyyymmdd.hhmm, local time, when this build ran.
BUILD_STAMP = time.strftime('%Y%m%d.%H%M')


def compile_flags(spec, objdir):
    """(compiler flags, extra sources) for an app: the mTCP library and config if it uses TCP/IP."""
    model = spec.get('model', 's')
    flags = ['-q', '-0', f'-m{model}', '-bt=dos', '-os', '-s', '-zp2', '-i=' + os.path.join(ROOT, 'lib')]
    if model in 'clh':
        flags.append('-zc')     # string literals in the code segment, out of DGROUP (data pointers are far)
    extra = []
    # Watcom can't take a quoted -D value from a command line, so quoted
    # definitions go in a force-included header: the build stamp for every
    # program (ClawDOS shows it, so you can tell which build a disk has), and
    # CFG_H for mTCP, whose sources do `#include CFG_H`.
    build_h = os.path.join(objdir, 'build_h.h')
    with open(build_h, 'w', encoding='utf-8') as f:
        f.write(f'#define BUILD_STAMP "{BUILD_STAMP}"\n')
        if spec.get('mtcp'):
            f.write(f'#define CFG_H "{os.path.basename(spec["mtcp"])}"\n')
    flags.append(f'-fi={build_h}')
    if spec.get('mtcp'):
        cfg = os.path.join(ROOT, spec['mtcp'])
        flags += ['-i=' + os.path.dirname(cfg),
                  '-i=' + os.path.join(MTCP_SRC, 'TCPINC'), '-i=' + os.path.join(MTCP_SRC, 'INCLUDE')]
        extra = [os.path.join(MTCP_SRC, 'TCPLIB', f.upper()) for f in MTCP_LIB]
    return flags, extra


def compile_sources(sources, spec, flags, objdir, env):
    """Compile/assemble each source into objdir; returns the object files."""
    model = spec.get('model', 's')
    objs = []
    for src in sources:
        obj = os.path.join(objdir, os.path.splitext(os.path.basename(src))[0].lower() + '.obj')
        objs.append(obj)
        ext = os.path.splitext(src)[1].lower()
        ours = src.startswith(ROOT) and not src.startswith(TOOLS)
        if ext == '.asm':
            run([find_tool('wasm', WATCOM_BINS), '-q', '-0', f'-m{model}', f'-fo={obj}', src],
                show_output=ours, env=env)
        else:
            compiler = find_tool('wpp' if ext == '.cpp' else 'wcc', WATCOM_BINS)
            # All warnings for our code, except where mTCP's headers come along.
            strict = ours and not (spec.get('mtcp') and ext == '.cpp')
            run([compiler] + flags + (['-wx'] if strict else []) + [f'-fo={obj}', src],
                show_output=ours, env=env)
    return objs


def map_sizes(text):
    """(memory size, DGROUP size) in bytes from a wlink map; None where the map lacks it."""
    mem = re.search(r'^Memory size:\s+([0-9A-Fa-f]+)', text, re.M)
    dgroup = re.search(r'^DGROUP\s+[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)', text, re.M)
    return (int(mem.group(1), 16) if mem else None, int(dgroup.group(1), 16) if dgroup else None)


def report_sizes(name, mapfile):
    """Print the program's footprint from its link map; fail past the DGROUP budget."""
    with open(mapfile, encoding='utf-8', errors='replace') as f:
        mem, dgroup = map_sizes(f.read())
    line = f'     memory {mem:,} bytes' if mem else '     (no Memory size in the map)'
    if dgroup:
        line += f', DGROUP {dgroup:,} of {DGROUP_BUDGET:,}'
    print(line)
    if dgroup and dgroup > DGROUP_BUDGET:
        sys.exit(f'build: {name}: DGROUP is {dgroup:,} bytes, over the {DGROUP_BUDGET:,} budget')


def link_app(name, objs, objdir, env):
    """Link the object files into build/<name> with a wlink command file."""
    lnk = os.path.join(objdir, 'link.lnk')
    with open(lnk, 'w', encoding='utf-8') as f:
        mapfile = os.path.join(BUILD, os.path.splitext(name)[0] + '.map')
        f.write(f'system dos\noption quiet, map={mapfile}, eliminate, stack=4096\n')
        f.write(f'name {os.path.join(BUILD, name)}\n')
        f.writelines(f'file {o}\n' for o in objs)
    run([find_tool('wlink', WATCOM_BINS), f'@{lnk}'], env=env)
    report_sizes(name, mapfile)


def build_app(name, spec, env):
    """Compile and link one DOS program."""
    objdir = os.path.join(BUILD, 'obj', os.path.splitext(name)[0])
    os.makedirs(objdir, exist_ok=True)
    flags, extra = compile_flags(spec, objdir)
    sources = [os.path.join(ROOT, s) for s in spec['sources']] + extra
    link_app(name, compile_sources(sources, spec, flags, objdir, env), objdir, env)


def build_apps(only=None):
    """Build the DOS programs in APPS (just those named in `only`, if given)."""
    env = watcom_env()
    for name, spec in APPS.items():
        if only and name not in only:
            continue
        print(f'  -> {name}')
        build_app(name, spec, env)


CLANGD_DIR = os.path.join(ROOT, 'scripts', 'clangd')
# clang only has to parse the sources (clangd reads them for code navigation and
# diagnostics): a 32-bit Windows target with Microsoft extensions comes closest to
# Watcom's dialect, and scripts/clangd/watcom.h papers over the rest.
CLANG_FLAGS = ['--target=i686-pc-windows-msvc', '-fms-extensions', '-fno-ms-compatibility', '-nostdinc',
               '-isystem', os.path.join(CLANGD_DIR, 'include'), '-isystem', os.path.join(WATCOM, 'h'),
               '-include', os.path.join(CLANGD_DIR, 'watcom.h'),
               '-Wno-nonportable-include-path']    # DOS file names have no case: #include "TYPES.H" is fine


def write_compiledb():
    """build/compile_commands.json: how clangd should read each DOS source, derived from
    APPS so it sees the same include paths and config headers as the Watcom build."""
    entries, seen = [], set()
    for name, spec in APPS.items():
        objdir = os.path.join(BUILD, 'obj', os.path.splitext(name)[0])
        os.makedirs(objdir, exist_ok=True)
        flags, extra = compile_flags(spec, objdir)
        clang = list(CLANG_FLAGS)
        for f in flags:
            if f.startswith('-i='):
                clang += ['-I', f[3:]]
            elif f.startswith('-fi='):
                clang += ['-include', f[4:]]
        for src in [os.path.join(ROOT, s) for s in spec['sources']] + extra:
            if src in seen or os.path.splitext(src)[1].lower() not in ('.c', '.cpp'):
                continue
            seen.add(src)
            entries.append({'directory': ROOT, 'file': src, 'arguments': ['clang'] + clang + ['-c', src]})
    out = os.path.join(BUILD, 'compile_commands.json')
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(entries, f, indent=1)
    return out


_missing_external = set()


def zip_members(path, pattern, source):
    """[(name, bytes)] of the members of zip `path` matching `pattern`."""
    with zipfile.ZipFile(path) as z:
        if pattern.endswith('/**'):                    # a whole tree, keeping its folders
            top = pattern[:-2].lower()
            names = [n for n in z.namelist() if n.lower().startswith(top) and not n.endswith('/')]
            if not names:
                sys.exit(f'build: nothing matches {source!r}')
            return [(n[len(top):], z.read(n)) for n in sorted(names)]
        folder, pat = os.path.split(pattern.lower())   # one folder level: LIB/* is not LIB/I386/*
        names = [n for n in z.namelist() if os.path.dirname(n.lower()) == folder
                 and not n.endswith('/') and fnmatch.fnmatch(os.path.basename(n.lower()), pat)]
        if not names:
            sys.exit(f'build: nothing matches {source!r}')
        return [(os.path.basename(n), z.read(n)) for n in sorted(names)]


def external_files(spec, source):
    """external:IMAGE!PATH, both wildcards: files from the user's own floppy
    images in external/. Optional: without a matching image, nothing."""
    image_pat, _, member_pat = spec.partition('!')
    images = sorted(f for f in os.listdir(EXTERNAL) if fnmatch.fnmatch(f.lower(), image_pat.lower())) \
        if os.path.isdir(EXTERNAL) else []
    if not images:
        if image_pat not in _missing_external:
            _missing_external.add(image_pat)
            print(f'build: note: no external/{image_pat}; skipping what it would provide')
        return []
    out = []
    for image in images:
        with open(os.path.join(EXTERNAL, image), 'rb') as f:
            fs = fatimg.open_fs(bytearray(f.read()))
        for path, entry in fs.walk():
            path_l, folder, pat = path.lstrip('/').lower(), *os.path.split(member_pat.lower())
            if (not entry.is_dir and os.path.dirname(path_l) == folder
                    and fnmatch.fnmatch(os.path.basename(path_l), pat)):
                out.append((path.rsplit('/', 1)[-1], fs.read_file(path.lstrip('/'))))
    if not out:
        sys.exit(f'build: nothing matches {source!r}')
    return out


def tree_files(top):
    """[(path relative to top with '/', bytes)] of every file under the folder `top`."""
    out = []
    for dirpath, _, files in os.walk(top):
        for n in sorted(files):
            rel = os.path.relpath(os.path.join(dirpath, n), top).replace(os.sep, '/')
            with open(os.path.join(dirpath, n), 'rb') as f:
                out.append((rel, f.read()))
    return out


def resolve(source):
    """Return [(filename, bytes)] for a manifest source spec."""
    kind, _, spec = source.partition(':')
    if kind == 'fdpkg':
        zpath, _, member = spec.partition('!')
        with zipfile.ZipFile(os.path.join(TOOLS, 'freedos', 'packages', zpath)) as z:
            return [(os.path.basename(member), z.read(member))]
    if kind == 'external':
        return external_files(spec, source)
    if kind == 'devel' and '!' in spec:
        zpath, _, member = spec.partition('!')
        return zip_members(os.path.join(TOOLS, 'devel', zpath), member, source)
    base = {'repo': ROOT, 'build': BUILD, 'mtcp': os.path.join(TOOLS, 'mtcp'),
            'picomem': os.path.join(TOOLS, 'picomem'), 'devel': os.path.join(TOOLS, 'devel')}.get(kind)
    if base is None:
        sys.exit(f'build: unknown source kind in {source!r}')
    if spec.endswith('/**'):                           # a whole tree, keeping its folders
        out = tree_files(os.path.join(base, spec[:-3]))
        if not out:
            sys.exit(f'build: nothing matches {source!r}')
        return out
    d, pat = os.path.split(os.path.join(base, spec))
    names = sorted(n for n in os.listdir(d)
                   if fnmatch.fnmatch(n.lower(), pat.lower()) and os.path.isfile(os.path.join(d, n)))
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


def manifest_entries(autotest=None):
    """[(destination, source spec)] from image/manifest.txt, plus AUTOTEST.BAT if given."""
    entries = []
    with open(os.path.join(ROOT, 'image', 'manifest.txt'), encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if line:
                dest, source = line.split(None, 1)   # sources may contain spaces
                entries.append((dest, source.strip()))
    if autotest:
        entries.append(('AUTOTEST.BAT', 'repo:' + os.path.relpath(autotest, ROOT)))
    return entries


def fill_image(fs, entries):
    """Write the files named by the manifest entries into the file system."""
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


def build_image(autotest=None):
    """Build the bootable disk image from the manifest (plus C:\\AUTOTEST.BAT if given); returns its path."""
    with open(os.path.join(BUILD, 'mbr.bin'), 'rb') as f:
        mbr = f.read()
    with open(os.path.join(BUILD, 'fat16.bin'), 'rb') as f:
        vbr = f.read()
    img = fatimg.make_hdd(CYLINDERS, mbr=mbr, vbr=vbr, label='CLAWDOS')
    fs = fatimg.open_fs(img)
    fill_image(fs, manifest_entries(autotest))

    # Point HARNESS.EXE at the TLS tunnel and give it the API key.
    fs.write_file('CLAWDOS/CLAWDOS.CFG', clawdos_cfg(fs.read_file('CLAWDOS/CLAWDOS.CFG')))

    out = os.path.join(BUILD, IMAGE_NAME)
    with open(out, 'wb') as f:
        f.write(img)
    print(f'{out}: {len(img) // 1024} KB, {fs.free_bytes() // 1024} KB free, '
          f'CHS {CYLINDERS}/16/63')
    return out


def api_key():
    """ANTHROPIC_API_KEY from the repo's .env (gitignored), or None."""
    path = os.path.join(ROOT, '.env')
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as f:
        for line in f:
            key, sep, value = line.strip().partition('=')
            if sep and key.strip() == 'ANTHROPIC_API_KEY':
                return value.strip().strip('"\'') or None
    return None


def clawdos_cfg(template):
    """The template CLAWDOS.CFG with the [anthropic] tunnel address from the settings
    and the API key from .env filled in (a build without a key still boots)."""
    text = template.decode('cp437')
    text = re.sub(r'^tunnel .*$', f'tunnel {settings()["tunnel"]}', text, count=1, flags=re.M)
    # The DOS PC sets its clock from the API's Date header (GMT): give it this machine's offset.
    hours = round(time.localtime().tm_gmtoff / 3600)
    text = re.sub(r'^timezone .*$', f'timezone {hours}', text, count=1, flags=re.M)
    key = api_key()
    if key:
        text = re.sub(r'^# key sk-ant-\.\.\..*$', f'key {key}', text, count=1, flags=re.M)
    return text.encode('cp437')


SETTINGS_FILE = os.path.join(BUILD, 'settings.json')
DEFAULT_SETTINGS = {
    # Where the DOS PC finds the TLS tunnel. 10.0.2.2 is the host PC as seen
    # from the emulators' NAT network; real hardware needs the tunnel machine's LAN IP.
    'tunnel': '10.0.2.2:8443',
}


def settings(**changes):
    """Persistent build settings (build/settings.json), updated by command-line flags."""
    s = dict(DEFAULT_SETTINGS)
    if os.path.exists(SETTINGS_FILE):
        with open(SETTINGS_FILE, encoding='utf-8') as f:
            s.update(json.load(f))
    changes = {k: v for k, v in changes.items() if v is not None}
    if changes:
        s.update(changes)
        os.makedirs(BUILD, exist_ok=True)
        with open(SETTINGS_FILE, 'w', encoding='utf-8') as f:
            json.dump(s, f, indent=2)
    return s


def main():
    """Command line: set the tunnel address, then build the boot code, the programs and the image."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--autotest', help='batch file installed as C:\\AUTOTEST.BAT')
    ap.add_argument('--tunnel', help='host:port of the TLS tunnel (remembered for later builds)')
    ap.add_argument('--compiledb', action='store_true',
                    help='only write build/compile_commands.json for clangd (a full build writes it too)')
    a = ap.parse_args()
    if a.compiledb:
        print(write_compiledb())
        return
    s = settings(tunnel=a.tunnel)
    print(f'[settings] tunnel = {s["tunnel"]}, key {"from .env" if api_key() else "not set"}, build v{BUILD_STAMP}')
    print('[boot]')
    build_boot()
    print('[apps]')
    build_apps()
    write_compiledb()
    print('[image]')
    build_image(a.autotest)


if __name__ == '__main__':
    main()
