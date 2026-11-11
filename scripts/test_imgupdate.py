#!/usr/bin/env python3
"""Tests of scripts/imgupdate.py (python3 scripts/test_imgupdate.py, part of make test).

Every image here is made in a temporary directory; no real image is involved.
The "PC" image holds data the build must never touch, the "fresh" one is what a build ships.
"""

import contextlib
import io
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build  # noqa: E402  pylint: disable=wrong-import-position
import fatimg  # noqa: E402  pylint: disable=wrong-import-position
import imgupdate  # noqa: E402  pylint: disable=wrong-import-position

CFG_TEMPLATE = (b'provider anthropic\r\n\r\n[anthropic]\r\ntunnel 10.0.2.2:8443\r\nhost api.anthropic.com\r\n'
                b'model claude-opus-5-5\r\nthreads 1             # message threads\r\nmaxtokens 32000\r\n'
                b'# cachettl 1h\r\nkey sk-ant-fresh-made-up\r\n\r\n[other]\r\nonlyhere 1\r\n')
CFG_PC = (b'provider anthropic\r\n\r\n[anthropic]\r\ntunnel 192.168.1.50:8443\r\nhost api.anthropic.com\r\n'
          b'model claude-opus-5-5\r\nkey sk-ant-pc-made-up\r\n')

FRESH = {
    'KERNEL.SYS': b'kernel v2',
    'CLAWDOS/HARNESS.EXE': b'harness v2',
    'CLAWDOS/CLAWDOS.EXE': b'launcher',
    'CLAWDOS/CLAWDOS.CFG': CFG_TEMPLATE,
    'CLAWDOS/PROMPT.TXT': b'prompt v2',
    'NET/MTCP.CFG': b'PACKET_INT 0x60\r\n',
    'MEMORY/DEVTOOLS.TXT': b'devtools v2',
    'MEMORY/NEWMEM.TXT': b'a new memory',
    'SKILLS/DEEP/ONE.TXT': b'a new folder tree',
}
PC = {
    'KERNEL.SYS': b'kernel v1',
    'CLAWDOS/HARNESS.EXE': b'harness v1',
    'CLAWDOS/CLAWDOS.EXE': b'launcher',                   # unchanged
    'CLAWDOS/CLAWDOS.CFG': CFG_PC,
    'CLAWDOS/CLAWDOS.LOG': b'log of the PC',
    'CLAWDOS/CHATS/A.JSN': b'[a chat]',
    'CLAWDOS/CHATS/A.PRE': b'frozen prefix',
    'CLAWDOS/RUN.TXT': b'run', 'CLAWDOS/RUN.OUT': b'out',
    'CLAWDOS/RESUME.TXT': b'resume', 'CLAWDOS/SCREEN.SAV': b'screen',
    'MEMORY/DEVTOOLS.TXT': b'devtools v1, edited by the user',
    'MEMORY/MINE.TXT': b'the PC wrote this',
    'NET/MTCP.CFG': b'PACKET_INT 0x60\r\nIPADDR 10.0.0.9 (a DHCP lease)\r\n',
    'GAMES/PONG.EXE': b'the user\'s own program',
    'CLAWDOS/NOTES.TXT': b'a file in a folder the build ships to',
}


def make_image(files, cyl=9):
    """A disk image (bytearray) holding the files."""
    img = fatimg.make_hdd(cyl)
    fs = fatimg.open_fs(img)
    for path, data in files.items():
        parent = path.rpartition('/')[0]
        if parent:
            fs.makedirs(parent)
        fs.write_file(path, data)
    return img


def contents(img):
    """{PATH: bytes} of every file on an image."""
    fs = fatimg.open_fs(bytearray(img))
    return {p.lstrip('/').upper(): fs.read_file(p.lstrip('/')) for p, e in fs.walk() if not e.is_dir}


