"""The dated pack must fail visibly rather than invent unavailable prayers."""
import json
import urllib.error
import urllib.request
import pytest
from muse_mac.host import serve_preview
from tools.mac_preview.office import OfficePack
from tools.mac_preview.run import application
from tools.mac_preview.pocket_commands import DisplayState


def test_dated_office_and_missing_content():
    pack = OfficePack()
    available = json.loads(pack.route({'date':['2026-10-03'], 'hour':['Vespers']})[0])
    assert available['available'] and available['blocks']
    assert available['source']['rite'] == 'Monastic - 1963'
    assert 'Clear Creek local propers not applied' in available['source']['calendar']
    for query in ({'date':['2100-01-01']}, {'date':['2026-10-03'], 'rite':['modern']}):
        result = json.loads(pack.route(query)[0])
        assert result['available'] is False
        assert 'blocks' not in result
    with pytest.raises(ValueError):
        pack.route({'date':['not-a-date']})
    with pytest.raises(ValueError):
        pack.route({'hour':['Invented hour']})


def test_pocket_profile_serves_office_without_muse():
    profile = application()
    server = serve_preview(DisplayState(), profile.preview_path, profile.routes)
    base = f'http://127.0.0.1:{server.server_port}'
    try:
        assert b'open-hours' in urllib.request.urlopen(base).read()
        response = urllib.request.urlopen(base + '/office?date=2026-10-03&hour=Lauds')
        assert json.load(response)['available']
        with pytest.raises(urllib.error.HTTPError) as error:
            urllib.request.urlopen(base + '/office?date=invalid')
        assert error.value.code == 400
    finally:
        server.shutdown()
        server.server_close()
