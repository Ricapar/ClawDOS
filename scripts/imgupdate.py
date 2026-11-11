"""Bring an existing ClawDOS disk image up to date in place (build.py --update).

The user's real PC runs from an image on an SD card that holds the PC's own data:
memories, chats, the log, settings and whatever programs they added. A fresh
build must not replace that, so this copies only what the build ships, and
everything it ships is whatever the fresh image holds: there is no list of
directories here beyond the ones the PC owns (below). A file on the image that
the build does not ship is never touched, never deleted.

Stdlib only; the FAT work is scripts/fatimg.py.
"""

import os
import re
import shutil
import struct

import fatimg

# What the PC owns, as upper-case paths without a leading slash. The build never
# writes these unless the matching flag says so.
CHATS_DIR = 'CLAWDOS/CHATS'                      # chat files *.JSN and their frozen prefixes *.PRE
MEMORY_DIR = 'MEMORY'                            # the model's memories (--memories)
CFG_FILE = 'CLAWDOS/CLAWDOS.CFG'                 # tunnel address, key, the user's edits (--cfg)
PC_FILES = ('CLAWDOS/CLAWDOS.LOG',               # the log
            'CLAWDOS/RUN.TXT', 'CLAWDOS/RUN.OUT',        # relaunch state (lib/relaunch.h)
            'CLAWDOS/RESUME.TXT', 'CLAWDOS/SCREEN.SAV')
MTCP_CFG = 'NET/MTCP.CFG'              # shipped, but mTCP's DHCP writes the lease into it on the PC


class UpdateError(Exception):
    """The image cannot be updated (and nothing was changed)."""


def norm(path):
    """The comparison form of an image path: upper case, '/' separators, no leading '/'."""
    return path.replace('\\', '/').strip('/').upper()


def under(path, folder):
    """True if the normalised path is inside the folder."""
    return path.startswith(folder + '/')


def owner(path, memories=False, cfg=False):
    """Why the build must leave this path alone ('chats', 'log', ...), or None if the build may write it."""
    p = norm(path)
    if under(p, CHATS_DIR):
        return 'chats'
    if p in PC_FILES:
        return 'PC state'
    if p == MTCP_CFG:
        return 'the PC writes its DHCP lease here; put the build\'s back with fatimg if you want it'
    if p == CFG_FILE and not cfg:
        return 'settings (--cfg replaces them)'
    if under(p, MEMORY_DIR) and not memories:
        return 'memories (--memories replaces the ones the build ships)'
    return None


def open_target(img):
    """The FatFS of a ClawDOS image (a bytearray); UpdateError if it is not one."""
    try:
        fs = fatimg.open_fs(img)
    except (fatimg.FatError, struct.error) as e:
        raise UpdateError(f'not a FAT disk image ({e})') from e
    try:
        ent, _ = fs.lookup('CLAWDOS')
    except fatimg.FatError:
        ent = None
    if ent is None or not ent.is_dir:
        raise UpdateError('not a ClawDOS image: it has no CLAWDOS directory')
    return fs


def shipped_files(fresh):
    """[(path, DirEntry)] of every file the fresh image holds, in a stable order."""
    return sorted(((p, e) for p, e in fresh.walk() if not e.is_dir), key=lambda pe: norm(pe[0]))


def current(fs, path):
    """('file', bytes), ('dir', None), ('missing', None) or ('blocked', None) for a path on the image.
    blocked: a file sits where a parent directory should be."""
    parts = [p for p in path.replace('\\', '/').split('/') if p]
    try:
        for i in range(1, len(parts)):
            ent, _ = fs.lookup('/'.join(parts[:i]))
            if not ent.is_dir:
                return 'blocked', None
        ent, _ = fs.lookup(path)
    except fatimg.FatError:
        return 'missing', None
    return ('dir', None) if ent.is_dir else ('file', fs.read_file(path))


