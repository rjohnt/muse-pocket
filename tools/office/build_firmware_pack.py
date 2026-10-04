#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pack office-pack.json into the binary the firmware embeds.

Layout, little-endian:
  "MPO1", u32 text_count, u32 office_count, u32 texts_at, u32 offices_at
  texts_at:   u32 offset[text_count], each -> u8 kind, u16 latin_len,
              u16 english_len, latin bytes, english bytes (UTF-8, NFC)
              kind: 0 heading, 1 prayer (last phrase), 2 note,
              3 phrase with more of the same prayer following
  offices_at: office_count records sorted by (date, rite, hour):
              u32 yyyymmdd, u8 rite, u8 hour, u16 title_len, u32 title_at,
              u32 block_count, u32 blocks_at (u16 text indices)
              Common prayers from prayers.json are stored the same way, with
              date 0, rite 2 and their position in the list as the hour.
Offsets are from the start of the file.
"""
import argparse
import json
import re
import struct
import unicodedata
from pathlib import Path

HOURS = ("Matins", "Lauds", "Prime", "Terce", "Sext", "None", "Vespers", "Compline")
RITES = ("monastic", "modern")
KINDS = {"heading": 0, "prayer": 1}
SKIP = dict.fromkeys((0x00AD, 0xFE0E, 0xFE0F))


def clean(value):
    # U+0097 is a stray Windows-1252 em dash in the source text.
    value = unicodedata.normalize("NFC", value.replace("\u0097", "\u2014")).translate(SKIP)
    return "".join(c for c in value if not unicodedata.combining(c)).encode("utf-8")


# Prayers are cut into short pieces so each Latin line sits above its own
# translation. The two languages rarely punctuate alike, so the pieces are
# found in three steps:
#   1. Lines and the psalm pause marks divide a prayer wherever Latin and
#      English have the same number of them. These are certain.
#   2. Inside each division both sides are cut at every punctuation mark and
#      the two runs of clauses are matched in order, by length, letting one
#      clause answer to several where the translation runs them together.
#   3. Neighbouring pieces are joined again while the Latin still fits a line.
CERTAIN = (r"\n", r"(?<=[*\u2020\u2021])\s+")
CLAUSE = r"(?<=[,.;:?!])\s+|\n"
STRONG = ".?!"
LINE = 32  # Latin letters and spaces that fit one line at the medium size
SPAN = 4   # most clauses one side may gather against the other


def letters(text):
    return sum(ch.isalpha() for ch in text) or 1


def clauses(text):
    """Cut at punctuation. Versicle signs, short labels such as "Ant." and
    verse numbers stay with what follows; a lone pause mark stays with what
    came before."""
    out, lead = [], False
    for part in (piece.strip() for piece in re.split(CLAUSE, text)):
        if not part:
            continue
        count = sum(ch.isalpha() for ch in part)
        if out and (lead or count == 0):
            out[-1] += " " + part
        else:
            out.append(part)
        lead = " " not in out[-1] and 0 < sum(ch.isalpha() for ch in out[-1]) <= 3 or \
            (count == 0 and len(out) == 1 and out[-1] == part)
    return out


def match(latin, english):
    """Pair two runs of clauses in order. Returns a list of (latin, english)."""
    if len(latin) < 2 or len(english) < 2:
        return [(" ".join(latin), " ".join(english))]
    ratio = sum(map(letters, english)) / sum(map(letters, latin))
    a = [0]
    for part in latin:
        a.append(a[-1] + letters(part))
    b = [0]
    for part in english:
        b.append(b[-1] + letters(part))
    best = {(0, 0): (0.0, None)}
    for i in range(len(latin) + 1):
        for j in range(len(english) + 1):
            if (i, j) not in best:
                continue
            for di in range(1, SPAN + 1):
                for dj in range(1, SPAN + 1):
                    if i + di > len(latin) or j + dj > len(english) or (di > 1 and dj > 1 and di + dj > 4):
                        continue
                    la, en = a[i + di] - a[i], b[j + dj] - b[j]
                    cost = (en - la * ratio) ** 2 / (la * ratio + en) + 2.0 * (di + dj - 2)
                    # A full stop on one side should meet one on the other.
                    ends = (latin[i + di - 1][-1] in STRONG, english[j + dj - 1][-1] in STRONG)
                    cost += 1.5 if ends[0] != ends[1] else 0.0
                    total = best[(i, j)][0] + cost
                    key = (i + di, j + dj)
                    if key not in best or total < best[key][0]:
                        best[key] = (total, (i, j))
    key, pairs = (len(latin), len(english)), []
    if key not in best:
        return [(" ".join(latin), " ".join(english))]
    while best[key][1] is not None:
        prev = best[key][1]
        pairs.append((" ".join(latin[prev[0]:key[0]]), " ".join(english[prev[1]:key[1]])))
        key = prev
    return pairs[::-1]


def join_short(pairs):
    out = []
    for la, en in pairs:
        if out and len(out[-1][0]) + 1 + len(la) <= LINE and out[-1][0][-1] not in STRONG:
            out[-1] = (out[-1][0] + " " + la, out[-1][1] + " " + en)
        else:
            out.append((la, en))
    return out


def phrases(latin, english):
    parts = [(latin, english)]
    for pattern in CERTAIN:
        finer = []
        for la, en in parts:
            x = [part.strip() for part in re.split(pattern, la) if part.strip()]
            y = [part.strip() for part in re.split(pattern, en) if part.strip()]
            finer += zip(x, y) if len(x) == len(y) > 1 else [(la, en)]
        parts = finer
    out = []
    for la, en in parts:
        out += join_short(match(clauses(la), clauses(en)))
    return out


def build(source, prayers=None):
    data = json.loads(Path(source).read_text())
    if data.get("schema") != 2:
        raise SystemExit("Unsupported office pack")
    entries, index_of, expanded = [], {}, {}

    def entry(kind, latin, english):
        key = (kind, latin, english)
        if key not in index_of:
            index_of[key] = len(entries)
            entries.append(key)
        return index_of[key]

    for ident, block in data["texts"].items():
        kind = KINDS.get(block["kind"], 2)
        if kind != 1 or not block["latin"].strip() or not block["english"].strip():
            expanded[ident] = [entry(kind, clean(block["latin"]), clean(block["english"]))]
            continue
        parts = phrases(block["latin"], block["english"])
        expanded[ident] = [entry(3 if n + 1 < len(parts) else 1, clean(la), clean(en))
                           for n, (la, en) in enumerate(parts)]
    offices = []
    for position, prayer in enumerate(json.loads(Path(prayers).read_text())["prayers"] if prayers else []):
        blocks = []
        for block in prayer["blocks"]:
            latin, english = block.get("latin", ""), block.get("english", "")
            if block["kind"] != "prayer":
                blocks.append(entry(KINDS.get(block["kind"], 2), clean(latin), clean(english)))
                continue
            # With no Latin, each line of the English is its own piece.
            parts = phrases(latin, english) if latin.strip() else [("", line) for line in english.split("\n") if line.strip()]
            blocks += [entry(3 if n + 1 < len(parts) else 1, clean(la), clean(en)) for n, (la, en) in enumerate(parts)]
        ident = f"prayer:{position}"
        expanded[ident] = blocks
        offices.append((0, 2, position, {"title": prayer["title"], "blocks": [ident]}))
    if len(entries) > 0xFFFF:
        raise SystemExit("Too many texts for 16-bit block indices")
    for day, rites in data["days"].items():
        number = int(day.replace("-", ""))
        for rite, hours in rites.items():
            for hour, office in hours.items():
                offices.append((number, RITES.index(rite), HOURS.index(hour), office))
    offices.sort(key=lambda entry: entry[:3])
    header = 20
    texts_at = header
    body = bytearray()
    base = texts_at + 4 * len(entries)
    offsets = []
    for kind, latin, english in entries:
        if len(latin) > 0xFFFF or len(english) > 0xFFFF:
            raise SystemExit("Prayer text too long")
        offsets.append(base + len(body))
        body += struct.pack("<BHH", kind, len(latin), len(english)) + latin + english
    offices_at = base + len(body)
    tail = bytearray()
    tail_base = offices_at + 20 * len(offices)
    records = bytearray()
    for number, rite, hour, office in offices:
        title = clean(office["title"])
        title_at = tail_base + len(tail)
        tail += title
        blocks_at = tail_base + len(tail)
        blocks = [index for ident in office["blocks"] for index in expanded[ident]]
        tail += struct.pack(f"<{len(blocks)}H", *blocks)
        records += struct.pack("<IBBHIII", number, rite, hour, len(title), title_at, len(blocks), blocks_at)
    return (b"MPO1" + struct.pack("<IIII", len(entries), len(offices), texts_at, offices_at)
            + struct.pack(f"<{len(offsets)}I", *offsets) + body + records + tail)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", default=root / "tools/office/office-pack.json")
    parser.add_argument("output")
    parser.add_argument("--prayers", default=root / "tools/office/prayers.json")
    args = parser.parse_args()
    Path(args.output).write_bytes(build(args.source, args.prayers))


if __name__ == "__main__":
    main()
