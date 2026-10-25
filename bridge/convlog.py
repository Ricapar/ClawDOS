"""Conversation log: one JSONL file per XT connection, for analysis later.

  bridge/logs/2026-10-25/091123-172.16.0.229-c1.jsonl

Each line is one event: {"t": ISO time, "type": ..., fields}. The types are
session_start, user, assistant, api_call, tool_call, tool_result, turn_end,
model_switch, new_chat and session_end (xtbridge.Session writes them).
Lines are flushed as they are written, so a crash loses nothing. Logging must never break a chat: a write that fails is
reported once and the log goes quiet.
"""

import json
import logging
import os
from datetime import datetime

log = logging.getLogger('xtbridge')

USAGE_KEYS = ('input', 'output', 'cache_read', 'cache_write')


def no_usage():
    return dict.fromkeys(USAGE_KEYS, 0)


def add_usage(total, more):
    for k in USAGE_KEYS:
        total[k] += more.get(k, 0) or 0


class ConvLog:
    def __init__(self, path):
        self.path = path
        self.file = None
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            self.file = open(path, 'a', encoding='utf-8')
        except OSError as e:
            log.warning('Conversation log off: %s', e)

    def event(self, type, **fields):
        if not self.file:
            return
        record = {'t': datetime.now().isoformat(timespec='milliseconds'), 'type': type, **fields}
        try:
            self.file.write(json.dumps(record, ensure_ascii=False) + '\n')
            self.file.flush()
        except (OSError, ValueError) as e:
            log.warning('Conversation log off: %s', e)
            self.close()

    def close(self):
        if self.file:
            try:
                self.file.close()
            except OSError:
                pass
            self.file = None


class NoLog:
    """Stands in when --convlog is '-'."""
    path = None

    def event(self, type, **fields):
        pass

    def close(self):
        pass


def open_log(base, peer, number):
    """The log for connection `number` from `peer` (host, port), or a NoLog."""
    if not base or base == '-':
        return NoLog()
    now = datetime.now()
    name = f'{now:%H%M%S}-{peer[0] if peer else "unknown"}-c{number}.jsonl'
    return ConvLog(os.path.join(base, f'{now:%Y-%m-%d}', name))
