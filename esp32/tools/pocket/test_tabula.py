# SPDX-License-Identifier: Apache-2.0
"""The firmware's Tabula must draw the reference composer's panel, pixel for pixel."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.tabula import compose, pack  # noqa: E402

CENTRAL = timezone(timedelta(hours=-5))  # Central daylight time, as in October
CROWDED = {"updated": 1, "status": [
    {"name": f"source-number-{n}", "state": "waiting-on-input", "note": "a note much too long to fit on the line", "checked": 0}
    for n in range(6)]}


@unittest.skipUnless(shutil.which("c++"), "needs a host C++ compiler")
class TabulaOnTheReader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        work = Path(cls.work.name)
        cls.binary, cls.pack_file = work / "tabula_harness", work / "tabula.bin"
        subprocess.run([sys.executable, str(ROOT / "tools/tabula/pack.py"), str(cls.pack_file)], check=True, capture_output=True)
        subprocess.run(["c++", "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-o", str(cls.binary),
                        str(Path(__file__).with_name("tabula_harness.cpp")), str(ROOT / "esp32/main/office/tabula.cpp")], check=True)
        cls.reference = compose.Pack(cls.pack_file.read_bytes())

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def firmware(self, date, weekday, hour, valid, now, items):
        out = Path(self.work.name) / "panel.pgm"
        sources = [str(v) for item in items for v in (item["name"], item["state"], item["note"], item["checked"])]
        result = subprocess.run([str(self.binary), str(self.pack_file), str(out), str(date), str(weekday), str(hour),
                                 "1" if valid else "0", str(now), *sources], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return out.read_bytes().split(b"\n255\n", 1)[1]

    def same(self, when, status=None):
        date, weekday, hour = compose.office_time(when)
        items = compose.parse_status(status) if status else []
        now = int(when.timestamp())
        expected = bytes(compose.render(self.reference, date, weekday, hour, True, items, now).pixels)
        drawn = self.firmware(date, weekday, hour, True, now, items)
        differing = sum(a != b for a, b in zip(expected, drawn))
        self.assertEqual(differing, 0, f"{differing} pixels differ at {when.isoformat()}")

    def test_golden_day_matches_the_committed_image(self):
        status = (ROOT / "tools/tabula/golden/status.json").read_text()
        when = datetime(2026, 10, 5, 10, 30, tzinfo=CENTRAL)
        self.same(when, status)
        date, weekday, hour = compose.office_time(when)
        drawn = self.firmware(date, weekday, hour, True, int(when.timestamp()), compose.parse_status(status))
        width, height, rows = pack.read_png(ROOT / "tools/tabula/golden/2026-10-05-terce.png")
        packed = bytes(sum((drawn[y * 480 + x + b] == 0) << (7 - b) for b in range(8))
                       for y in range(800) for x in range(0, 480, 8))
        self.assertEqual((width, height, packed), (480, 800, rows))

    def test_every_hour_of_several_days_matches(self):
        for day in (2, 5, 11, 25, 28):
            for minute in (0, 320, 400, 500, 620, 800, 900, 1100, 1300):
                self.same(datetime(2026, 10, day, minute // 60, minute % 60, tzinfo=CENTRAL))
        self.same(datetime(2026, 11, 1, 18, 30, tzinfo=CENTRAL))

    def test_status_line_matches_when_stale_crowded_or_absent(self):
        when = datetime(2026, 10, 7, 13, 0, tzinfo=CENTRAL)
        self.same(when, CROWDED)
        self.same(when, {"updated": 1, "status": [{"name": "ínbox", "state": "ok", "note": "ça va", "checked": int(when.timestamp()) - 60}]})
        self.same(when, {"updated": 1, "status": [{"name": "one-very-long-source-nam", "state": "sixteen-byte-sta", "note": "", "checked": 5}]})

    def test_outside_the_pack_and_without_a_clock(self):
        self.same(datetime(2027, 3, 15, 10, 30, tzinfo=CENTRAL))
        expected = bytes(compose.render(self.reference, 0, 0, 7, False, [], 0).pixels)
        self.assertEqual(self.firmware(0, 0, 7, False, 0, []), expected)


if __name__ == "__main__":
    unittest.main()