class Report:
    """What an update did (or, in a dry run, would do)."""

    def __init__(self):
        self.added, self.changed, self.left, self.unsure = [], [], [], []
        self.unchanged = 0
        self.not_ours = 0                            # files on the image the build does not ship
        self.new_settings = []                       # [anthropic] lines in the template, absent from the image's CFG
        self.cfg_note = ''
        self.warnings = []
        self.boot_differs = False                    # the build's MBR/boot sector code is not the image's
        self.backup = None                           # the name the old image was copied to, if it was

    def lines(self, dry_run=False):
        """The report as text lines."""
        out = []
        verb = ('would add', 'would change') if dry_run else ('added', 'changed')
        out += [f'  {verb[0]:<13} {p}' for p in self.added]
        out += [f'  {verb[1]:<13} {p}' for p in self.changed]
        out += [f'  left alone    {p}  ({why})' for p, why in self.left]
        out += [f'  UNSURE, left  {p}  ({why})' for p, why in self.unsure]
        out.append(f'{len(self.added)} added, {len(self.changed)} changed, {self.unchanged} unchanged, '
                   f'{len(self.left) + len(self.unsure)} left alone, '
                   f'{self.not_ours} other files on the image not touched'
                   + (' (dry run: nothing was written)' if dry_run else ''))
        if not dry_run and not self.added and not self.changed:
            out.append('Nothing to update: the image was not written and no backup was made.')
        if self.backup:
            out.append(f'The old image is saved as {self.backup}')
        if self.boot_differs:
            out.append('Note: the build\'s boot code (MBR or boot sector) differs from the image\'s. --update does not '
                       'rewrite it; to get the new boot code, write a whole new image.')
        out += [f'WARNING: {w}' for w in self.warnings]
        if self.cfg_note:
            out.append(self.cfg_note)
        if self.new_settings:
            out.append('CLAWDOS.CFG was left alone; the template has these [anthropic] settings the image lacks '
                       '(add them with EDIT, or run again with --cfg to replace the file):')
            out += [f'    {s}' for s in self.new_settings]
        return out


def section_settings(text, section='anthropic'):
    """{key: 'key value' line, comment cut off} for the uncommented lines of one [section]; None if absent."""
    found, inside = None, False
    for raw in text.splitlines():
        line = raw.strip()
        m = re.match(r'\[([^\]]*)\]', line)
        if m:
            inside = m.group(1).strip().lower() == section
            if inside:
                found = {} if found is None else found
            continue
        if inside and line and not line.startswith('#'):
            key = line.split(None, 1)[0].lower()
            found.setdefault(key, re.sub(r'\s+#.*$', '', line))
    return found


def missing_settings(template, image):
    """The template's [anthropic] settings (as lines) that the image's CLAWDOS.CFG lacks, or None if the
    image has no [anthropic] section. The key lines are never listed."""
    want = section_settings(template)
    have = section_settings(image)
    if want is None or have is None:
        return None
    return [line for key, line in want.items() if key not in have and key not in ('key', 'keyfile')]


def has_auth_tunnel(cfg_text):
    """True if the [anthropic] section says `auth tunnel`."""
    line = (section_settings(cfg_text) or {}).get('auth', '')
    return line.split()[1:2] == ['tunnel']


def has_key(cfg_text):
    """True if the [anthropic] section has a key line."""
    return 'key' in (section_settings(cfg_text) or {})


