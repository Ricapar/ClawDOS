"""Replay a recorded session from the bridge's debug log (dosbridge.log).

  python bridge/dosbridge.py --replay bridge/dosbridge.log [--connection N] [--speed 1]

Any message from the DOS PC then plays back the whole recorded conversation: each
user message as a line of its own, then the reply exactly as it was sent
(TXT/NL lines and tool calls, which the DOS PC really runs) with the original
pauses between them. That reproduces on demand whatever the DOS PC did with that
traffic, without an API key. --speed scales the pauses (0 = none).
"""

import asyncio
import re
from datetime import datetime

from mockchat import report_round
from tools import ToolCall, unescape

LINE = re.compile(r'(\S+ \S+) \w (\(.*?\)) (>>|>|<) ?(.*)$')


def pick_peer(path, lines, connection):
    """The peer (as logged, "(host, port)") whose connection to replay."""
    peers = []                                   # in order of connection
    for line in lines:
        m = re.search(r' (\(.*?\)) connected \(connection #(\d+)', line)
        if m:
            peers.append(m[1])
    if not peers:
        raise ValueError(f'{path}: no connections logged')
    if connection is not None:
        return peers[connection - 1]
    with_msgs = [p for p in peers if any(f'{p} >> ' in line for line in lines)]
    if not with_msgs:
        raise ValueError(f'{path}: no messages logged')
    return with_msgs[-1]


class TurnParser:
    """Builds the turns from one peer's log lines, fed in order."""

    def __init__(self):
        self.turns = []
        self.items = None            # the current turn's items; None before the first message
        self.last = None             # when the previous item (or the user's message) happened
        self.call = None             # (delay, name, args so far) of a tool call being sent

    def parse(self, lines, peer):
        """The turns of `peer` (as logged) in the log `lines`."""
        for line in lines:
            m = LINE.match(line)
            if m and m[2] == peer:
                self.feed(datetime.strptime(m[1], '%Y-%m-%d %H:%M:%S.%f'), m[3], m[4])
        return self.turns

    def feed(self, when, direction, rest):
        """Take one log line: the time, '>>' (user), '>' (to the DOS PC) or '<' (from it), and the text."""
        if direction == '>>':
            self.items = []
            self.turns.append((rest, self.items))
            self.last = when
            return
        if self.items is None:
            return
        verb, _, payload = rest.partition(' ')
        if direction == '<':
            if verb == 'DONE':                   # the pause after a tool counts from here
                self.last = when
            return
        delay = max(0.0, (when - self.last).total_seconds())
        if verb == 'TXT':
            self.items.append((delay, payload))
        elif verb == 'NL':
            self.items.append((delay, '\n'))
        elif verb == 'TOOL':
            self.call = (delay, payload.partition(' ')[2], {})
            return
        elif verb == 'ARG' and self.call:
            key, _, piece = payload.partition(' ')
            args = self.call[2]
            args[key] = args.get(key, '') + piece
            return
        elif verb == 'CALL' and self.call:
            call_delay, name, args = self.call
            self.items.append((call_delay, ToolCall(name, {k: unescape(v) for k, v in args.items()})))
            self.call = None
        else:
            return
        self.last = when


def parse_log(path, connection=None):
    """Turns of one connection (the last one with a message, or the Nth to
    connect): a list of (user text, [(delay seconds, str | ToolCall), ...])."""
    with open(path, encoding='utf-8', errors='replace') as f:
        lines = f.read().splitlines()
    return TurnParser().parse(lines, pick_peer(path, lines, connection))


class ReplayChat:   # pylint: disable=too-few-public-methods  # a chat backend is just reply() plus state
    """Plays back parsed turns in place of a model."""

    def __init__(self, turns, speed=1.0):
        self.turns = turns
        self.speed = speed
        self.history = []

    async def reply(self, _text):
        """Play back every recorded turn, whatever the message was."""
        rounds = 0
        for n, (user, items) in enumerate(self.turns, 1):
            yield f'[replay {n}/{len(self.turns)}] > {user}\n'
            for delay, item in items:
                if self.speed > 0 and delay > 0:
                    await asyncio.sleep(delay / self.speed)
                if isinstance(item, ToolCall):
                    item = ToolCall(item.name, dict(item.args))   # fresh for each replay
                    report_round(self, rounds, 'tool_use')
                    rounds += 1
                yield item
            yield '\n'
        yield '[replay finished]'
        report_round(self, rounds, 'end_turn')
