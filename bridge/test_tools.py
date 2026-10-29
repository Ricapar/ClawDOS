#!/usr/bin/env python3
"""Tests of the tool schemas and the TOOL/ARG line encoding.

  python bridge/test_tools.py
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# pylint: disable=wrong-import-position  # sys.path must be set before these imports
from cp437 import to_cp437  # noqa: E402
from tools import (MAX_ROUNDS, TOOLS, TOOLS_PROMPT, ToolCall, ToolResult,  # noqa: E402
                   anthropic_tools, call_lines, escape, openai_tools, stringify,
                   summary, unescape)
# pylint: enable=wrong-import-position


class Encoding(unittest.TestCase):
    """Escaping and chunking of tool arguments."""
    def test_escape_roundtrip(self):
        """Escaping and unescaping are inverses."""
        for s in ('a\\b', 'x\ny', '\\n literal', '', 'C:\\DOS\\', '\n\n'):
            self.assertEqual(unescape(escape(s)), s)
        self.assertEqual(escape('a\nb\\'), 'a\\nb\\\\')

    def test_call_lines_chunks(self):
        """Long arguments are split into ARG lines of bounded size."""
        script = '\n'.join(f'LINE ({i},0)-({i},199),{i % 4}' for i in range(150))
        self.assertGreater(len(script), 3000)
        lines = call_lines('t1', ToolCall('draw', {'script': script}))
        self.assertEqual(lines[0], ('TOOL', 't1 draw'))
        self.assertEqual(lines[-1], ('CALL', ''))
        args = lines[1:-1]
        self.assertGreater(len(args), 1)
        joined = ''
        for verb, payload in args:
            self.assertEqual(verb, 'ARG')
            self.assertLessEqual(len(to_cp437(payload)), 500)
            key, _, chunk = payload.partition(' ')
            self.assertEqual(key, 'script')
            joined += chunk
        self.assertEqual(unescape(joined), script)

    def test_chunks_never_split_an_escape(self):
        """A chunk never ends in half an escape sequence."""
        text = '\\' * 1200
        lines = call_lines('t2', ToolCall('write', {'path': 'C:\\X', 'text': text}))
        for _, payload in lines[1:-1]:
            chunk = payload.partition(' ')[2]
            # an odd run of trailing backslashes would be half an escape
            run = len(chunk) - len(chunk.rstrip('\\'))
            self.assertEqual(run % 2, 0)
        self.assertEqual(unescape(''.join(p.partition(' ')[2] for _, p in lines[2:-1])), text)

    def test_stringify(self):
        """Argument values become strings; None is dropped."""
        self.assertEqual(stringify({'lines': 40, 'append': True, 'x': None, 'y': False}),
                         {'lines': '40', 'append': '1', 'y': '0'})


class Schemas(unittest.TestCase):
    """The tool table and its API renderings."""
    def test_schemas(self):
        """Names, classes and required parameters are consistent."""
        names = [t['name'] for t in TOOLS]
        self.assertEqual(len(names), 19)
        self.assertEqual(len(set(names)), 19)
        cls = {t['name']: t['cls'] for t in TOOLS}
        self.assertEqual({n for n, c in cls.items() if c == 'R'},
                         {'sysinfo', 'dir', 'read', 'search', 'peek'})
        self.assertEqual({n for n, c in cls.items() if c == 'H'}, {'draw', 'play', 'textart'})
        self.assertEqual({n for n, c in cls.items() if c == 'W'},
                         {'write', 'edit', 'delete', 'mkdir', 'rename', 'run', 'poke', 'port'})
        self.assertEqual({n for n, c in cls.items() if c == 'M'},
                         {'memory_list', 'memory_read', 'memory_write'})
        for t, a in zip(TOOLS, anthropic_tools()):
            schema = a['input_schema']
            required = [k for k, (_, req) in t['params'].items() if req]
            self.assertEqual(sorted(schema.get('required', [])), sorted(required))
            for prop in schema['properties'].values():
                self.assertEqual(prop['type'], 'string')
        o = openai_tools()
        self.assertEqual(len(o), 19)
        self.assertEqual(o[0]['type'], 'function')
        self.assertEqual(o[0]['function']['name'], TOOLS[0]['name'])

    def test_model_text_and_summary(self):
        """Result text for the model, and the one-line summaries."""
        self.assertEqual(ToolResult('ok', 'hi').model_text(), 'hi')
        self.assertEqual(ToolResult('error', 'nope').model_text(), 'Error: nope')
        self.assertEqual(ToolResult('denied', '').model_text(), 'The user denied this tool call.')
        self.assertEqual(ToolResult('stopped', '').model_text(), 'Interrupted by the user.')
        call = ToolCall('read', {'path': 'C:\\A.BAT'}, result=ToolResult('ok', '..'))
        self.assertEqual(summary(call), '[read C:\\A.BAT -> ok]')
        self.assertEqual(summary(ToolCall('sysinfo', {}, result=ToolResult('ok', ''))), '[sysinfo -> ok]')
        self.assertEqual(summary(ToolCall('run', {'command': 'VER'})), '[run VER]')
        self.assertEqual(summary(ToolCall('run', {'command': 'TCC -c A.C\nTLINK @A.LNK'})),
                         '[run TCC -c A.C; TLINK @A.LNK]')

    def test_prompt_and_cap(self):
        """The tools prompt mentions the key facts; the round cap."""
        self.assertEqual(MAX_ROUNDS, 25)
        for phrase in ('real IBM PC XT', 'GW-BASIC', 'may be denied'):
            self.assertIn(phrase, TOOLS_PROMPT)


if __name__ == '__main__':
    unittest.main()
