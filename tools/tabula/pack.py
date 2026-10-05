#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pack everything Tabula draws into the one file the firmware embeds.

Standard library only, so the firmware build can run it. Layout, little-endian,
offsets from the start of the file:

  "MPT1", u32 type_at, u32 type_size, u32 plates_at, u32 plate_count,
          u32 verses_at, u32 verse_count
  type:    type.mtf as written by build_type.py (its offsets are from type_at)
  plates:  plate_count records sorted by day, each
           u16 month*100+day (0 is the fallback panel), u16 width, u16 height,
           u16 reserved, u32 rows_at; rows are padded to a byte, first pixel
           in the high bit, 1 is ink
  verses:  verse_count records sorted by (date, hour), each
           u32 yyyymmdd (0 is the default for an hour with no office),
           u8 hour, u8 reserved, u16 versicle_len, u32 versicle_at,
           u16 response_len, u16 reserved, u32 response_at; UTF-8, NFC

A day in skip.txt is left out of the plates, so the reader shows the fallback.
"""
import argparse
import json
import re
import struct
import unicodedata
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
OFFICE = HERE.parent / "office/office-pack.json"
HOURS = ("Matins", "Lauds", "Prime", "Terce", "Sext", "None", "Vespers", "Compline")
# Said at the start of every Hour; shown when the pack has no office for the day.
DEFAULT = ("Deus, in adiutórium meum inténde.", "Dómine, ad adiuvándum me festína.")
VERSICLE, RESPONSE = "℣", "℟"


def clean(text):
    # Crosses and the pause marks of chant are left out of the card.
    text = unicodedata.normalize("NFC", re.sub("[✠✙†*]", "", text))
    return re.sub(r"\s+", " ", text).strip()


def versicle(office, texts):
    """The Hour's own versicle and response: the last pair before the collect."""
    lines = []
    for ident in office["blocks"]:
        block = texts[ident]
        if block["kind"] == "heading":
            if block["latin"].startswith("Oratio") and lines:
                break
            continue
        lines += [line.strip() for line in block["latin"].split("\n") if line.strip()]
    found = None
    for first, second in zip(lines, lines[1:]):
        # The doxology closes psalms and canticles; it is not the Hour's versicle.
        if first.startswith(VERSICLE) and second.startswith(RESPONSE) and "Glória Patri" not in first:
            found = (first, second)
    if not found:
        return DEFAULT
    return tuple(clean(line[1:].lstrip(". ")) for line in found)


def verses(office_pack=OFFICE):
    """(yyyymmdd, hour) -> (versicle, response), with the default under date 0."""
    data = json.loads(Path(office_pack).read_text())
    table = {(0, hour): DEFAULT for hour in range(8)}
    for day, rites in data["days"].items():
        for name, office in rites.get("monastic", {}).items():
            table[(int(day.replace("-", "")), HOURS.index(name))] = versicle(office, data["texts"])
    return table


def skipped(path=HERE / "skip.txt"):
    days = set()
    for line in Path(path).read_text().splitlines():
        entry = line.split("#")[0].strip()
        if not entry:
            continue
        if not re.fullmatch(r"(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])", entry):
            raise SystemExit(f"{path}: expected MM-DD, found {entry!r}")
        days.add(entry)
    return days


def read_png(path):
    """Width, height and packed rows (1 is ink) of a one-bit greyscale PNG."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    at, body, header = 8, b"", None
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        chunk = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            body += chunk
        at += 12 + length
    width, height, depth, colour, _, _, interlace = header
    if (depth, colour, interlace) != (1, 0, 0):
        raise ValueError(f"{path} must be a one-bit greyscale PNG, not interlaced")
    raw, stride = zlib.decompress(body), (width + 7) // 8
    rows, previous = [], bytes(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left, up = (line[i - 1] if i else 0), previous[i]
            corner = previous[i - 1] if i else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                nearest = min((abs(p - left), 0, left), (abs(p - up), 1, up), (abs(p - corner), 2, corner))[2]
                line[i] = (line[i] + nearest) & 255
        rows.append(bytes(line))
        previous = bytes(line)
    # PNG stores 1 as white; the pack stores 1 as ink. Padding bits stay clear.
    mask = bytes([0xff] * (width // 8) + ([0xff << (8 - width % 8) & 0xff] if width % 8 else []))
    return width, height, b"".join(bytes(~b & m & 0xff for b, m in zip(row, mask)) for row in rows)


def plates(directory=HERE / "plates", manifest=HERE / "plates.json", skip=None):
    skip = skipped() if skip is None else skip
    listed = json.loads(Path(manifest).read_text())
    out = [(0,) + read_png(Path(directory) / listed["fallback"])]
    for plate in listed["plates"]:
        if plate["date"] not in skip:
            out.append((int(plate["date"].replace("-", "")),) + read_png(Path(directory) / plate["file"]))
    return sorted(out)


def build(type_file=HERE / "type.mtf", skip=None, office_pack=OFFICE):
    faces = Path(type_file).read_bytes()
    pictures, table = plates(skip=skip), verses(office_pack)
    head = 28
    type_at = head
    plates_at = type_at + len(faces)
    verses_at = plates_at + 12 * len(pictures)
    body, strings = bytearray(), {}
    at = verses_at + 20 * len(table)

    def store(data):
        if data not in strings:
            strings[data] = at + len(body)
            body.extend(data)
        return strings[data]
    plate_records = b""
    for day, width, height, rows in pictures:
        plate_records += struct.pack("<HHHHI", day, width, height, 0, store(rows))
    verse_records = b""
    for (day, hour), (first, second) in sorted(table.items()):
        a, b = first.encode(), second.encode()
        verse_records += struct.pack("<IBBHIHHI", day, hour, 0, len(a), store(a), len(b), 0, store(b))
    return (b"MPT1" + struct.pack("<IIIIII", type_at, len(faces), plates_at, len(pictures), verses_at, len(table))
            + faces + plate_records + verse_records + bytes(body))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output")
    args = parser.parse_args()
    data = build()
    Path(args.output).write_bytes(data)
    print(f"tabula pack: {len(data)} bytes")


if __name__ == "__main__":
    main()
