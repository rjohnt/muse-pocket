#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Draw the Tabula panel: 480x800, one bit, Latin only.

Top to bottom: the day's saint by Callot, a rule, the day and Hour in spaced
capitals, the Hour's versicle and response with a large initial, and one line
of personal agent status.

This is the reference drawing. esp32/main/office/tabula.cpp draws the same
panel on the reader from the same pack, and the tests hold the two to the same
pixels, so change them together. Everything here is integer arithmetic for
that reason. Standard library only.

    python3 tools/tabula/compose.py --when 2026-10-05T09:30 --out panel.png
"""
import argparse
import json
import struct
import unicodedata
import zlib
from datetime import datetime, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

try:
    from . import pack as packer
except ImportError:  # run as a script
    import pack as packer

W, H = 480, 800
LEFT, RIGHT = 24, 456
PLATE_TOP, PLATE_BOX = 14, 424
RULE_Y, DAY_Y, DAY_RULE_Y = 450, 461, 492
VERSE_TOP, VERSE_BOTTOM = 500, 762
FOOT_RULE_Y, FOOT_Y = 768, 775
# Faces by position in type.mtf.
TEXT, INITIAL, CAPITALS, SMALL, SIZES = 0, 3, 6, 7, 3
CAPITAL_SPACING = 4
# Clear Creek's published ordinary schedule, minutes after local midnight.
TIMES = (315, 375, 480, 600, 770, 875, 1080, 1225)
DAYS = ("DOMINICA", "FERIA II", "FERIA III", "FERIA IV", "FERIA V", "FERIA VI", "SABBATO")
HOURS = ("AD MATUTINUM", "AD LAUDES", "AD PRIMAM", "AD TERTIAM", "AD SEXTAM", "AD NONAM",
         "AD VESPERAS", "AD COMPLETORIUM")
NO_CLOCK = "HORA IGNOTA"
STALE_AFTER = 86400
MAX_SOURCES, NAME_BYTES, STATE_BYTES, NOTE_BYTES = 6, 24, 16, 80


class Face:
    def __init__(self, data, base, at):
        count, glyphs_at, bits_at, self.line, self.size = struct.unpack_from("<IIIHH", data, at)
        self.data, self.bits_at, self.glyphs = data, base + bits_at, {}
        for i in range(count):
            code, offset, w, h, advance, x, y = struct.unpack_from("<IIBBBbb", data, base + glyphs_at + 16 * i)
            self.glyphs[code] = (offset, w, h, advance, x, y)

    def glyph(self, code):
        return self.glyphs.get(code) or self.glyphs.get(ord("?"))


class Pack:
    """Read-only view of the file written by pack.py."""
    def __init__(self, data):
        if data[:4] != b"MPT1":
            raise ValueError("not a Tabula pack")
        self.data = data
        type_at, _, self.plates_at, self.plate_count, self.verses_at, self.verse_count = struct.unpack_from("<IIIIII", data, 4)
        if data[type_at:type_at + 4] != b"MTF1":
            raise ValueError("no type in the Tabula pack")
        count = struct.unpack_from("<I", data, type_at + 4)[0]
        self.faces = [Face(data, type_at, type_at + 8 + 16 * i) for i in range(count)]

    def plate(self, month_day):
        """(width, height, rows offset) for a day, or for the fallback panel."""
        found = None
        for i in range(self.plate_count):
            day, w, h, _, at = struct.unpack_from("<HHHHI", self.data, self.plates_at + 12 * i)
            if day == month_day:
                return w, h, at
            if day == 0:
                found = (w, h, at)
        return found

    def has_plate(self, month_day):
        return any(struct.unpack_from("<H", self.data, self.plates_at + 12 * i)[0] == month_day
                   for i in range(self.plate_count)) if month_day else False

    def verse(self, date, hour):
        """Versicle and response for an office, or the default for the Hour."""
        found = ("", "")
        for i in range(self.verse_count):
            day, at_hour, _, a_len, a_at, b_len, _, b_at = struct.unpack_from("<IBBHIHHI", self.data, self.verses_at + 20 * i)
            if at_hour == hour and day in (0, date):
                found = (self.data[a_at:a_at + a_len].decode(), self.data[b_at:b_at + b_len].decode())
                if day == date:
                    break
        return found


class Canvas:
    def __init__(self):
        self.pixels = bytearray(b"\xff" * (W * H))

    def rect(self, x, y, w, h, colour=0):
        for row in range(max(0, y), min(H, y + h)):
            for col in range(max(0, x), min(W, x + w)):
                self.pixels[row * W + col] = colour

    def bits(self, data, at, w, h, x, y, stride_bits):
        """Blit packed ink. `stride_bits` is the bits in a row, w or w padded to a byte."""
        for row in range(h):
            for col in range(w):
                bit = row * stride_bits + col
                if data[at + bit // 8] & (0x80 >> (bit % 8)) and 0 <= x + col < W and 0 <= y + row < H:
                    self.pixels[(y + row) * W + x + col] = 0

    def text(self, face, string, x, y, spacing=0):
        for ch in string:
            glyph = face.glyph(ord(ch))
            if not glyph:
                continue
            offset, w, h, advance, gx, gy = glyph
            self.bits(face.data, face.bits_at + offset, w, h, x + gx, y + gy, w)
            x += advance + spacing
        return x

    def dotted(self, x, y, w, h):
        """A dotted outline, the mark of stale information on the other screens."""
        for col in range(x, x + w, 2):
            self.rect(col, y, 1, 1)
            self.rect(col, y + h - 1, 1, 1)
        for row in range(y, y + h, 2):
            self.rect(x, row, 1, 1)
            self.rect(x + w - 1, row, 1, 1)

    def diamond(self, cx, cy, radius):
        for dy in range(-radius, radius + 1):
            span = radius - abs(dy)
            self.rect(cx - span, cy + dy, 2 * span + 1, 1)

    def png(self):
        rows = b""
        for y in range(H):
            line = bytearray(W // 8)
            for x in range(W):
                if self.pixels[y * W + x]:
                    line[x // 8] |= 0x80 >> (x % 8)
            rows += b"\0" + bytes(line)

        def chunk(kind, body):
            return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 1, 0, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def measure(face, string, spacing=0):
    total, count = 0, 0
    for ch in string:
        glyph = face.glyph(ord(ch))
        if glyph:
            total += glyph[3]
            count += 1
    return total + spacing * max(0, count - 1)


def wrap(face, string, width_of):
    """Greedy lines of whole words; `width_of(n)` is the room on line n."""
    lines, line = [], ""
    for word in string.split(" "):
        if not word:
            continue
        trial = word if not line else line + " " + word
        if line and measure(face, trial) > width_of(len(lines)):
            lines.append(line)
            line = word
        else:
            line = trial
    if line:
        lines.append(line)
    return lines


def fit(face, string, width):
    """The string, or as much of it as fits with an ellipsis."""
    if measure(face, string) <= width:
        return string
    out = ""
    for ch in string:
        if measure(face, out + ch + "…") > width:
            break
        out += ch
    return out + "…"


def initial_of(string):
    """The capital to enlarge and the rest of the text, or (None, text)."""
    if not string:
        return None, string
    first = string[0]
    base = first if first in "ÆŒ" else unicodedata.normalize("NFD", first)[0].upper()
    if not ("A" <= base <= "Z" or base in "ÆŒ"):
        return None, string
    return base, string[1:]


def verse_layout(pack, size, versicle, response):
    face, big = pack.faces[TEXT + size], pack.faces[INITIAL + size]
    line = face.line
    column = LEFT + measure(face, packer.RESPONSE) + 8
    letter, rest = initial_of(versicle)
    if letter is None or not big.glyph(ord(letter)):
        letter, rest = None, versicle
    box = 2 * line if letter else 0
    beside = column + box + 12 if letter else column
    first = wrap(face, rest, lambda n: RIGHT - (beside if n < 2 else column))
    second = wrap(face, response, lambda n: RIGHT - column)
    above = max(len(first) * line, box)
    gap = line // 3
    return dict(face=face, big=big, line=line, column=column, letter=letter, box=box, beside=beside,
                first=first, second=second, above=above, gap=gap,
                height=above + gap + len(second) * line)


def draw_initial(canvas, big, letter, x, y, box):
    """The capital in a ruled square, on a ground of small lozenges."""
    top, side = y + 3, box - 6
    canvas.rect(x, top, side, side)
    canvas.rect(x + 2, top + 2, side - 4, side - 4, 255)
    canvas.rect(x + 5, top + 5, side - 10, side - 10)
    canvas.rect(x + 6, top + 6, side - 12, side - 12, 255)
    for py in range(10, side - 10):
        for px in range(10, side - 10):
            if (px + py) % 12 == 0 and (px - py) % 12 == 0:
                canvas.diamond(x + px, top + py, 1)
    offset, w, h, _, _, _ = big.glyph(ord(letter))
    gx, gy = x + (side - w) // 2, top + (side - h) // 2
    # Clear a margin round the letter so it stands free of the ground.
    for row in range(h):
        for col in range(w):
            bit = row * w + col
            if big.data[big.bits_at + offset + bit // 8] & (0x80 >> (bit % 8)):
                canvas.rect(max(x + 7, gx + col - 3), max(top + 7, gy + row - 3),
                            min(x + side - 7, gx + col + 4) - max(x + 7, gx + col - 3),
                            min(top + side - 7, gy + row + 4) - max(top + 7, gy + row - 3), 255)
    canvas.bits(big.data, big.bits_at + offset, w, h, gx, gy, w)


def draw_verse(canvas, pack, versicle, response):
    room = VERSE_BOTTOM - VERSE_TOP
    layout = None
    for size in range(SIZES):
        layout = verse_layout(pack, size, versicle, response)
        if layout["height"] <= room:
            break
    face, line, column = layout["face"], layout["line"], layout["column"]
    first, second = layout["first"], layout["second"]
    # Too long even at the smallest size: keep what fits and mark the cut.
    while layout["above"] + layout["gap"] + len(second) * line > room and len(second) > 1:
        second = second[:-1]
        second[-1] = fit(face, second[-1] + " …", RIGHT - column)
    height = layout["above"] + layout["gap"] + len(second) * line
    y = VERSE_TOP + max(0, (room - height) // 2)
    canvas.text(face, packer.VERSICLE, LEFT, y)
    if layout["letter"]:
        draw_initial(canvas, layout["big"], layout["letter"], column, y, layout["box"])
    for n, text in enumerate(first):
        if y + (n + 1) * line > VERSE_BOTTOM:
            break
        canvas.text(face, text, layout["beside"] if n < 2 else column, y + n * line)
    y += layout["above"] + layout["gap"]
    canvas.text(face, packer.RESPONSE, LEFT, y)
    for n, text in enumerate(second):
        if y + (n + 1) * line > VERSE_BOTTOM:
            break
        canvas.text(face, text, column, y + n * line)


def is_stale(item, now):
    return item["checked"] <= 0 or now - item["checked"] >= STALE_AFTER


def draw_status(canvas, pack, items, now):
    """One line: each source's name and state, a dotted box round the stale ones."""
    if not items:
        return
    face = pack.faces[SMALL]
    separator = "  ·  "
    gap = measure(face, separator)

    def labels(notes):
        return [item["name"] + " " + item["state"] + (" (" + item["note"] + ")" if notes and item["note"] else "")
                for item in items]

    def width(texts):
        return sum(measure(face, text) + (10 if is_stale(item, now) else 0)
                   for text, item in zip(texts, items)) + gap * (len(texts) - 1)
    texts = labels(True)
    if width(texts) > RIGHT - LEFT:
        texts = labels(False)
    # Still too wide: drop sources from the end and say so with an ellipsis.
    more = False
    while len(texts) > 1 and width(texts) + (measure(face, " …") if more else 0) > RIGHT - LEFT:
        texts.pop()
        more = True
    if len(texts) == 1:
        room = RIGHT - LEFT - (10 if is_stale(items[0], now) else 0) - (measure(face, " …") if more else 0)
        texts[0] = fit(face, texts[0], room)
    total = width(texts) + (measure(face, " …") if more else 0)
    x = LEFT + (RIGHT - LEFT - total) // 2
    for n, text in enumerate(texts):
        if n:
            x = canvas.text(face, separator, x, FOOT_Y)
        if is_stale(items[n], now):
            span = measure(face, text) + 10
            canvas.dotted(x, FOOT_Y - 1, span, face.line + 3)
            canvas.text(face, text, x + 5, FOOT_Y)
            x += span
        else:
            x = canvas.text(face, text, x, FOOT_Y)
    if more:
        canvas.text(face, " …", x, FOOT_Y)


