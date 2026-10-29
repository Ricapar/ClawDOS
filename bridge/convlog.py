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
    """A token-usage record with every counter at zero."""
    return dict.fromkeys(USAGE_KEYS, 0)


def add_usage(total, more):
    """Add the counters of usage record `more` into `total`."""
    for k in USAGE_KEYS:
        total[k] += more.get(k, 0) or 0


class ConvLog:
    """An open JSONL conversation log; goes quiet for good after a write error."""

    def __init__(self, path):
        self.path = path
        self.file = None
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            # Held open for the connection's life and closed by close(), so no `with`.
            self.file = open(path, 'a', encoding='utf-8')  # pylint: disable=consider-using-with
        except OSError as e:
            log.warning('Conversation log off: %s', e)

    def event(self, kind, **fields):
        """Append one event of type `kind` with the given fields."""
        if not self.file:
            return
        record = {'t': datetime.now().isoformat(timespec='milliseconds'), 'type': kind, **fields}
        try:
            self.file.write(json.dumps(record, ensure_ascii=False) + '\n')
            self.file.flush()
        except (OSError, ValueError) as e:
            log.warning('Conversation log off: %s', e)
            self.close()

    def close(self):
        """Close the log file; later events are dropped."""
        if self.file:
            try:
                self.file.close()
            except OSError:
                pass
            self.file = None


class NoLog:
    """Stands in when --convlog is '-'."""
    path = None

    def event(self, kind, **fields):
        """Drop the event."""

    def close(self):
        """Nothing to close."""


def open_log(base, peer, number):
    """The log for connection `number` from `peer` (host, port), or a NoLog."""
    if not base or base == '-':
        return NoLog()
    now = datetime.now()
    name = f'{now:%H%M%S}-{peer[0] if peer else "unknown"}-c{number}.jsonl'
    return ConvLog(os.path.join(base, f'{now:%Y-%m-%d}', name))
