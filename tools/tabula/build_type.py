#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bake the one-bit type Tabula is set in, as tools/tabula/type.mtf.

EB Garamond for the versicle, the initial and the small capitals; Noto Sans for
the status line. Both are SIL OFL 1.1 (licences in tools/tabula/fonts). Only
the characters the bundled versicles use are kept. Needs Pillow and fontTools.

Layout, little-endian, offsets from the start of the file:
  "MTF1", u32 face_count, then per face
          u32 glyph_count, u32 glyphs_at, u32 bits_at, u16 line_height, u16 size
  glyphs: sorted by code, each u32 code, u32 offset into the face's bits,
          u8 width, u8 height, u8 advance, i8 x, i8 y, 3 reserved
  bits:   each glyph row after row without padding, first pixel in the high bit
"""
import argparse
import struct
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import ImageFont

import pack

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CAPITALS = "ABCDEFGHIJKLMNOPQRSTUVWXYZÆŒ"
ASCII = "".join(chr(c) for c in range(32, 127))
LATIN1 = "".join(chr(c) for c in range(0xC0, 0x100) if c not in (0xD7, 0xF7))
# Order is fixed: compose.py and tabula.cpp pick faces by position.
# (name, family, size, line height, characters)
TEXT_SIZES = ((34, 42), (29, 36), (25, 31))


def faces():
    text = set(ASCII) | {pack.VERSICLE, pack.RESPONSE, "…", "·"}
    for first, second in pack.verses().values():
        text |= set(first) | set(second)
    out = [(f"Text {size}", "serif", size, line, text) for size, line in TEXT_SIZES]
    # The initial stands in a box two lines tall, with room for its ground;
    # EB Garamond's capitals are 0.65 em.
    out += [(f"Initial {size}", "serif", round((2 * line - 40) / 0.65), 2 * line, set(CAPITALS)) for size, line in TEXT_SIZES]
    out.append(("Capitals", "serif", 21, 26, set(CAPITALS + "0123456789 ·.")))
    out.append(("Small", "sans", 15, 20, set(ASCII + LATIN1 + "·…")))
    return out


def bake(font, cmap, chars, tall=0):
    glyphs, bits = [], bytearray()
    for ch in sorted(chars):
        if ord(ch) not in cmap:
            raise SystemExit(f"no glyph for U+{ord(ch):04X} in {font.path}")
        mask, box = font.getmask2(ch, mode="1")
        w, h = mask.size
        if w > 255 or h > 255:
            raise SystemExit(f"U+{ord(ch):04X} is too large to store")
        if tall and h > tall:
            raise SystemExit(f"U+{ord(ch):04X} is too tall for the initial's box")
        packed = bytearray((w * h + 7) // 8)
        for y in range(h):
            for x in range(w):
                if mask.getpixel((x, y)):
                    bit = y * w + x
                    packed[bit // 8] |= 0x80 >> (bit % 8)
        glyphs.append(struct.pack("<IIBBBbb3x", ord(ch), len(bits), w, h, round(font.getlength(ch)), box[0], box[1]))
        bits += packed
    return glyphs, bytes(bits)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--serif", default=ROOT / ".references/fonts/EBGaramond.ttf")
    parser.add_argument("--sans", default=ROOT / ".references/fonts/NotoSans.ttf")
    parser.add_argument("--out", default=HERE / "type.mtf")
    args = parser.parse_args()
    paths = {"serif": str(args.serif), "sans": str(args.sans)}
    cmaps = {family: TTFont(path, lazy=True).getBestCmap() for family, path in paths.items()}
    listed = faces()
    at = 8 + 16 * len(listed)
    heads, body = b"", b""
    for name, family, size, line, chars in listed:
        # An initial must fit inside its ruled box: see draw_initial in compose.py.
        glyphs, bits = bake(ImageFont.truetype(paths[family], size), cmaps[family], chars,
                            line - 20 if name.startswith("Initial") else 0)
        table = b"".join(glyphs)
        heads += struct.pack("<IIIHH", len(glyphs), at + len(body), at + len(body) + len(table), line, size)
        body += table + bits
        print(f"{name}: {len(glyphs)} glyphs, {len(bits)} bytes")
    Path(args.out).write_bytes(b"MTF1" + struct.pack("<I", len(listed)) + heads + body)
    print(f"type: {8 + len(heads) + len(body)} bytes")


if __name__ == "__main__":
    main()
