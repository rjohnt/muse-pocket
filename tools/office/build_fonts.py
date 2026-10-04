#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bake 1-bit glyph subsets for the Pocket e-paper UI.

Reads Noto Sans and Gentium Book Plus (both SIL OFL 1.1; licences are kept in
esp32/main/office/fonts) and writes esp32/main/office/font_data.cpp. Only the
characters used by the UI and the bundled prayer pack are included.
"""
import argparse
import json
import unicodedata
from pathlib import Path

from PIL import ImageFont

ROOT = Path(__file__).resolve().parents[2]
UI_EXTRA = "·←→×…‹›—–’‘“”°•✓"
# Order must match font() in raster.cpp: the reading faces at each text size
# (small, medium, large), then the fixed interface faces.
FACES = [
    ("Latin small", "serif", 25, 33),
    ("English small", "sans", 16, 22),
    ("Latin medium", "serif", 29, 38),
    ("English medium", "sans", 18, 25),
    ("Latin large", "serif", 34, 44),
    ("English large", "sans", 21, 29),
    ("UI", "sans", 18, 25),
    ("Title", "serif", 38, 48),
    ("Small", "sans", 15, 20),
]
SKIP = {0x00AD, 0xFE0E, 0xFE0F, 0x0097}
CROSSES = "\u2719\u2720"  # liturgical crosses; neither font has them


def charset(pack):
    chars = {chr(c) for c in range(32, 127)} | set(UI_EXTRA)
    data = json.loads(Path(pack).read_text())
    for block in data["texts"].values():
        for key in ("latin", "english"):
            chars |= set(unicodedata.normalize("NFC", block[key]))
    for rites in data["days"].values():
        for offices in rites.values():
            for office in offices.values():
                chars |= set(unicodedata.normalize("NFC", office["title"]))
    return sorted(c for c in chars if ord(c) >= 32 and ord(c) not in SKIP
                  and not unicodedata.combining(c))


def has(font, ch):
    return font.getmask(ch, mode="1").getbbox() is not None or ch == " "


def bake(primary, fallback, chars):
    glyphs, bits, missing = [], bytearray(), []
    for ch in chars:
        font = primary
        if ch in CROSSES:
            glyphs.append((ord(ch), len(bits)) + cross(primary, bits))
            continue
        if not _covered(primary, ch):
            if _covered(fallback, ch):
                font = fallback
            else:
                missing.append(ch)
                continue
        # getmask2 reports where this exact 1-bit rendering sits; the bounding
        # box of the anti-aliased glyph can differ by a pixel once hinted.
        mask, box = font.getmask2(ch, mode="1")
        box = (box[0], box[1] + settle(font, ch, mask, box))
        w, h = mask.size
        advance = round(font.getlength(ch))
        offset = len(bits)
        row = bytearray((w * h + 7) // 8)
        for y in range(h):
            for x in range(w):
                if mask.getpixel((x, y)):
                    bit = y * w + x
                    row[bit // 8] |= 0x80 >> (bit % 8)
        bits += row
        glyphs.append((ord(ch), offset, w, h, advance, box[0], box[1]))
    return glyphs, bits, missing


def ink_bottom(mask):
    box = mask.getbbox()
    return box[3] if box else 0


def settle(font, ch, mask, box):
    """Rows to move an accented letter so it rests where its plain letter does.

    Hinting at 1 bit can lift a letter with a mark above it by a pixel, which
    makes a word look uneven. Marks below the letter are left alone.
    """
    parts = unicodedata.normalize("NFD", ch)
    if len(parts) < 2 or any(unicodedata.combining(c) != 230 for c in parts[1:]):
        return 0
    base = parts[0]
    if not _covered(font, base):
        return 0
    plain, plain_box = font.getmask2(base, mode="1")
    return (plain_box[1] + ink_bottom(plain)) - (box[1] + ink_bottom(mask))


def cross(font, bits):
    """Cross with capped arms, sized to the capital height."""
    top, bottom = font.getbbox("H")[1], font.getbbox("H")[3]
    size = (bottom - top) | 1
    bar, cap, depth = max(1, size // 7) | 1, max(3, size // 2) | 1, max(1, size // 8)
    mid = size // 2
    row = bytearray((size * size + 7) // 8)
    for y in range(size):
        for x in range(size):
            arm = abs(x - mid) <= bar // 2 or abs(y - mid) <= bar // 2
            ends = ((y < depth or y >= size - depth) and abs(x - mid) <= cap // 2) or \
                   ((x < depth or x >= size - depth) and abs(y - mid) <= cap // 2)
            if arm or ends:
                bit = y * size + x
                row[bit // 8] |= 0x80 >> (bit % 8)
    bits += row
    return size, size, size + 4, 2, top


def _covered(font, ch):
    # FreeType draws .notdef for unmapped codepoints, so ask the cmap.
    cmap = font.font  # noqa: F841 (kept for clarity)
    from fontTools.ttLib import TTFont  # lazy: optional dependency
    path = font.path
    table = _CMAPS.setdefault(path, TTFont(path, lazy=True).getBestCmap())
    return ord(ch) in table


_CMAPS = {}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sans", default=ROOT / ".references/fonts/NotoSans.ttf")
    parser.add_argument("--serif", default=ROOT / ".references/fonts/GentiumBookPlus-Regular.ttf")
    parser.add_argument("--pack", default=ROOT / "tools/office/office-pack.json")
    parser.add_argument("--out", default=ROOT / "esp32/main/office/font_data.cpp")
    args = parser.parse_args()
    chars = charset(args.pack)
    paths = {"sans": str(args.sans), "serif": str(args.serif)}
    out = ["// Generated by tools/office/build_fonts.py. Do not edit.",
           "// Glyph subsets of Noto Sans and Gentium Book Plus, SIL OFL 1.1; see fonts/.",
           '#include "font_data.h"', "namespace pocket_office {", "namespace {"]
    table = []
    for index, (name, family, size, line) in enumerate(FACES):
        primary = ImageFont.truetype(paths[family], size)
        fallback = ImageFont.truetype(paths["serif" if family == "sans" else "sans"], size)
        glyphs, bits, missing = bake(primary, fallback, chars)
        if missing:
            print(f"{name}: no glyph for {' '.join(f'U+{ord(c):04X}' for c in missing)}")
        out.append(f"const Glyph glyphs_{index}[]={{")
        out += [f"{{{c},{o},{w},{h},{a},{x},{y}}}," for c, o, w, h, a, x, y in glyphs]
        out.append("};")
        out.append(f"const uint8_t bits_{index}[]={{")
        for i in range(0, len(bits), 24):
            out.append(",".join(str(b) for b in bits[i:i + 24]) + ",")
        out.append("};")
        table.append(f"{{glyphs_{index},{len(glyphs)},bits_{index},{line}}}")
        print(f"{name}: {len(glyphs)} glyphs, {len(bits)} bytes")
    out += ["}", "const Font fonts[]={" + ",".join(table) + "};", "}", ""]
    Path(args.out).write_text("\n".join(out))


if __name__ == "__main__":
    main()