def render(pack, date, weekday, hour, valid, items, now):
    """The panel as a Canvas.

    `date` is the office's yyyymmdd, `weekday` 0 for Sunday, `hour` 0 to 7.
    With `valid` false the clock is not set: the fallback panel is shown.
    `now` is Unix time, for marking stale status.
    """
    canvas = Canvas()
    w, h, at = pack.plate(date % 10000 if valid else 0)
    stride = (w + 7) // 8 * 8
    canvas.bits(pack.data, at, w, h, (W - w) // 2, PLATE_TOP + (PLATE_BOX - h) // 2, stride)
    # A double rule broken by a lozenge.
    for y in (RULE_Y, RULE_Y + 4):
        canvas.rect(LEFT, y, W // 2 - 14 - LEFT, 1)
        canvas.rect(W // 2 + 15, y, RIGHT - W // 2 - 15, 1)
    canvas.diamond(W // 2, RULE_Y + 2, 7)
    canvas.diamond(W // 2 - 22, RULE_Y + 2, 2)
    canvas.diamond(W // 2 + 22, RULE_Y + 2, 2)
    capitals = pack.faces[CAPITALS]
    label = DAYS[weekday % 7] + " · " + HOURS[hour % 8] if valid else NO_CLOCK
    canvas.text(capitals, label, (W - measure(capitals, label, CAPITAL_SPACING)) // 2, DAY_Y, CAPITAL_SPACING)
    canvas.rect(W // 2 - 60, DAY_RULE_Y, 121, 1)
    versicle, response = pack.verse(date if valid else 0, hour % 8)
    draw_verse(canvas, pack, versicle, response)
    if items:
        canvas.rect(LEFT, FOOT_RULE_Y, RIGHT - LEFT, 1)
        draw_status(canvas, pack, items, now if valid else 0)
    return canvas


# ---- Time and status ----------------------------------------------------------

def office_time(local):
    """(yyyymmdd, weekday with Sunday 0, hour) of the office being said at a local time."""
    minute = local.hour * 60 + local.minute
    hour = 7
    for index, start in enumerate(TIMES):
        if minute >= start:
            hour = index
    # Before Matins the previous day's Compline is still the Hour.
    day = local.date() - timedelta(days=1) if minute < TIMES[0] else local.date()
    return day.year * 10000 + day.month * 100 + day.day, (day.weekday() + 1) % 7, hour


def moment(value):
    """Unix seconds from a number or an ISO 8601 time with an offset."""
    if isinstance(value, bool):
        raise ValueError("not a time")
    if isinstance(value, (int, float)):
        if value < 0:
            raise ValueError("not a time")
        return int(value)
    if not isinstance(value, str) or len(value) > 50:
        raise ValueError("not a time")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("a time needs an offset")
    return int(parsed.timestamp())


def short(value, limit, required=True):
    if value is None and not required:
        return ""
    if not isinstance(value, str) or len(value.encode()) > limit or any(ord(c) < 32 for c in value):
        raise ValueError("text is missing, too long or has control characters")
    return value


def parse_status(document):
    """Validate a status document and return its sources. Raises ValueError.

    {"updated": time, "status": [{"name", "state", "note"?, "checked": time}]}
    Times are Unix seconds or ISO 8601 with an offset. At most six sources;
    name up to 24 bytes, state up to 16, note up to 80. A source that has
    never reported has checked 0. Keys this screen does not use are ignored.
    """
    data = json.loads(document) if isinstance(document, (str, bytes)) else document
    if not isinstance(data, dict):
        raise ValueError("status must be an object")
    moment(data.get("updated"))
    sources = data.get("status")
    if not isinstance(sources, list) or len(sources) > MAX_SOURCES:
        raise ValueError(f"status needs at most {MAX_SOURCES} sources")
    items = []
    for source in sources:
        if not isinstance(source, dict):
            raise ValueError("a source must be an object")
        name, state = short(source.get("name"), NAME_BYTES), short(source.get("state"), STATE_BYTES)
        if not name or not state:
            raise ValueError("a source needs a name and a state")
        items.append(dict(name=name, state=state, note=short(source.get("note"), NOTE_BYTES, required=False),
                          checked=moment(source.get("checked"))))
    return items


def load_pack():
    return Pack(packer.build())


def compose(when, status=None, pack=None):
    """The panel for an aware datetime and an optional status document."""
    pack = pack or load_pack()
    date, weekday, hour = office_time(when)
    items = parse_status(status) if status else []
    return render(pack, date, weekday, hour, True, items, int(when.timestamp()))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--when", help="local date and time, ISO 8601; now if omitted")
    parser.add_argument("--tz", default="America/Chicago")
    parser.add_argument("--status", help="status JSON file")
    parser.add_argument("--out", required=True, help="PNG to write")
    args = parser.parse_args()
    zone = ZoneInfo(args.tz)
    when = datetime.fromisoformat(args.when) if args.when else datetime.now(timezone.utc)
    when = when.replace(tzinfo=zone) if when.tzinfo is None else when.astimezone(zone)
    status = Path(args.status).read_text() if args.status else None
    Path(args.out).write_bytes(compose(when, status).png())


if __name__ == "__main__":
    main()