def update_fs(target, fresh, memories=False, cfg=False, dry_run=False):
    """Copy what the fresh image ships into the target file system; returns a Report.
    Files are replaced when their contents differ, added when absent, and never removed."""
    rep = Report()
    shipped = set()
    for path, ent in shipped_files(fresh):
        shipped.add(norm(path))
        data = fresh.read_file(path.lstrip('/'))
        state, old = current(target, path)
        why = owner(path, memories, cfg)
        if why:
            if state != 'file' or old != data:
                rep.left.append((path.lstrip('/'), why))
            if norm(path) == CFG_FILE and state == 'file':
                rep.new_settings = missing_settings(data.decode('cp437'), old.decode('cp437'))
                if rep.new_settings is None:
                    rep.new_settings = []
                    rep.cfg_note = ('CLAWDOS.CFG has no [anthropic] section (or the template has none): '
                                    'its settings were not compared.')
            continue
        if norm(path) == CFG_FILE and state == 'file' and has_auth_tunnel(old.decode('cp437')) \
                and has_key(data.decode('cp437')):
            rep.warnings.append('CLAWDOS.CFG says `auth tunnel` (the tunnel supplies the key) and this build writes a '
                                'key line into it. Build with --no-key (remembered) to keep the key off the card.')
        if state in ('dir', 'blocked'):
            rep.unsure.append((path.lstrip('/'), 'a ' + ('directory is' if state == 'dir' else 'file is')
                               + ' in its way on the image'))
            continue
        if state == 'file' and old == data:
            rep.unchanged += 1
            continue
        (rep.changed if state == 'file' else rep.added).append(path.lstrip('/'))
        if not dry_run:
            parent = path.lstrip('/').rpartition('/')[0]
            if parent:
                target.makedirs(parent)
            target.write_file(path.lstrip('/'), data, attr=ent.attr & ~fatimg.ATTR_DIR)
    for path, ent in target.walk():
        if not ent.is_dir and norm(path) not in shipped:
            rep.not_ours += 1
    if cfg:
        rep.cfg_note = rep.cfg_note or ('CLAWDOS.CFG was replaced by the build\'s: the tunnel address and key line are '
                        'the build\'s, and any edit made on the PC is gone.')
    return rep


def boot_code(img):
    """(MBR code, partition boot sector code) of an image: the parts the build owns that are not files."""
    parts = [p for p in fatimg.partitions(img) if p[1] in (0x01, 0x04, 0x06, 0x0E)]
    if not parts:
        return bytes(img[:446]), b''
    vbr = parts[0][2] * fatimg.SECTOR
    return bytes(img[:446]), bytes(img[vbr:vbr + 3] + img[vbr + 62:vbr + 510])


def check_not_built(path, built_path):
    """UpdateError if `path` is the freshly built image itself (checked before building, by identity)."""
    if os.path.exists(path) and os.path.exists(built_path) and os.path.samefile(path, built_path):
        raise UpdateError('the image to update is the one the build writes (build/CLAWDOS.IMG)')


def free_backup_name(path):
    """path.bak, or path.bak.2, .3, ... : the first name not taken, so an earlier backup is never overwritten."""
    name, n = path + '.bak', 1
    while os.path.exists(name):
        n += 1
        name = f'{path}.bak.{n}'
    return name


def update_image(path, fresh_path, memories=False, cfg=False, backup=True, dry_run=False):
    """Update the image file `path` from the freshly built image `fresh_path`; returns a Report.
    Nothing is written when the update would add and change nothing. Otherwise the old image is first
    copied to a backup name that does not exist yet (path.bak, .bak.2, ...; unless backup is False) and
    the new one replaces it only when every file was written: a failure leaves the image as it was."""
    if not os.path.isfile(path):
        raise UpdateError(f'{path}: no such image')
    if os.path.exists(fresh_path) and os.path.samefile(path, fresh_path):
        raise UpdateError('the image to update is the one just built')
    with open(path, 'rb') as f:
        img = bytearray(f.read())
    target = open_target(img)
    with open(fresh_path, 'rb') as f:
        fresh_img = bytearray(f.read())
    try:
        rep = update_fs(target, fatimg.open_fs(fresh_img), memories, cfg, dry_run)
    except fatimg.FatError as e:
        raise UpdateError(f'{path} is unchanged: {e}') from e
    rep.boot_differs = boot_code(img) != boot_code(fresh_img)
    if dry_run or not (rep.added or rep.changed):
        return rep
    tmp = path + '.new'
    made = None
    try:
        if backup:
            made = free_backup_name(path)
            shutil.copyfile(path, made)
        with open(tmp, 'wb') as f:
            f.write(img)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    except OSError as e:
        for leftover in (tmp, made):
            if leftover and os.path.exists(leftover):
                os.remove(leftover)
        raise UpdateError(f'{path} is unchanged: {e.strerror or e}') from e
    rep.backup = made
    return rep
