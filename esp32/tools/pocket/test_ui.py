# SPDX-License-Identifier: Apache-2.0
"""Compile the portable Pocket screens on the host and run their checks."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OFFICE = ROOT / "esp32/main/office"


@unittest.skipUnless(shutil.which("c++"), "needs a host C++ compiler")
class PocketScreens(unittest.TestCase):
    def test_screens_render_and_every_office_paginates(self):
        with tempfile.TemporaryDirectory() as work:
            pack, binary = Path(work) / "office.bin", Path(work) / "ui_harness"
            subprocess.run([sys.executable, str(ROOT / "tools/office/build_firmware_pack.py"),
                            str(ROOT / "tools/office/office-pack.json"), str(pack)], check=True)
            sources = [Path(__file__).with_name("ui_harness.cpp")] + [OFFICE / name for name in
                       ("ui.cpp", "pack.cpp", "raster.cpp", "font_data.cpp")]
            subprocess.run(["c++", "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-o", str(binary),
                            *map(str, sources)], check=True)
            result = subprocess.run([str(binary), str(pack)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
