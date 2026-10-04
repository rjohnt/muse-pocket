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


# Finer breaks are tried in turn: lines, the psalm pause marks, then
# sentences. A break is used only where Latin and English divide into the same
# number of parts, so each phrase keeps its own translation beside it. Commas
# are not used: the translations move clauses across them too freely.
BREAKS = (r"\n", r"(?<=[*\u2020\u2021])\s+", r"(?<=[.;:?!])\s+")


def phrases(latin, english):
    parts = [(latin, english)]
    for level, pattern in enumerate(BREAKS):
        finer = []
        for la, en in parts:
            a = [part.strip() for part in re.split(pattern, la) if part.strip()]
            b = [part.strip() for part in re.split(pattern, en) if part.strip()]
            # Below line level, a one-word fragment is more likely an
            # abbreviation or versicle sign than a phrase.
            whole = level == 0 or all(len(part.split()) > 1 for part in a + b)
            finer += zip(a, b) if len(a) == len(b) > 1 and whole else [(la, en)]
        parts = finer
    return parts


def build(source):
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
    if len(entries) > 0xFFFF:
        raise SystemExit("Too many texts for 16-bit block indices")
    offices = []
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
    args = parser.parse_args()
    Path(args.output).write_bytes(build(args.source))


if __name__ == "__main__":
    main()
