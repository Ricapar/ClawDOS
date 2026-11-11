"""The host-side reference for the screenshot PNGs (lib/shotpng.c).

  python3 tests/unit/pngref.py OUTDIR      writes OUTDIR/ref320.png and OUTDIR/ref640.png

DOSUNIT.EXE encodes the same frames on the PC and compares its files with
these byte for byte (the build puts them in C:\\TESTS). The encoding is
written out here from the PNG and zlib specifications, with Python's zlib only
for the CRC-32 and Adler-32, so the PC's checksums are checked against an
independent implementation. Stdlib only.
"""

import struct
import sys
import os
import zlib

ROWS = 200
ROW_BYTES = 80
BANK2 = 0x2000

# The frames and colours dosunit.c's testShotPng uses.
REF320_PALETTE = [0x0000AA, 0x55FF55, 0xFF5555, 0xFFFF55]   # colour register 11h: blue, palette 0 bright
REF640_PALETTE = [0x000000, 0xFFFF55]                        # shot_color 0Eh: yellow on black


def rgbi(c):
    """The RGB value of CGA colour c (0-15) on an RGBI monitor, with colour 6 as brown."""
    lo = 0x55 if c & 8 else 0
    r = (0xAA if c & 4 else 0) + lo
    g = 0x55 if c == 6 else (0xAA if c & 2 else 0) + lo
    b = (0xAA if c & 1 else 0) + lo
    return r << 16 | g << 8 | b


def unit_frame():
    """dosunit.c's test frame: byte o of video memory is (o * 13 + o / 128) mod 256."""
    return bytes((o * 13 + (o >> 7)) & 0xFF for o in range(16384))


def quads_frame():
    """The video memory QUADS.COM (tests/unit/quads.asm) leaves: quadrants 0 1 / 2 3, a diagonal XOR 3."""
    mem = bytearray(16384)
    for y in range(ROWS):
        at = (BANK2 if y & 1 else 0) + (y >> 1) * ROW_BYTES
        left, right = (0x00, 0x55) if y < 100 else (0xAA, 0xFF)
        mem[at:at + 40] = bytes([left]) * 40
        mem[at + 40:at + 80] = bytes([right]) * 40
        mem[at + (y >> 2)] ^= 3 << ((3 - (y & 3)) * 2)
    return bytes(mem)


def rows_of(frame):
    """The 200 rows of 80 bytes, de-interlaced from the two banks."""
    return [frame[(BANK2 if y & 1 else 0) + (y >> 1) * ROW_BYTES:][:ROW_BYTES] for y in range(ROWS)]


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def png_cga(frame, bpp, palette):
    """The PNG the PC writes for this frame: indexed, bpp bits per pixel, one stored deflate block."""
    raw = b''.join(b'\0' + r for r in rows_of(frame))
    stored = b'\x01' + struct.pack('<HH', len(raw), len(raw) ^ 0xFFFF) + raw
    z = b'\x78\x01' + stored + struct.pack('>I', zlib.adler32(raw))
    ihdr = struct.pack('>IIBBBBB', 640 // bpp, ROWS, bpp, 3, 0, 0, 0)
    plte = b''.join(struct.pack('>I', c)[1:] for c in palette)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'PLTE', plte) + chunk(b'IDAT', z)
            + chunk(b'IEND', b''))


def main(argv):
    out = argv[1] if len(argv) > 1 else '.'
    frame = unit_frame()
    for name, bpp, palette in (('ref320.png', 2, REF320_PALETTE), ('ref640.png', 1, REF640_PALETTE)):
        with open(os.path.join(out, name), 'wb') as f:
            f.write(png_cga(frame, bpp, palette))


if __name__ == '__main__':
    main(sys.argv)
