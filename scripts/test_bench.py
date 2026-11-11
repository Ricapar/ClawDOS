#!/usr/bin/env python3
"""Tests of scripts/bench.py's table (python3 scripts/test_bench.py, part of make test).

table() is the format later benchmark runs are compared in (docs/ROADMAP.md),
so a canned CLAWDOS.LOG checks every column and line it prints.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bench  # noqa: E402  pylint: disable=wrong-import-position



def api(when, fields):
    """An `api` line of CLAWDOS.LOG as chat.cpp's logRound writes it."""
    return f'2026-11-10 {when} api anthropic claude-opus-5-5 {fields} in=1 out=1 cr=0 cw=0 ev=900\n'


LOG = ('ClawDOS benchmark\n'
       '2026-11-10 10:00:04 connection #1, local port 1234\n'
       + api('10:00:05', 'round=0 ttft=3000 total=4000 up=2048 len=10240 stop=tool_use')
       + api('10:00:15', 'round=1 ttft=2500 total=5000 up=1000 len=20480 stop=tool_use')
       + '2026-11-10 10:00:40 relaunch: program 3.1 s, CLAWDOS away 20.5 s\n'
       + api('10:01:00', 'round=2 ttft=1000 total=11000 up=0 len=30720 stop=end_turn')
       + '2026-11-10 10:01:01 stack: used 1347 of 4027 bytes\n'
         '2026-11-10 10:01:02 stack: used 1557 of 4055 bytes\n'
         'CLAWDOS.EXE exit code 0\n')


class TableTest(unittest.TestCase):
    def setUp(self):
        self.lines = bench.table(LOG, 'Somewhere').splitlines()

    def test_header(self):
        self.assertTrue(self.lines[0].startswith('Somewhere, '))
        self.assertIn(', commit ', self.lines[0])
        self.assertEqual(self.lines[2], '| round | gap s | len | up ms | up KB/s | ttft-up ms | total ms |')
        self.assertEqual(self.lines[3], '|---:|---:|---:|---:|---:|---:|---:|')

    def test_rows(self):
        # gap: seconds between one round's end and the next round's start (its end less its total)
        self.assertEqual(self.lines[4:8], [
            '| 0 | - | 10240 | 2048 | 4.9 | 952 | 4000 |',
            '| 1 | 5 | 20480 | 1000 | 20.0 | 1500 | 5000 |',
            '| 2 | 34 | 30720 | 0 | - | 1000 | 11000 |',
            '| all | | 61440 | 3048 | 19.7 | | 20000 |',
        ])

    def test_summary(self):
        text = '\n'.join(self.lines[8:])
        self.assertIn('The turn took 59 s from the first request to the last reply.', text)
        streamed = bench.BENCH_REPLY
        self.assertIn(f'Final round: {len(streamed)} characters streamed in 10000 ms after the first, '
                      f'{len(streamed) / 10:.0f} characters/s (stop=end_turn).', text)
        self.assertIn('Relaunch round trip: program 3.1 s, CLAWDOS away 20.5 s.', text)
        self.assertIn('Stack: used 1347 of 4027 bytes; used 1557 of 4055 bytes (one line per HARNESS.EXE).', text)

    def test_no_rounds_is_an_error(self):
        with self.assertRaises(SystemExit):
            bench.table('nothing here', 'Somewhere')


if __name__ == '__main__':
    unittest.main()
