# SPDX-License-Identifier: Apache-2.0
"""Tabula in the Mac preview: the same panel the reader draws, as a PNG.

The status line comes from the last valid `pocket.set_tabula_status` payload,
or else from a small local JSON file (schema in tools/tabula/README.md). The
file is for personal sources only.
"""
from datetime import datetime
import os
from pathlib import Path
import threading
import zlib
from zoneinfo import ZoneInfo

from tools.tabula import compose, pack

ZONE = ZoneInfo('America/Chicago')
DEFAULT_STATUS = Path(os.environ.get('MUSE_TABULA_STATUS', Path.home() / '.config/muse-pocket/tabula-status.json'))


class TabulaPanel:
    def __init__(self, status_path=DEFAULT_STATUS):
        self.status_path = Path(status_path)
        self.lock = threading.Lock()
        self.pushed = None
        self.pack = None
        self.cached = (None, None)

    def push(self, payload):
        """Accept a status document sent as a command; raises ValueError if it is malformed."""
        compose.parse_status(payload)
        with self.lock:
            self.pushed = payload

    def status(self):
        """The current status document, or None. A malformed file is ignored."""
        with self.lock:
            if self.pushed is not None:
                return self.pushed
        try:
            text = self.status_path.read_text()
            compose.parse_status(text)
            return text
        except (OSError, ValueError):
            return None

    def key(self, now=None):
        """Changes whenever the panel would: a new Hour or day, new status, or a source going stale."""
        now = now or datetime.now(ZONE)
        text = self.status()
        date, _, hour = compose.office_time(now)
        stale = ''.join('s' if compose.is_stale(item, int(now.timestamp())) else 'f'
                        for item in (compose.parse_status(text) if text else []))
        return f'{date}-{hour}-{zlib.crc32((text or "").encode()):08x}-{stale}'

    def png(self, now=None):
        now = now or datetime.now(ZONE)
        key = self.key(now)
        with self.lock:
            if self.cached[0] == key:
                return self.cached[1]
            if self.pack is None:
                self.pack = compose.Pack(pack.build())
        image = compose.compose(now, self.status(), self.pack).png()
        with self.lock:
            self.cached = (key, image)
        return image

    def route(self, query):
        return self.png(), 'image/png'


PANEL = TabulaPanel()
