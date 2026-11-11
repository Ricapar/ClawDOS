#!/usr/bin/env python3
"""The ClawDOS benchmark: a fixed agent turn in 86Box (an 8088 at 4.77 MHz) against the mock API.

  python scripts/bench.py [--dosbox] [--timeout SECONDS] [--no-build]

Runs image/tests/bench.bat through scripts/test8088.py --mock: the mock's
!bench scenario (tunnel/mockchat.py) makes HARNESS.EXE do 20 reads of a 4 KB
file, one run with full_memory (a real relaunch through CLAWDOS.EXE) and a 4 KB
reply, so the request body grows from ~13 KB to over 100 KB. Then it reads the
`api ...`, `relaunch:` and `stack:` lines of CLAWDOS.LOG and prints a Markdown
table: per round the seconds since the previous round ended (gap: the tool
call, the history written, the prefix file checked; from the log's
timestamps), the request body (len), the upload time (up), upload KB/s, the
wait for the API (ttft - up) and the round's total; for the final round
the streamed characters per second; then the relaunch round trip and the
stack's high-water mark. docs/ROADMAP.md keeps the table ("Benchmark"); later
changes rerun this and compare, so the scenario and the format stay as they are.

--dosbox runs the same in DOSBox-X through boottest.py, for a quick check that
it works: its numbers are not comparable (an 80186 at full host speed).
--timeout is how long to wait for the run: by default 5400 s in 86Box, 600 s
in DOSBox-X.
"""

import argparse
import datetime
import os
import re
import subprocess
import sys

import build

sys.path.insert(0, os.path.join(build.ROOT, 'tunnel'))
from mockchat import BENCH_REPLY  # noqa: E402  pylint: disable=wrong-import-position

TEST = os.path.join(build.ROOT, 'image', 'tests', 'bench.bat')
TIMEOUT = {'86box': 5400, 'dosbox': 600}       # seconds to wait for the run, by default
LINE = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) ((?:api|relaunch:|stack:) .*)$', re.M)
FIELD = re.compile(r'(\w+)=(\S+)')


def run_86box(timeout, no_build):
    """bench.bat in 86Box with the mock; returns test8088.py's output."""
    cmd = [sys.executable, os.path.join(build.ROOT, 'scripts', 'test8088.py'), TEST, '--mock',
           '--log', 'BENCH.LOG', '--done', 'BENCH-DONE', '--timeout', str(timeout),
           '--expect', 'bench: PASS', '--expect', 'CLAWDOS.EXE exit code 0']
    return run(cmd + (['--no-build'] if no_build else []))


def run_dosbox(timeout, no_build):
    """bench.bat in DOSBox-X with the mock, with the log copied to COM1 for boottest.py."""
    with open(TEST, 'rb') as f:
        batch = f.read()
    batch += b'TYPE C:\\BENCH.LOG > COM1\r\nECHO CLAWDOS-TEST-DONE > COM1\r\n'
    path = os.path.join(build.BUILD, 'benchdbx.bat')
    with open(path, 'wb') as f:
        f.write(batch)
    cmd = [sys.executable, os.path.join(build.ROOT, 'scripts', 'boottest.py'), path, '--mock',
           '--timeout', str(timeout), '--expect', 'bench: PASS', '--expect', 'CLAWDOS.EXE exit code 0']
    return run(cmd + (['--no-build'] if no_build else []))


def run(cmd):
    """Run a test script; its output, or exit with it shown if the test failed."""
    p = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace', check=False)
    if p.returncode != 0:
        sys.stdout.write(p.stdout + p.stderr)
        sys.exit('bench: the test run failed (output above)')
    return p.stdout


def commit():
    """The short hash of HEAD, with -dirty if the working tree has changes."""
    def git(*args):
        return subprocess.run(['git', *args], cwd=build.ROOT, capture_output=True, text=True,
                              check=False).stdout.strip()
    return git('rev-parse', '--short', 'HEAD') + ('-dirty' if git('status', '--porcelain', '-uno') else '')


