"""Unicode <-> code page 437 for the XT's screen."""

import unicodedata

# Characters CP437 lacks, mapped to close equivalents.
CP437_FALLBACKS = {
    '‘': "'", '’': "'", '‚': "'", '‛': "'",
    '“': '"', '”': '"', '„': '"', '″': '"', '′': "'",
    '–': '-', '—': '--', '―': '--', '−': '-', '‐': '-', '‑': '-',
    '…': '...',
    '\u200b': '', '\u200d': '', '\ufe0f': '',
    '→': '->', '←': '<-', '⇒': '=>', '↔': '<->',
    '•': '∙', '◦': 'o', '‣': '>', '×': 'x',
    '≤': '<=', '≥': '>=', '≠': '!=', '≈': '~',
    '✓': 'v', '✔': 'v', '✗': 'x', '✘': 'x',
    '©': '(c)', '®': '(R)', '™': '(TM)', '€': 'EUR',
    '\t': '    ',
}


def to_cp437(text):
    """Unicode -> CP437 bytes, approximating anything CP437 can't show."""
    out = bytearray()
    for ch in text:
        try:
            out += ch.encode('cp437')
            continue
        except UnicodeEncodeError:
            pass
        sub = CP437_FALLBACKS.get(ch)
        if sub is None:
            base = unicodedata.normalize('NFKD', ch)
            base = ''.join(c for c in base if not unicodedata.combining(c))
            sub = base if base and base != ch else '?'
        out += sub.encode('cp437', errors='replace')
    return bytes(out)


def from_cp437(data):
    """CP437 bytes -> Unicode."""
    return data.decode('cp437')
