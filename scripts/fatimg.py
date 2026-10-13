#!/usr/bin/env python3
"""FAT12/FAT16 disk image tool for Claude-XT. Pure Python, no dependencies.

Works on raw floppy images and MBR-partitioned hard disk images.

  fatimg.py ls    IMG [PATH]            list a directory
  fatimg.py tree  IMG                   list everything
  fatimg.py get   IMG SRC DEST          extract a file (or directory, recursively)
  fatimg.py put   IMG SRC DEST          copy a host file/dir into the image
  fatimg.py mkdir IMG PATH
  fatimg.py rm    IMG PATH
  fatimg.py mkhdd IMG --cyl N [--mbr BIN] [--vbr BIN] [--label NAME]
  fatimg.py mkfloppy IMG --kb 360|720|1200|1440 [--vbr BIN] [--label NAME]

Use -p N to pick a partition (default: first FAT partition, or the whole image
if it has no partition table).
"""

import argparse
import datetime
import os
import struct
import sys

SECTOR = 512

ATTR_RO, ATTR_HIDDEN, ATTR_SYSTEM, ATTR_VOLUME, ATTR_DIR, ATTR_ARCHIVE = 1, 2, 4, 8, 16, 32
ATTR_LFN = 0x0F

FLOPPY_PRESETS = {
    # kb: (sectors, spt, heads, spc, root_ents, media, spf)
    360: (720, 9, 2, 2, 112, 0xFD, 2),
    720: (1440, 9, 2, 2, 112, 0xF9, 3),
    1200: (2400, 15, 2, 1, 224, 0xF9, 7),
    1440: (2880, 18, 2, 1, 224, 0xF0, 9),
}


class FatError(Exception):
    pass


def to_83(name):
    """'kernel.sys' -> b'KERNEL  SYS'."""
    if name in ('.', '..'):
        return name.encode().ljust(11)
    base, _, ext = name.upper().rpartition('.') if '.' in name else (name.upper(), '', '')
    if not base or len(base) > 8 or len(ext) > 3:
        raise FatError(f'not a valid 8.3 name: {name!r}')
    for ch in base + ext:
        if ch in ' "*+,/:;<=>?[\\]|' or ord(ch) < 0x20:
            raise FatError(f'invalid character in name: {name!r}')
    return base.encode('ascii').ljust(8) + ext.encode('ascii').ljust(3)


def from_83(raw):
    base = raw[:8].decode('cp437').rstrip()
    ext = raw[8:11].decode('cp437').rstrip()
    if base.startswith('\x05'):
        base = '\xe5' + base[1:]
    return f'{base}.{ext}' if ext else base


