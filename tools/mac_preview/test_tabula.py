# SPDX-License-Identifier: Apache-2.0
"""Tabula is a mode of the preview and redraws when the Hour or the status changes."""
from datetime import datetime
import json
import struct
import urllib.request

from muse_mac.host import serve_preview
from tools.mac_preview.pocket_commands import COMMANDS, DisplayExecutor, DisplayState
from tools.mac_preview.run import application
from tools.mac_preview.tabula import PANEL, ZONE, TabulaPanel

STATUS = {'updated': '2026-10-05T09:20:00-05:00',
          'status': [{'name': 'inbox', 'state': 'clear', 'checked': '2026-10-05T09:18:00-05:00'}]}


def test_preview_serves_the_panel_and_offers_the_mode():
    profile = application()
    server = serve_preview(DisplayState(), profile.preview_path, profile.routes)
    base = f'http://127.0.0.1:{server.server_port}'
    try:
        page = urllib.request.urlopen(base).read()
        assert b'<option value="tabula">Tabula</option>' in page and b'id="tabula-panel"' in page
        response = urllib.request.urlopen(base + '/tabula')
        image = response.read()
        assert response.headers['Content-Type'] == 'image/png'
        assert struct.unpack('>II', image[16:24]) == (480, 800)
        assert json.load(urllib.request.urlopen(base + '/state'))['tabula_key']
    finally:
        server.shutdown()
        server.server_close()


def test_key_changes_with_the_hour_the_status_and_staleness(tmp_path):
    panel = TabulaPanel(tmp_path / 'status.json')
    prime, terce = datetime(2026, 10, 5, 9, 59, tzinfo=ZONE), datetime(2026, 10, 5, 10, 0, tzinfo=ZONE)
    assert panel.key(prime) == panel.key(datetime(2026, 10, 5, 9, 0, tzinfo=ZONE))
    assert panel.key(prime) != panel.key(terce)
    before = panel.key(terce)
    plain = panel.png(terce)
    (tmp_path / 'status.json').write_text(json.dumps(STATUS))
    assert panel.key(terce) != before and panel.png(terce) != plain
    # A day later the same document is stale, and the panel is drawn again.
    assert panel.key(datetime(2026, 10, 6, 10, 0, tzinfo=ZONE))[-1] == 's' != panel.key(terce)[-1]
    # A malformed file is ignored rather than shown.
    (tmp_path / 'status.json').write_text('{"status": "work"}')
    assert panel.key(terce) == before


def test_status_command_is_validated_and_keeps_the_previous_on_failure():
    assert 'pocket.set_tabula_status' in COMMANDS
    executor = DisplayExecutor(DisplayState())
    previous = PANEL.pushed
    try:
        assert executor.run('pocket.set_tabula_status', {'payload': json.dumps(STATUS)})['ok']
        accepted = PANEL.pushed
        broken = dict(STATUS, status=[{'name': 'inbox', 'state': 'clear'}])
        assert not executor.run('pocket.set_tabula_status', {'payload': json.dumps(broken)})['ok']
        assert PANEL.pushed == accepted
    finally:
        PANEL.pushed = previous
