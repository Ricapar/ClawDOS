"""System prompt files: bridge/prompts/<name>.txt, chosen with --prompt NAME.

A prompt file steers the conversation (tone, length, role). It goes on top of
the built-in prompt (providers.SYSTEM_PROMPT and tools.TOOLS_PROMPT), which
keeps what the XT needs: plain text for an 80-column CP437 screen, and how the
tools work. {name} in a file becomes the assistant's name. Files are read each
time a conversation starts, so edits apply without restarting the bridge.
"""

import os

PROMPT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'prompts')
DEFAULT = 'default'


class PromptError(Exception):
    pass


def names(folder=None):
    folder = folder or PROMPT_DIR
    try:
        return sorted(f[:-4] for f in os.listdir(folder) if f.lower().endswith('.txt'))
    except OSError:
        return []


def load(name, folder=None):
    folder = folder or PROMPT_DIR
    try:
        with open(os.path.join(folder, name + '.txt'), encoding='utf-8') as f:
            return f.read().strip()
    except OSError:
        have = ', '.join(names(folder)) or 'none'
        raise PromptError(f'no prompt "{name}" in {folder} (have: {have})')