class UpdateFsTest(unittest.TestCase):
    def update(self, pc=None, fresh=None, **opts):
        target_img = make_image(PC if pc is None else pc)
        rep = imgupdate.update_fs(imgupdate.open_target(target_img),
                                  fatimg.open_fs(make_image(FRESH if fresh is None else fresh)), **opts)
        return rep, contents(target_img)

    def test_adds_changes_and_counts(self):
        rep, after = self.update()
        self.assertEqual(after['KERNEL.SYS'], b'kernel v2')
        self.assertEqual(after['CLAWDOS/HARNESS.EXE'], b'harness v2')
        self.assertEqual(after['CLAWDOS/PROMPT.TXT'], b'prompt v2')
        self.assertEqual(after['SKILLS/DEEP/ONE.TXT'], b'a new folder tree')
        self.assertCountEqual(rep.changed, ['KERNEL.SYS', 'CLAWDOS/HARNESS.EXE'])
        self.assertCountEqual(rep.added, ['CLAWDOS/PROMPT.TXT', 'SKILLS/DEEP/ONE.TXT'])
        self.assertEqual(rep.unchanged, 1)

    def test_the_pcs_own_data_survives(self):
        _, after = self.update()
        for path in ('CLAWDOS/CLAWDOS.LOG', 'CLAWDOS/CHATS/A.JSN', 'CLAWDOS/CHATS/A.PRE', 'CLAWDOS/RUN.TXT',
                     'CLAWDOS/RUN.OUT', 'CLAWDOS/RESUME.TXT', 'CLAWDOS/SCREEN.SAV', 'MEMORY/MINE.TXT',
                     'GAMES/PONG.EXE', 'CLAWDOS/NOTES.TXT'):
            self.assertEqual(after[path], PC[path], path)
        self.assertEqual(after['CLAWDOS/CLAWDOS.CFG'], CFG_PC)
        self.assertEqual(after['MEMORY/DEVTOOLS.TXT'], PC['MEMORY/DEVTOOLS.TXT'])
        self.assertNotIn('MEMORY/NEWMEM.TXT', after)
        self.assertEqual(after['NET/MTCP.CFG'], PC['NET/MTCP.CFG'])     # the DHCP lease lives there

    def test_nothing_is_deleted(self):
        _, after = self.update()
        self.assertTrue(set(PC) <= set(after))

    def test_protected_shipped_files_are_reported_not_hidden(self):
        rep, _ = self.update()
        left = dict(rep.left)
        self.assertIn('CLAWDOS/CLAWDOS.CFG', left)
        self.assertIn('MEMORY/DEVTOOLS.TXT', left)
        self.assertIn('MEMORY/NEWMEM.TXT', left)
        self.assertIn('NET/MTCP.CFG', left)

    def test_other_files_counted(self):
        rep, _ = self.update()
        # on the PC and not shipped: LOG, 2 chat files, 4 relaunch files, MINE, PONG, NOTES
        self.assertEqual(rep.not_ours, 10)

    def test_memories_flag_replaces_only_shipped_memories(self):
        rep, after = self.update(memories=True)
        self.assertEqual(after['MEMORY/DEVTOOLS.TXT'], b'devtools v2')
        self.assertEqual(after['MEMORY/NEWMEM.TXT'], b'a new memory')
        self.assertEqual(after['MEMORY/MINE.TXT'], PC['MEMORY/MINE.TXT'])
        self.assertEqual(after['CLAWDOS/CLAWDOS.CFG'], CFG_PC)
        self.assertIn('MEMORY/NEWMEM.TXT', rep.added)

    def test_no_flag_replaces_mtcp_cfg(self):
        _, after = self.update(memories=True, cfg=True)
        self.assertEqual(after['NET/MTCP.CFG'], PC['NET/MTCP.CFG'])

    def test_cfg_flag_replaces_the_config_only(self):
        rep, after = self.update(cfg=True)
        self.assertEqual(after['CLAWDOS/CLAWDOS.CFG'], CFG_TEMPLATE)
        self.assertEqual(after['MEMORY/DEVTOOLS.TXT'], PC['MEMORY/DEVTOOLS.TXT'])
        self.assertEqual(after['CLAWDOS/CLAWDOS.LOG'], PC['CLAWDOS/CLAWDOS.LOG'])
        self.assertIn('CLAWDOS/CLAWDOS.CFG', rep.changed)
        self.assertTrue(rep.cfg_note)

    def test_chats_and_state_never_written_even_with_every_flag(self):
        fresh = dict(FRESH, **{'CLAWDOS/CHATS/A.JSN': b'from the build', 'CLAWDOS/CLAWDOS.LOG': b'build log',
                               'CLAWDOS/RESUME.TXT': b'build resume'})
        _, after = self.update(fresh=fresh, memories=True, cfg=True)
        self.assertEqual(after['CLAWDOS/CHATS/A.JSN'], b'[a chat]')
        self.assertEqual(after['CLAWDOS/CLAWDOS.LOG'], b'log of the PC')
        self.assertEqual(after['CLAWDOS/RESUME.TXT'], b'resume')

    def test_new_settings_listed_when_the_config_is_left_alone(self):
        rep, _ = self.update()
        self.assertEqual(rep.new_settings, ['threads 1', 'maxtokens 32000'])
        text = '\n'.join(rep.lines())
        self.assertIn('threads 1', text)
        self.assertNotIn('sk-ant', text)                      # no key line, either side
        self.assertNotIn('onlyhere', text)                    # only the [anthropic] section counts

    def test_a_directory_in_the_way_is_left_and_said(self):
        pc = dict(PC)
        pc['CLAWDOS/PROMPT.TXT/X'] = b'a folder where the build ships a file'
        rep, after = self.update(pc=pc)
        self.assertEqual([p for p, _ in rep.unsure], ['CLAWDOS/PROMPT.TXT'])
        self.assertEqual(after['CLAWDOS/PROMPT.TXT/X'], b'a folder where the build ships a file')
        self.assertIn('UNSURE', '\n'.join(rep.lines()))

    def test_a_file_where_a_folder_should_be_is_left_and_said(self):
        pc = dict(PC)
        pc['SKILLS'] = b'a file'
        rep, after = self.update(pc=pc)
        self.assertEqual([p for p, _ in rep.unsure], ['SKILLS/DEEP/ONE.TXT'])
        self.assertEqual(after['SKILLS'], b'a file')

    def test_system_attribute_follows_the_build(self):
        fresh_img = make_image({})
        ffs = fatimg.open_fs(fresh_img)
        ffs.write_file('KERNEL.SYS', b'k2', attr=fatimg.ATTR_ARCHIVE | fatimg.ATTR_SYSTEM | fatimg.ATTR_HIDDEN)
        pc_img = make_image({'CLAWDOS/X.TXT': b'x', 'KERNEL.SYS': b'k1'})
        imgupdate.update_fs(imgupdate.open_target(pc_img), ffs)
        ent, _ = fatimg.open_fs(pc_img).lookup('KERNEL.SYS')
        self.assertTrue(ent.attr & fatimg.ATTR_SYSTEM)

    def test_names_compare_without_case(self):
        self.assertEqual(imgupdate.owner('clawdos\\chats\\x.jsn'), 'chats')
        self.assertIsNone(imgupdate.owner('CLAWDOS/CHATSX/A'))     # a sibling is not the chats folder
        self.assertIsNone(imgupdate.owner('CLAWDOS/CLAWDOS.CFG', cfg=True))
        self.assertIsNotNone(imgupdate.owner('memory/a.txt'))
        self.assertIsNone(imgupdate.owner('memory/a.txt', memories=True))

    def test_a_dry_run_changes_nothing(self):
        pc_img = make_image(PC)
        before = bytes(pc_img)
        rep = imgupdate.update_fs(imgupdate.open_target(pc_img), fatimg.open_fs(make_image(FRESH)), dry_run=True)
        self.assertEqual(bytes(pc_img), before)
        self.assertEqual(len(rep.added), 2)
        self.assertIn('would add', '\n'.join(rep.lines(True)))

    def test_skills_are_the_builds(self):
        pc = dict(PC, **{'SKILLS/DEVTOOLS.TXT': b'skill v1', 'SKILLS/MINE.TXT': b'a skill made on the PC'})
        fresh = dict(FRESH, **{'SKILLS/DEVTOOLS.TXT': b'skill v2', 'SKILLS/DEMOFX.TXT': b'demofx'})
        rep, after = self.update(pc=pc, fresh=fresh)
        self.assertEqual(after['SKILLS/DEVTOOLS.TXT'], b'skill v2')
        self.assertEqual(after['SKILLS/DEMOFX.TXT'], b'demofx')
        self.assertEqual(after['SKILLS/MINE.TXT'], b'a skill made on the PC')   # not shipped: never touched
        self.assertIn('SKILLS/DEVTOOLS.TXT', rep.changed)
        self.assertIn('SKILLS/DEMOFX.TXT', rep.added)
        self.assertNotIn('SKILLS/DEVTOOLS.TXT', dict(rep.left))

    def test_a_memory_named_like_a_skill_is_noted_and_kept(self):
        # a card from a build that shipped DEVTOOLS and DEMOFX as memories, one of them edited since
        pc = dict(PC, **{'MEMORY/DEVTOOLS.TXT': b'devtools, edited', 'MEMORY/DEMOFX.TXT': b'demofx'})
        fresh = {'CLAWDOS/HARNESS.EXE': b'harness v3', 'SKILLS/DEVTOOLS.TXT': b'devtools',
                 'SKILLS/DEMOFX.TXT': b'demofx', 'SKILLS/DEEP/MINE.TXT': b'deeper'}
        pc['MEMORY/DEEP/MINE.TXT'] = b'only a skill straight in SKILLS/ counts'
        rep, after = self.update(pc=pc, fresh=fresh)
        self.assertEqual(after['MEMORY/DEVTOOLS.TXT'], b'devtools, edited')
        self.assertEqual(after['MEMORY/DEMOFX.TXT'], b'demofx')
        self.assertEqual(rep.old_skills, [('MEMORY/DEMOFX.TXT', 'SKILLS/DEMOFX.TXT', True),
                                          ('MEMORY/DEVTOOLS.TXT', 'SKILLS/DEVTOOLS.TXT', False)])
        text = '\n'.join(rep.lines())
        self.assertIn('the memory MEMORY/DEMOFX.TXT has the name of the skill SKILLS/DEMOFX.TXT', text)
        self.assertIn('same text as the skill, so delete it with F4', text)
        self.assertIn('if nobody edited it, delete it with F4', text)
        self.assertIn('Note:', '\n'.join(rep.lines(True)))               # a dry run says it too

    def test_no_note_without_such_a_memory(self):
        rep, _ = self.update()
        self.assertEqual(rep.old_skills, [])
        self.assertNotIn('has the name of the skill', '\n'.join(rep.lines()))

    def test_the_manifest_ships_skills_and_no_memories(self):
        dests = [dest for dest, _ in build.manifest_entries()]
        self.assertIn('SKILLS/', dests)
        self.assertFalse([d for d in dests if (imgupdate.norm(d) + '/').startswith('MEMORY/')])
        self.assertIsNone(imgupdate.owner('SKILLS/DEVTOOLS.TXT'))

    def test_the_second_update_changes_nothing(self):
        pc_img = make_image(PC)
        fresh = fatimg.open_fs(make_image(FRESH))
        imgupdate.update_fs(imgupdate.open_target(pc_img), fresh)
        rep = imgupdate.update_fs(imgupdate.open_target(pc_img), fresh)
        self.assertEqual((rep.added, rep.changed), ([], []))


class UpdateImageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pc = os.path.join(self.tmp.name, 'PC.IMG')
        self.fresh = os.path.join(self.tmp.name, 'FRESH.IMG')
        for path, files in ((self.pc, PC), (self.fresh, FRESH)):
            with open(path, 'wb') as f:
                f.write(make_image(files))

    def read(self, path):
        with open(path, 'rb') as f:
            return f.read()

    def test_backup_then_update(self):
        before = self.read(self.pc)
        imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.read(self.pc + '.bak'), before)
        self.assertEqual(contents(self.read(self.pc))['KERNEL.SYS'], b'kernel v2')
        self.assertFalse(os.path.exists(self.pc + '.new'))

    def test_no_backup(self):
        imgupdate.update_image(self.pc, self.fresh, backup=False)
        self.assertFalse(os.path.exists(self.pc + '.bak'))

    def test_dry_run_writes_no_file_at_all(self):
        before = self.read(self.pc)
        imgupdate.update_image(self.pc, self.fresh, dry_run=True)
        self.assertEqual(self.read(self.pc), before)
        self.assertEqual(sorted(os.listdir(self.tmp.name)), ['FRESH.IMG', 'PC.IMG'])

    def test_refuses_an_image_without_clawdos(self):
        with open(self.pc, 'wb') as f:
            f.write(make_image({'DOS/X.TXT': b'x'}))
        before = self.read(self.pc)
        with self.assertRaisesRegex(imgupdate.UpdateError, 'no CLAWDOS directory'):
            imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.read(self.pc), before)
        self.assertFalse(os.path.exists(self.pc + '.bak'))

    def test_refuses_what_is_not_an_image(self):
        with open(self.pc, 'wb') as f:
            f.write(b'not a disk')
        with self.assertRaises(imgupdate.UpdateError):
            imgupdate.update_image(self.pc, self.fresh)
        with self.assertRaisesRegex(imgupdate.UpdateError, 'no such image'):
            imgupdate.update_image(os.path.join(self.tmp.name, 'NOPE.IMG'), self.fresh)

    def test_refuses_the_image_just_built(self):
        with self.assertRaisesRegex(imgupdate.UpdateError, 'just built'):
            imgupdate.update_image(self.fresh, self.fresh)

    def test_a_full_disk_leaves_the_image_untouched(self):
        with open(self.fresh, 'wb') as f:
            f.write(make_image(dict(FRESH, **{'CLAWDOS/HUGE.BIN': bytes(5 * 1024 * 1024)}), cyl=16))
        before = self.read(self.pc)
        with self.assertRaisesRegex(imgupdate.UpdateError, 'unchanged'):
            imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.read(self.pc), before)
        self.assertFalse(os.path.exists(self.pc + '.bak'))