def kbps(nbytes, ms):
    """KB/s as text, or '-' when the time is below the clock's resolution."""
    return f'{nbytes / 1024 / (ms / 1000):.1f}' if ms > 0 else '-'


def table(output, where):
    """The benchmark's report from the test output's CLAWDOS.LOG lines."""
    stamped = LINE.findall(output)
    lines = [x for _, x in stamped]
    rounds = []
    for when, x in stamped:
        if x.startswith('api '):
            r = dict(FIELD.findall(x))
            r['end'] = datetime.datetime.strptime(when, '%Y-%m-%d %H:%M:%S')
            rounds.append(r)
    if not rounds:
        sys.exit('bench: no "api" lines in the log')
    out = [f'{where}, {datetime.date.today().isoformat()}, commit {commit()}', '',
           '| round | gap s | len | up ms | up KB/s | ttft-up ms | total ms |',
           '|---:|---:|---:|---:|---:|---:|---:|']
    previous = None
    for r in rounds:
        n, up, ttft, total = int(r['len']), int(r['up']), int(r['ttft']), int(r['total'])
        gap = '-' if previous is None else f'{max(0.0, (r["end"] - previous).total_seconds() - total / 1000):.0f}'
        previous = r['end']
        out.append(f'| {r["round"]} | {gap} | {n} | {up} | {kbps(n, up)} | {ttft - up} | {total} |')
    sent = sum(int(r['len']) for r in rounds)
    up = sum(int(r['up']) for r in rounds)
    wall = (rounds[-1]['end'] - rounds[0]['end']).total_seconds() + int(rounds[0]['total']) / 1000
    out.append(f'| all | | {sent} | {up} | {kbps(sent, up)} | | {sum(int(r["total"]) for r in rounds)} |')
    out.append('')
    out.append(f'The turn took {wall:.0f} s from the first request to the last reply.')
    out.append('')
    last = rounds[-1]
    streaming = int(last['total']) - int(last['ttft'])
    rate = f'{len(BENCH_REPLY) / (streaming / 1000):.0f}' if streaming > 0 else '-'
    out.append(f'Final round: {len(BENCH_REPLY)} characters streamed in {streaming} ms '
               f'after the first, {rate} characters/s (stop={last["stop"]}).')
    for x in lines:
        if x.startswith('relaunch:'):
            out.append(f'Relaunch round trip: {x[len("relaunch: "):]}.')
    stacks = [x[len('stack: '):] for x in lines if x.startswith('stack:')]
    if stacks:
        out.append('Stack: ' + '; '.join(stacks) + ' (one line per HARNESS.EXE).')
    return '\n'.join(out)


def main():
    """Run the benchmark and print its table."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dosbox', action='store_true', help='DOSBox-X instead of 86Box (a quick check)')
    ap.add_argument('--timeout', type=int, help=f'seconds to wait for the run (default {TIMEOUT["86box"]} in 86Box, '
                                                f'{TIMEOUT["dosbox"]} in DOSBox-X)')
    ap.add_argument('--no-build', action='store_true', help='reuse build/ outputs')
    a = ap.parse_args()
    timeout = a.timeout if a.timeout is not None else TIMEOUT['dosbox' if a.dosbox else '86box']
    print(f'bench: running image/tests/bench.bat in {"DOSBox-X" if a.dosbox else "86Box (about six minutes)"}...',
          file=sys.stderr, flush=True)
    if a.dosbox:
        output = run_dosbox(timeout, a.no_build)
        where = 'DOSBox-X (80186, not comparable)'
    else:
        output = run_86box(timeout, a.no_build)
        where = '86Box IBM PC 5160 (8088 4.77 MHz, NE1000, SLiRP) against the mock'
    print(table(output, where))


if __name__ == '__main__':
    main()
