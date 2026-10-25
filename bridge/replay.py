"""Replay a recorded session from the bridge's debug log (xtbridge.log).

  python bridge/xtbridge.py --replay bridge/xtbridge.log [--connection N] [--speed 1]

Any message from the XT then plays back the whole recorded conversation: each
user message as a line of its own, then the reply exactly as it was sent
(TXT/NL lines and tool calls, which the XT really runs) with the original
pauses between them. That reproduces on demand whatever the XT did with that
traffic, without an API key. --speed scales the pauses (0 = none).
"""

import asyncio
import re
from datetime import datetime

from mockchat import report_round
from tools import ToolCall, unescape

LINE = re.compile(r'(\S+ \S+) \w (\(.*?\)) (>>|>|<) ?(.*)$')


def parse_log(path, connection=None):
    """Turns of one connection (the last one with a message, or the Nth to
    connect): a list of (user text, [(delay seconds, str | ToolCall), ...])."""
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    peers = []                                   # in order of connection
    for line in lines:
        m = re.search(r' (\(.*?\)) connected \(connection #(\d+)', line)
        if m:
            peers.append(m[1])
    if not peers:
        raise ValueError(f'{path}: no connections logged')
    if connection is None:
        with_msgs = [p for p in peers if any(f'{p} >> ' in l for l in lines)]
        if not with_msgs:
            raise ValueError(f'{path}: no messages logged')
        peer = with_msgs[-1]
    else:
        peer = peers[connection - 1]

    turns, items, last, call = [], None, None, None
    for line in lines:
        m = LINE.match(line)
        if not m or m[2] != peer:
            continue
        when = datetime.strptime(m[1], '%Y-%m-%d %H:%M:%S.%f')
        direction, rest = m[3], m[4]
        if direction == '>>':
            items = []
            turns.append((rest, items))
            last = when
            continue
        if items is None:
            continue
        verb, _, payload = rest.partition(' ')
        if direction == '<':
            if verb == 'DONE':                   # the pause after a tool counts from here
                last = when
            continue
        delay = max(0.0, (when - last).total_seconds())
        if verb == 'TXT':
            items.append((delay, payload))
        elif verb == 'NL':
            items.append((delay, '\n'))
        elif verb == 'TOOL':
            call = (delay, payload.partition(' ')[2], {})
            continue
        elif verb == 'ARG' and call:
            key, _, piece = payload.partition(' ')
            call[2][key] = call[2].get(key, '') + piece
            continue
        elif verb == 'CALL' and call:
            items.append((call[0], ToolCall(call[1], {k: unescape(v) for k, v in call[2].items()})))
            call = None
        else:
            continue
        last = when
    return turns


class ReplayChat:
    """Plays back parsed turns in place of a model."""

    def __init__(self, turns, speed=1.0):
        self.turns = turns
        self.speed = speed
        self.history = []

    async def reply(self, text):
        round = 0
        for n, (user, items) in enumerate(self.turns, 1):
            yield f'[replay {n}/{len(self.turns)}] > {user}\n'
            for delay, item in items:
                if self.speed > 0 and delay > 0:
                    await asyncio.sleep(delay / self.speed)
                if isinstance(item, ToolCall):
                    item = ToolCall(item.name, dict(item.args))   # fresh for each replay
                    report_round(self, round, 'tool_use')
                    round += 1
                yield item
            yield '\n'
        yield '[replay finished]'
        report_round(self, round, 'end_turn')