class UpdateWarningsTest(unittest.TestCase):
    def run_update(self, pc_cfg, fresh_cfg, **kw):
        pc_img = make_image({'CLAWDOS/CLAWDOS.CFG': pc_cfg})
        fresh = fatimg.open_fs(make_image({'CLAWDOS/CLAWDOS.CFG': fresh_cfg}))
        return imgupdate.update_fs(imgupdate.open_target(pc_img), fresh, **kw)

    def test_a_key_written_over_auth_tunnel_warns(self):
        pc = b'[anthropic]\r\nmodel m\r\nauth tunnel      # the tunnel has the key\r\n'
        rep = self.run_update(pc, CFG_TEMPLATE, cfg=True)
        self.assertEqual(len(rep.warnings), 1)
        text = '\n'.join(rep.lines())
        self.assertIn('WARNING', text)
        self.assertNotIn('sk-ant', text)
        self.assertEqual(self.run_update(CFG_PC, CFG_TEMPLATE, cfg=True).warnings, [])
        self.assertEqual(self.run_update(pc, CFG_TEMPLATE).warnings, [])   # config left alone: nothing to warn of

    def test_auth_tunnel_is_recognised_in_any_case(self):
        pc = b'[anthropic]\r\nAUTH Tunnel\r\n'
        self.assertEqual(len(self.run_update(pc, CFG_TEMPLATE, cfg=True).warnings), 1)

    def test_cfg_note_only_when_the_config_really_was_replaced(self):
        rep = self.run_update(CFG_TEMPLATE, CFG_TEMPLATE, cfg=True)
        self.assertEqual(rep.cfg_note, '')
        self.assertNotIn('replaced', '\n'.join(rep.lines()))
        self.assertIn('replaced', '\n'.join(self.run_update(CFG_PC, CFG_TEMPLATE, cfg=True).lines()))

    def test_protected_files_missing_from_the_image_are_added(self):
        pc_img = make_image({'CLAWDOS/X.TXT': b'x'})
        fresh = fatimg.open_fs(make_image({'NET/MTCP.CFG': b'cfg', 'CLAWDOS/CLAWDOS.CFG': CFG_TEMPLATE,
                                           'MEMORY/NEW.TXT': b'm', 'CLAWDOS/CLAWDOS.LOG': b'log'}))
        rep = imgupdate.update_fs(imgupdate.open_target(pc_img), fresh)
        after = contents(pc_img)
        self.assertEqual(after['NET/MTCP.CFG'], b'cfg')
        self.assertEqual(after['CLAWDOS/CLAWDOS.CFG'], CFG_TEMPLATE)
        self.assertCountEqual(rep.added, ['NET/MTCP.CFG', 'CLAWDOS/CLAWDOS.CFG'])
        self.assertNotIn('MEMORY/NEW.TXT', after)             # memories and PC state still wait for their flag
        self.assertNotIn('CLAWDOS/CLAWDOS.LOG', after)

    def test_no_anthropic_section_is_said(self):
        rep = self.run_update(b'[other]\r\nx 1\r\n', CFG_TEMPLATE)
        self.assertIn('no [anthropic] section', '\n'.join(rep.lines()))
        self.assertEqual(rep.new_settings, [])


class UpdateBackupTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pc = os.path.join(self.tmp.name, 'PC.IMG')
        self.fresh = os.path.join(self.tmp.name, 'FRESH.IMG')
        self.write(self.pc, make_image(PC))
        self.write(self.fresh, make_image(FRESH))

    @staticmethod
    def write(path, data):
        with open(path, 'wb') as f:
            f.write(data)

    @staticmethod
    def read(path):
        with open(path, 'rb') as f:
            return f.read()

    def names(self):
        return sorted(os.listdir(self.tmp.name))

    def test_an_existing_backup_is_never_overwritten(self):
        original = self.read(self.pc)
        imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.read(self.pc + '.bak'), original)
        after_first = self.read(self.pc)
        self.write(self.fresh, make_image(dict(FRESH, **{'CLAWDOS/PROMPT.TXT': b'prompt v3'})))
        rep = imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.read(self.pc + '.bak'), original)                 # still the pre-update image
        self.assertEqual(self.read(self.pc + '.bak.2'), after_first)
        self.assertEqual(rep.backup, self.pc + '.bak.2')
        self.assertIn(self.pc + '.bak.2', '\n'.join(rep.lines()))
        self.write(self.fresh, make_image(dict(FRESH, **{'CLAWDOS/PROMPT.TXT': b'prompt v4'})))
        self.assertEqual(imgupdate.update_image(self.pc, self.fresh).backup, self.pc + '.bak.3')

    def test_nothing_to_do_writes_nothing(self):
        imgupdate.update_image(self.pc, self.fresh)
        done, bak = self.read(self.pc), self.read(self.pc + '.bak')
        stamp = os.stat(self.pc).st_mtime_ns
        rep = imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual((rep.added, rep.changed, rep.backup), ([], [], None))
        self.assertEqual(self.read(self.pc), done)
        self.assertEqual(os.stat(self.pc).st_mtime_ns, stamp)
        self.assertEqual(self.names(), ['FRESH.IMG', 'PC.IMG', 'PC.IMG.bak'])
        self.assertEqual(self.read(self.pc + '.bak'), bak)
        self.assertIn('Nothing to update', '\n'.join(rep.lines()))

    def test_a_failed_backup_copy_leaves_the_image_and_no_leftovers(self):
        before = self.read(self.pc)

        def fail(src, dst):
            self.write(dst, b'trunc')
            raise OSError(28, 'No space left on device')
        with mock.patch.object(imgupdate.shutil, 'copyfile', fail):
            with self.assertRaisesRegex(imgupdate.UpdateError, 'unchanged.*No space'):
                imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.read(self.pc), before)
        self.assertEqual(self.names(), ['FRESH.IMG', 'PC.IMG'])

    def test_a_failed_write_removes_only_the_temp_file(self):
        before = self.read(self.pc)
        with mock.patch.object(imgupdate.os, 'fsync', side_effect=OSError(28, 'No space left on device')):
            with self.assertRaisesRegex(imgupdate.UpdateError, 'unchanged.*writing.*No space'):
                imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.read(self.pc), before)
        self.assertEqual(self.names(), ['FRESH.IMG', 'PC.IMG', 'PC.IMG.bak'])    # the complete backup stays
        self.assertEqual(self.read(self.pc + '.bak'), before)

    def test_a_failed_rename_deletes_nothing_and_says_where_everything_is(self):
        before = self.read(self.pc)

        def fail(src, dst):                   # a FAT card: the old file is already gone when the rename fails
            os.remove(dst)
            raise OSError(5, 'Input/output error')
        with mock.patch.object(imgupdate.os, 'replace', fail):
            with self.assertRaises(imgupdate.UpdateError) as cm:
                imgupdate.update_image(self.pc, self.fresh)
        msg = str(cm.exception)
        self.assertIn('Nothing was deleted', msg)
        self.assertIn(self.pc + ' DOES NOT EXIST', msg)
        self.assertIn(self.pc + '.new', msg)
        self.assertIn(self.pc + '.bak', msg)
        self.assertEqual(self.names(), ['FRESH.IMG', 'PC.IMG.bak', 'PC.IMG.new'])
        self.assertEqual(self.read(self.pc + '.bak'), before)
        self.assertEqual(contents(self.read(self.pc + '.new'))['KERNEL.SYS'], b'kernel v2')

    def test_a_failed_rename_that_left_the_image_says_so(self):
        with mock.patch.object(imgupdate.os, 'replace', side_effect=OSError(30, 'Read-only file system')):
            with self.assertRaisesRegex(imgupdate.UpdateError, 'The image still exists'):
                imgupdate.update_image(self.pc, self.fresh)
        self.assertEqual(self.names(), ['FRESH.IMG', 'PC.IMG', 'PC.IMG.bak', 'PC.IMG.new'])

    def test_boot_code_difference_is_reported_not_rewritten(self):
        mbr = bytes([0x90]) * 446
        self.write(self.fresh, fatimg.make_hdd(9, mbr=mbr))
        fresh = fatimg.open_fs(bytearray(self.read(self.fresh)))
        for path, data in FRESH.items():
            if '/' in path:
                fresh.makedirs(path.rpartition('/')[0])
            fresh.write_file(path, data)
        self.write(self.fresh, bytes(fresh.img))
        old_mbr = self.read(self.pc)[:446]
        rep = imgupdate.update_image(self.pc, self.fresh)
        self.assertTrue(rep.boot_differs)
        self.assertIn('boot code', '\n'.join(rep.lines()))
        self.assertEqual(self.read(self.pc)[:446], old_mbr)

    def test_same_boot_code_is_not_reported(self):
        self.assertFalse(imgupdate.update_image(self.pc, self.fresh, dry_run=True).boot_differs)

    def test_check_not_built_sees_through_a_symlink(self):
        link = os.path.join(self.tmp.name, 'LINK.IMG')
        os.symlink(self.fresh, link)
        with self.assertRaisesRegex(imgupdate.UpdateError, 'build writes'):
            imgupdate.check_not_built(link, self.fresh)
        imgupdate.check_not_built(self.pc, self.fresh)
        imgupdate.check_not_built(self.pc, os.path.join(self.tmp.name, 'NOT-YET.IMG'))