def dos_datetime(ts):
    t = datetime.datetime.fromtimestamp(ts)
    if t.year < 1980:
        t = datetime.datetime(1980, 1, 1)
    date = ((t.year - 1980) << 9) | (t.month << 5) | t.day
    time = (t.hour << 11) | (t.minute << 5) | (t.second // 2)
    return date, time


def lba_to_chs(lba, heads, spt):
    c = lba // (heads * spt)
    h = (lba // spt) % heads
    s = lba % spt + 1
    if c > 1023:
        c, h, s = 1023, heads - 1, spt
    return bytes([h, s | ((c >> 2) & 0xC0), c & 0xFF])


class DirEntry:
    __slots__ = ('name', 'attr', 'cluster', 'size', 'date', 'time', 'loc')

    def __init__(self, raw, loc):
        self.name = from_83(raw[0:11])
        self.attr = raw[11]
        self.time, self.date, self.cluster, self.size = struct.unpack_from('<HHHI', raw, 22)
        self.loc = loc  # byte offset of this entry in the image

    @property
    def is_dir(self):
        return bool(self.attr & ATTR_DIR)


class FatFS:
    def __init__(self, img, start_lba=0):
        self.img = img
        self.base = start_lba * SECTOR
        b = img[self.base:self.base + SECTOR]
        (self.bps, self.spc, self.reserved, self.nfats, self.root_ents, tot16,
         self.media, self.spf, self.spt, self.heads, self.hidden, tot32) = \
            struct.unpack_from('<HBHBHHBHHHII', b, 11)
        if self.bps != SECTOR or self.spc == 0 or self.nfats == 0:
            raise FatError('no FAT12/16 filesystem found')
        self.total = tot16 or tot32
        self.root_secs = (self.root_ents * 32 + SECTOR - 1) // SECTOR
        self.fat_off = self.base + self.reserved * SECTOR
        self.root_off = self.fat_off + self.nfats * self.spf * SECTOR
        self.data_off = self.root_off + self.root_secs * SECTOR
        data_secs = self.total - self.reserved - self.nfats * self.spf - self.root_secs
        self.nclusters = data_secs // self.spc
        if self.nclusters < 4085:
            self.fat_bits = 12
        elif self.nclusters < 65525:
            self.fat_bits = 16
        else:
            raise FatError('FAT32 is not supported')
        self.eoc = 0xFF8 if self.fat_bits == 12 else 0xFFF8
        self.csize = self.spc * SECTOR
        self.hint = 2

    # --- FAT table -------------------------------------------------------
    def fat_get(self, c):
        if self.fat_bits == 16:
            return struct.unpack_from('<H', self.img, self.fat_off + c * 2)[0]
        v = struct.unpack_from('<H', self.img, self.fat_off + c + c // 2)[0]
        return v >> 4 if c & 1 else v & 0xFFF

    def fat_set(self, c, val):
        for n in range(self.nfats):
            off = self.fat_off + n * self.spf * SECTOR
            if self.fat_bits == 16:
                struct.pack_into('<H', self.img, off + c * 2, val)
            else:
                o = off + c + c // 2
                v = struct.unpack_from('<H', self.img, o)[0]
                v = (v & 0x000F) | (val << 4) if c & 1 else (v & 0xF000) | (val & 0xFFF)
                struct.pack_into('<H', self.img, o, v)

    def chain(self, c):
        out = []
        while 2 <= c < self.eoc - 1:
            if len(out) > self.nclusters:
                raise FatError('FAT chain loop')
            out.append(c)
            c = self.fat_get(c)
        return out

    def alloc(self, n):
        found = []
        c = self.hint
        for _ in range(self.nclusters):
            if c >= self.nclusters + 2:
                c = 2
            if self.fat_get(c) == 0:
                found.append(c)
                if len(found) == n:
                    break
            c += 1
        if len(found) < n:
            raise FatError('disk full')
        for a, b in zip(found, found[1:]):
            self.fat_set(a, b)
        self.fat_set(found[-1], 0xFFFF if self.fat_bits == 16 else 0xFFF)
        self.hint = found[-1] + 1
        return found

    def free_chain(self, c):
        for x in self.chain(c):
            self.fat_set(x, 0)

    def coff(self, c):
        return self.data_off + (c - 2) * self.csize

    def free_bytes(self):
        return sum(1 for c in range(2, self.nclusters + 2) if self.fat_get(c) == 0) * self.csize

    # --- directories -----------------------------------------------------
    def dir_slots(self, cluster):
        """Byte offsets of every 32-byte slot in a directory (0 = root)."""
        if cluster == 0:
            return [self.root_off + i * 32 for i in range(self.root_ents)]
        return [self.coff(c) + i * 32 for c in self.chain(cluster) for i in range(self.csize // 32)]

    def listdir(self, cluster):
        out = []
        for off in self.dir_slots(cluster):
            first = self.img[off]
            if first == 0:
                break
            attr = self.img[off + 11]
            if first == 0xE5 or attr == ATTR_LFN or attr & ATTR_VOLUME:
                continue
            out.append(DirEntry(self.img[off:off + 32], off))
        return out

    def lookup(self, path):
        """Return (DirEntry or None for root, dir cluster)."""
        parts = [p for p in path.replace('\\', '/').split('/') if p]
        cluster, ent = 0, None
        for i, p in enumerate(parts):
            want = from_83(to_83(p))
            for e in self.listdir(cluster):
                if e.name == want:
                    ent = e
                    break
            else:
                raise FatError(f'not found: {path}')
            if i < len(parts) - 1:
                if not ent.is_dir:
                    raise FatError(f'not a directory: {p}')
                cluster = ent.cluster
        return ent, (ent.cluster if ent else 0)

    def resolve_dir(self, path):
        ent, cl = self.lookup(path)
        if ent is not None and not ent.is_dir:
            raise FatError(f'not a directory: {path}')
        return cl

    def free_slot(self, cluster):
        for off in self.dir_slots(cluster):
            if self.img[off] in (0, 0xE5):
                return off
        if cluster == 0:
            raise FatError('root directory full')
        last = self.chain(cluster)[-1]
        new = self.alloc(1)[0]
        self.fat_set(last, new)
        self.img[self.coff(new):self.coff(new) + self.csize] = bytes(self.csize)
        return self.coff(new)

    def write_entry(self, off, name83, attr, cluster, size, ts):
        d, t = dos_datetime(ts)
        self.img[off:off + 32] = struct.pack('<11sBBBHHHHHHHI', name83, attr, 0, 0, t, d, d,
                                             0, t, d, cluster, size)

    def split(self, path):
        path = path.replace('\\', '/').strip('/')
        parent, _, name = path.rpartition('/')
        return parent, name

    def find_in(self, cluster, name):
        want = from_83(to_83(name))
        for e in self.listdir(cluster):
            if e.name == want:
                return e
        return None

    # --- files -----------------------------------------------------------
    def read_file(self, path):
        ent, _ = self.lookup(path)
        if ent is None or ent.is_dir:
            raise FatError(f'not a file: {path}')
        data = b''.join(bytes(self.img[self.coff(c):self.coff(c) + self.csize])
                        for c in self.chain(ent.cluster))
        return data[:ent.size]

    def write_file(self, path, data, ts=None, attr=ATTR_ARCHIVE):
        parent, name = self.split(path)
        pcl = self.resolve_dir(parent)
        old = self.find_in(pcl, name)
        if old is not None:
            if old.is_dir:
                raise FatError(f'is a directory: {path}')
            if old.cluster:
                self.free_chain(old.cluster)
            off = old.loc
        else:
            off = self.free_slot(pcl)
        first = 0
        if data:
            clusters = self.alloc((len(data) + self.csize - 1) // self.csize)
            first = clusters[0]
            for i, c in enumerate(clusters):
                chunk = data[i * self.csize:(i + 1) * self.csize]
                self.img[self.coff(c):self.coff(c) + self.csize] = chunk.ljust(self.csize, b'\0')
        self.write_entry(off, to_83(name), attr, first, len(data), ts or 315532800)

    def mkdir(self, path, ts=None, exist_ok=True):
        parent, name = self.split(path)
        pcl = self.resolve_dir(parent)
        old = self.find_in(pcl, name)
        if old is not None:
            if old.is_dir and exist_ok:
                return old.cluster
            raise FatError(f'exists: {path}')
        ts = ts or 315532800
        off = self.free_slot(pcl)
        c = self.alloc(1)[0]
        self.img[self.coff(c):self.coff(c) + self.csize] = bytes(self.csize)
        self.write_entry(self.coff(c), to_83('.'), ATTR_DIR, c, 0, ts)
        self.write_entry(self.coff(c) + 32, to_83('..'), ATTR_DIR, pcl, 0, ts)
        self.write_entry(off, to_83(name), ATTR_DIR, c, 0, ts)
        return c

    def makedirs(self, path, ts=None):
        cur = ''
        for p in [p for p in path.replace('\\', '/').split('/') if p]:
            cur = f'{cur}/{p}'
            self.mkdir(cur, ts)

    def remove(self, path):
        ent, _ = self.lookup(path)
        if ent is None:
            raise FatError('cannot remove root')
        if ent.is_dir:
            for e in self.listdir(ent.cluster):
                if e.name not in ('.', '..'):
                    self.remove(f'{path}/{e.name}')
        if ent.cluster:
            self.free_chain(ent.cluster)
        self.img[ent.loc] = 0xE5

    def walk(self, cluster=0, prefix=''):
        for e in self.listdir(cluster):
            if e.name in ('.', '..'):
                continue
            yield f'{prefix}/{e.name}', e
            if e.is_dir:
                yield from self.walk(e.cluster, f'{prefix}/{e.name}')


# --- formatting --------------------------------------------------------------

def write_bpb(sector, *, spc, reserved, nfats, root_ents, total, media, spf, spt, heads,
              hidden, drive, label, fstype):
    struct.pack_into('<8sHBHBHHBHHHII', sector, 3, b'CLAUDEXT', SECTOR, spc, reserved, nfats,
                     root_ents, total if total < 65536 else 0, media, spf, spt, heads, hidden,
                     total if total >= 65536 else 0)
    volid = int(datetime.datetime.now().timestamp()) & 0xFFFFFFFF
    struct.pack_into('<BBBI11s8s', sector, 0x24, drive, 0, 0x29, volid,
                     label.upper().encode('ascii')[:11].ljust(11), fstype.ljust(8).encode())
    sector[510:512] = b'\x55\xAA'


def default_boot_sector():
    """Non-bootable stub: prints a message and waits for a key, then reboots."""
    code = bytes([0xEB, 0x3C, 0x90]) + bytes(0x3B)
    msg = b'Not a boot disk. Press a key.\r\n\0'
    # at 0x3E: xor ax,ax ; mov ds,ax ; mov si,msg ; cld
    #    loop: lodsb ; or al,al ; jz done ; mov ah,0E ; int 10 ; jmp loop
    #    done: xor ax,ax ; int 16 ; int 19
    stub = bytes([0x31, 0xC0, 0x8E, 0xD8, 0xBE]) + struct.pack('<H', 0x7C00 + 0x3E + 25) + bytes([
        0xFC, 0xAC, 0x08, 0xC0, 0x74, 0x06, 0xB4, 0x0E, 0xCD, 0x10, 0xEB, 0xF5,
        0x31, 0xC0, 0xCD, 0x16, 0xCD, 0x19])
    sec = bytearray(code + stub + msg)
    return sec.ljust(SECTOR, b'\0')


def format_fat(img, start_lba, total, *, fat_bits, spc, root_ents, media, spt, heads, drive,
               label, vbr=None, spf=None):
    reserved, nfats = 1, 2
    root_secs = root_ents * 32 // SECTOR
    if spf is None:
        spf = 1
        while True:
            clusters = (total - reserved - nfats * spf - root_secs) // spc
            need = ((clusters + 2) * fat_bits + 7) // 8
            if (need + SECTOR - 1) // SECTOR <= spf:
                break
            spf += 1
    sec = bytearray(vbr if vbr else default_boot_sector())
    if len(sec) != SECTOR:
        raise FatError('boot sector must be 512 bytes')
    write_bpb(sec, spc=spc, reserved=reserved, nfats=nfats, root_ents=root_ents, total=total,
              media=media, spf=spf, spt=spt, heads=heads, hidden=start_lba, drive=drive,
              label=label, fstype=f'FAT{fat_bits}')
    base = start_lba * SECTOR
    meta_end = base + (reserved + nfats * spf + root_secs) * SECTOR
    img[base:meta_end] = bytes(meta_end - base)
    img[base:base + SECTOR] = sec
    fs = FatFS(img, start_lba)
    if fs.fat_bits != fat_bits:
        raise FatError(f'geometry yields FAT{fs.fat_bits}, expected FAT{fat_bits}')
    fs.fat_set(0, (0xFF00 | media) if fat_bits == 16 else (0xF00 | media))
    fs.fat_set(1, 0xFFFF if fat_bits == 16 else 0xFFF)
    if label:
        fs.write_entry(fs.root_off, label.upper().encode('ascii')[:11].ljust(11), ATTR_VOLUME,
                       0, 0, datetime.datetime.now().timestamp())
    return fs


def fat16_spc(total):
    for limit, spc in ((32680, 2), (262144, 4), (524288, 8), (1048576, 16),
                       (2097152, 32), (4194304, 64)):
        if total <= limit:
            return spc
    raise FatError('partition too large for FAT16')


def make_hdd(cyl, heads=16, spt=63, mbr=None, vbr=None, label='CLAUDEXT'):
    total_secs = cyl * heads * spt
    img = bytearray(total_secs * SECTOR)
    start = spt  # partition starts at cylinder 0, head 1
    count = total_secs - start
    m = bytearray(mbr if mbr else bytes(SECTOR))
    m[446:510] = bytes(64)
    ptype = 0x04 if count < 65536 else 0x06
    m[446:462] = (bytes([0x80]) + lba_to_chs(start, heads, spt) + bytes([ptype]) +
                  lba_to_chs(start + count - 1, heads, spt) + struct.pack('<II', start, count))
    m[510:512] = b'\x55\xAA'
    img[0:SECTOR] = m
    format_fat(img, start, count, fat_bits=16, spc=fat16_spc(count), root_ents=512, media=0xF8,
               spt=spt, heads=heads, drive=0x80, label=label, vbr=vbr)
    return img


def make_floppy(kb, vbr=None, label=''):
    total, spt, heads, spc, root_ents, media, spf = FLOPPY_PRESETS[kb]
    img = bytearray(total * SECTOR)
    format_fat(img, 0, total, fat_bits=12, spc=spc, root_ents=root_ents, media=media, spt=spt,
               heads=heads, drive=0x00, label=label, vbr=vbr, spf=spf)
    return img


# --- image access ------------------------------------------------------------

def partitions(img):
    if img[510:512] != b'\x55\xAA':
        return []
    if img[0] in (0xEB, 0xE9) and struct.unpack_from('<H', img, 11)[0] == SECTOR:
        return []  # looks like a boot sector with a BPB: unpartitioned
    out = []
    for i in range(4):
        e = img[446 + i * 16:462 + i * 16]
        ptype = e[4]
        start, count = struct.unpack_from('<II', e, 8)
        if ptype and count:
            out.append((i, ptype, start, count))
    return out


def open_fs(img, part=None):
    parts = partitions(img)
    if not parts:
        return FatFS(img, 0)
    if part is not None:
        for p in parts:
            if p[0] == part:
                return FatFS(img, p[2])
        raise FatError(f'no partition {part}')
    for p in parts:
        if p[1] in (0x01, 0x04, 0x06, 0x0E):
            return FatFS(img, p[2])
    raise FatError('no FAT12/16 partition found')


def put_tree(fs, src, dest):
    if os.path.isdir(src):
        fs.makedirs(dest, os.path.getmtime(src))
        for name in sorted(os.listdir(src)):
            put_tree(fs, os.path.join(src, name), f'{dest}/{name}')
    else:
        with open(src, 'rb') as f:
            fs.write_file(dest, f.read(), os.path.getmtime(src))


def get_tree(fs, src, dest):
    ent, _ = fs.lookup(src)
    if ent is None or ent.is_dir:
        os.makedirs(dest, exist_ok=True)
        for e in fs.listdir(ent.cluster if ent else 0):
            if e.name not in ('.', '..'):
                get_tree(fs, f'{src}/{e.name}', os.path.join(dest, e.name))
    else:
        with open(dest, 'wb') as f:
            f.write(fs.read_file(src))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-p', '--part', type=int, help='partition index (0-3)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('ls'); s.add_argument('img'); s.add_argument('path', nargs='?', default='/')
    s = sub.add_parser('tree'); s.add_argument('img')
    s = sub.add_parser('get'); s.add_argument('img'); s.add_argument('src'); s.add_argument('dest')
    s = sub.add_parser('put'); s.add_argument('img'); s.add_argument('src'); s.add_argument('dest')
    s = sub.add_parser('mkdir'); s.add_argument('img'); s.add_argument('path')
    s = sub.add_parser('rm'); s.add_argument('img'); s.add_argument('path')
    s = sub.add_parser('mkhdd'); s.add_argument('img'); s.add_argument('--cyl', type=int, required=True)
    s.add_argument('--heads', type=int, default=16); s.add_argument('--spt', type=int, default=63)
    s.add_argument('--mbr'); s.add_argument('--vbr'); s.add_argument('--label', default='CLAUDEXT')
    s = sub.add_parser('mkfloppy'); s.add_argument('img')
    s.add_argument('--kb', type=int, choices=sorted(FLOPPY_PRESETS), required=True)
    s.add_argument('--vbr'); s.add_argument('--label', default='')
    a = ap.parse_args(argv)

    def readbin(p):
        if not p:
            return None
        with open(p, 'rb') as f:
            return f.read()

    if a.cmd in ('mkhdd', 'mkfloppy'):
        img = (make_hdd(a.cyl, a.heads, a.spt, readbin(a.mbr), readbin(a.vbr), a.label)
               if a.cmd == 'mkhdd' else make_floppy(a.kb, readbin(a.vbr), a.label))
        with open(a.img, 'wb') as f:
            f.write(img)
        fs = open_fs(img)
        print(f'{a.img}: {len(img) // 1024} KB, FAT{fs.fat_bits}, {fs.nclusters} clusters '
              f'of {fs.csize} bytes, {fs.free_bytes() // 1024} KB free')
        return

    with open(a.img, 'rb') as f:
        img = bytearray(f.read())
    fs = open_fs(img, a.part)
    dirty = False
    if a.cmd == 'ls':
        ent, cl = fs.lookup(a.path)
        entries = fs.listdir(cl) if ent is None or ent.is_dir else [ent]
        for e in entries:
            print(f'{e.name:<12} {"<DIR>" if e.is_dir else e.size:>10}')
        print(f'{fs.free_bytes() // 1024} KB free')
    elif a.cmd == 'tree':
        for path, e in fs.walk():
            print(f'{path}{"/" if e.is_dir else ""}  {"" if e.is_dir else e.size}')
    elif a.cmd == 'get':
        get_tree(fs, a.src, a.dest)
    elif a.cmd == 'put':
        put_tree(fs, a.src, a.dest)
        dirty = True
    elif a.cmd == 'mkdir':
        fs.makedirs(a.path)
        dirty = True
    elif a.cmd == 'rm':
        fs.remove(a.path)
        dirty = True
    if dirty:
        with open(a.img, 'wb') as f:
            f.write(img)


if __name__ == '__main__':
    try:
        main()
    except FatError as e:
        sys.exit(f'fatimg: {e}')