class BuildMainTest(unittest.TestCase):
    """build.py's own flag checks, and --update through main with the toolchain steps stubbed out."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pc = os.path.join(self.tmp.name, 'PC.IMG')
        self.fresh = os.path.join(self.tmp.name, 'FRESH.IMG')
        for path, files in ((self.pc, PC), (self.fresh, FRESH)):
            with open(path, 'wb') as f:
                f.write(make_image(files))

    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        code = None
        patches = [mock.patch.object(build, 'build_boot'), mock.patch.object(build, 'build_apps'),
                   mock.patch.object(build, 'write_compiledb'),
                   mock.patch.object(build, 'build_image', return_value=self.fresh),
                   mock.patch.object(build, 'api_key', return_value=None),
                   mock.patch.object(build, 'SETTINGS_FILE', os.path.join(self.tmp.name, 'settings.json')),
                   mock.patch.object(build, 'BUILD', self.tmp.name)]
        with contextlib.ExitStack() as stack:
            for p in patches:
                stack.enter_context(p)
            stack.enter_context(contextlib.redirect_stdout(out))
            stack.enter_context(contextlib.redirect_stderr(err))
            try:
                build.main(list(args))
            except SystemExit as e:
                code = e.code
        return code, out.getvalue(), err.getvalue()

    def test_flags_that_go_with_update_need_it(self):
        for flag in ('--dry-run', '--memories', '--cfg', '--no-backup'):
            code, _, err = self.run_main(flag)
            self.assertEqual(code, 2, flag)
            self.assertIn('--update', err)

    def test_update_refuses_autotest_and_key_with_no_key(self):
        self.assertEqual(self.run_main('--update', self.pc, '--autotest', 'x.bat')[0], 2)
        self.assertEqual(self.run_main('--key', '--no-key')[0], 2)

    def test_update_refuses_a_non_image_before_building(self):
        with open(self.pc, 'wb') as f:
            f.write(make_image({'DOS/X.TXT': b'x'}))
        with mock.patch.object(build, 'build_boot') as boot:
            code, _, _ = self.run_main('--update', self.pc)
        self.assertIn('no CLAWDOS directory', str(code))
        boot.assert_not_called()

    def test_update_through_main(self):
        code, out, _ = self.run_main('--update', self.pc, '--dry-run')
        self.assertIsNone(code)
        self.assertRegex(out, r'would change\s+KERNEL.SYS')
        code, out, _ = self.run_main('--update', self.pc)
        self.assertIsNone(code)
        self.assertIn('The old image is saved as ' + self.pc + '.bak', out)
        with open(self.pc, 'rb') as f:
            self.assertEqual(contents(f.read())['KERNEL.SYS'], b'kernel v2')

    def test_no_key_is_remembered_until_key(self):
        self.run_main('--no-key', '--update', self.pc, '--dry-run')
        self.assertTrue(self.settings_nokey())
        self.run_main('--key', '--update', self.pc, '--dry-run')
        self.assertFalse(self.settings_nokey())

    def settings_nokey(self):
        with mock.patch.object(build, 'SETTINGS_FILE', os.path.join(self.tmp.name, 'settings.json')):
            return build.settings()['nokey']

    def test_remembered_no_key_makes_the_config_say_auth_tunnel(self):
        with open(os.path.join(build.ROOT, 'image', 'clawdos', 'CLAWDOS.CFG'), 'rb') as f:
            template = f.read()
        with mock.patch.object(build, 'api_key', return_value='sk-ant-made-up'), \
                mock.patch.object(build, 'settings', return_value={'tunnel': '1.2.3.4:8443', 'nokey': True}):
            text = build.clawdos_cfg(template).decode('cp437')
        self.assertNotIn('sk-ant-made-up', text)
        self.assertIn('auth tunnel  ', text)


class BuildCfgTest(unittest.TestCase):
    """build.clawdos_cfg on the repo's real template, with a made-up key."""

    def cfg(self, **kw):
        with open(os.path.join(build.ROOT, 'image', 'clawdos', 'CLAWDOS.CFG'), 'rb') as f:
            template = f.read()
        with mock.patch.object(build, 'api_key', return_value='sk-ant-made-up'), \
                mock.patch.object(build, 'settings', return_value={'tunnel': '1.2.3.4:8443'}):
            return build.clawdos_cfg(template, **kw).decode('cp437')

    def lines(self, text):
        return [l.split('#')[0].strip() for l in text.splitlines()]

    def test_default_has_the_key_and_no_tunnel_auth(self):
        text = self.cfg()
        self.assertIn('key sk-ant-made-up', self.lines(text))
        self.assertNotIn('auth tunnel', self.lines(text))
        self.assertIn('tunnel 1.2.3.4:8443', self.lines(text))

    def test_no_key_says_auth_tunnel_and_carries_no_key(self):
        text = self.cfg(with_key=False, tunnel_key=True)
        self.assertIn('auth tunnel', self.lines(text))
        self.assertNotIn('sk-ant-made-up', text)
        self.assertFalse([l for l in self.lines(text) if l.startswith('key ')])

    def test_no_key_wins_over_a_key_in_env(self):
        text = self.cfg(with_key=True, tunnel_key=True)
        self.assertNotIn('sk-ant-made-up', text)

    def test_the_template_has_the_auth_line_to_turn_on(self):
        self.assertTrue(any(l.startswith('# auth tunnel') for l in self.cfg().splitlines()))


if __name__ == '__main__':
    unittest.main()
