# SPDX-License-Identifier: Apache-2.0
"""The Tabula panel: one golden image, and the rules the layout must keep."""
import json
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

import pytest

from tools.tabula import compose, pack

HERE = Path(__file__).resolve().parent
CHICAGO = ZoneInfo("America/Chicago")
STATUS = (HERE / "golden/status.json").read_text()
TERCE = datetime(2026, 10, 5, 10, 30, tzinfo=CHICAGO)


@pytest.fixture(scope="module")
def tabula():
    return compose.Pack(pack.build())


def ink(canvas, top, bottom):
    return sum(p == 0 for p in canvas.pixels[top * compose.W:bottom * compose.W])


def test_golden_panel_for_5_october_at_terce(tabula):
    canvas = compose.compose(TERCE, STATUS, tabula)
    width, height, rows = pack.read_png(HERE / "golden/2026-10-05-terce.png")
    assert (width, height) == (compose.W, compose.H)
    drawn = bytes(sum((canvas.pixels[y * 480 + x + b] == 0) << (7 - b) for b in range(8))
                  for y in range(800) for x in range(0, 480, 8))
    # Regenerate with: python3 tools/tabula/compose.py --when 2026-10-05T10:30
    #   --status tools/tabula/golden/status.json --out tools/tabula/golden/2026-10-05-terce.png
    assert drawn == rows


def test_the_hour_follows_the_local_clock():
    assert compose.office_time(TERCE) == (20261005, 1, 3)
    assert compose.office_time(datetime(2026, 10, 5, 9, 59, tzinfo=CHICAGO))[2] == 2
    assert compose.office_time(datetime(2026, 10, 4, 12, 50, tzinfo=CHICAGO)) == (20261004, 0, 4)
    # Before Matins the previous day's Compline is still being said.
    assert compose.office_time(datetime(2026, 10, 5, 3, 0, tzinfo=CHICAGO)) == (20261004, 0, 7)


def test_the_versicle_is_the_hours_own_and_latin():
    table = pack.verses()
    assert table[(20261005, 3)] == ("Óculi Dómini super metuéntes eum.", "Et in eis, qui sperant super misericórdia eius.")
    data = json.loads(pack.OFFICE.read_text())
    latin = "\n".join(block["latin"] for block in data["texts"].values())
    for (day, hour), (versicle, response) in table.items():
        assert versicle and response and "Glória Patri" not in versicle
        if day:
            # Every word shown comes from the Latin column of the prayer pack.
            assert versicle.split()[0] in latin and response.split()[-1] in latin


def test_every_bundled_versicle_fits_its_space(tabula):
    room = compose.VERSE_BOTTOM - compose.VERSE_TOP
    for versicle, response in set(pack.verses().values()):
        assert any(compose.verse_layout(tabula, size, versicle, response)["height"] <= room
                   for size in range(compose.SIZES)), versicle


def test_a_skipped_day_and_a_day_without_a_plate_show_the_fallback():
    assert tabula_has("10-05", skip=set())
    skipping = compose.Pack(pack.build(skip={"10-05"}))
    assert not skipping.has_plate(1005) and skipping.has_plate(1006)
    fallback = skipping.plate(0)
    assert skipping.plate(1005) == fallback == skipping.plate(315)
    shown = compose.compose(TERCE, None, skipping)
    usual = compose.compose(TERCE, None, compose.Pack(pack.build(skip=set())))
    assert shown.pixels[:440 * 480] != usual.pixels[:440 * 480]
    assert shown.pixels[445 * 480:] == usual.pixels[445 * 480:]


def tabula_has(day, skip):
    return compose.Pack(pack.build(skip=skip)).has_plate(int(day.replace("-", "")))


def test_skip_list_is_read_and_checked(tmp_path):
    assert pack.skipped() == set()
    listed = tmp_path / "skip.txt"
    listed.write_text("# martyrdoms\n10-05\n11-02  # too grim\n\n")
    assert pack.skipped(listed) == {"10-05", "11-02"}
    listed.write_text("5 October\n")
    with pytest.raises(SystemExit):
        pack.skipped(listed)


def test_status_document_is_validated():
    items = compose.parse_status(STATUS)
    assert [item["name"] for item in items] == ["inbox", "errands", "backup"]
    assert items[1]["note"] == "2 left" and items[0]["note"] == ""
    epoch = {"updated": 1791210000, "status": [{"name": "inbox", "state": "unknown", "checked": 0}], "panel_id": None}
    assert compose.parse_status(epoch)[0]["checked"] == 0
    good = json.loads(STATUS)
    for broken in (
        {**good, "updated": "2026-10-05T09:20:00"},                      # no offset
        {**good, "status": good["status"] * 3},                           # more than six
        {**good, "status": [{**good["status"][0], "name": "x" * 25}]},
        {**good, "status": [{**good["status"][0], "state": ""}]},
        {**good, "status": [{**good["status"][0], "note": "line\nbreak"}]},
        {**good, "status": [{"name": "inbox", "state": "ok"}]},           # never says when
        ["not", "an", "object"],
    ):
        with pytest.raises(ValueError):
            compose.parse_status(broken)


def test_stale_sources_are_boxed_and_the_line_never_overflows(tabula):
    fresh = json.loads(STATUS)
    fresh["status"][2]["checked"] = "2026-10-05T10:00:00-05:00"
    with_stale, without = compose.compose(TERCE, STATUS, tabula), compose.compose(TERCE, fresh, tabula)
    assert ink(with_stale, 770, 800) > ink(without, 770, 800)
    crowded = {"updated": 1, "status": [{"name": "source-number-%d" % n, "state": "waiting-on-input",
                                         "note": "a note that is much too long to fit on one line", "checked": 0}
                                        for n in range(6)]}
    canvas = compose.compose(TERCE, crowded, tabula)
    for y in range(770, 800):
        row = canvas.pixels[y * 480:(y + 1) * 480]
        assert row[:compose.LEFT] == b"\xff" * compose.LEFT and row[compose.RIGHT:] == b"\xff" * (480 - compose.RIGHT)
    # With no status there is no footer at all.
    assert ink(compose.compose(TERCE, None, tabula), 764, 800) == 0


def test_unset_clock_and_days_outside_the_pack_still_draw(tabula):
    blank = compose.render(tabula, 0, 0, 7, False, [], 0)
    assert ink(blank, 0, 440) > 1000 and ink(blank, 500, 762) > 1000
    later = compose.compose(datetime(2027, 3, 15, 10, 30, tzinfo=CHICAGO), None, tabula)
    assert tabula.verse(20270315, 3) == pack.DEFAULT
    assert ink(later, 500, 762) > 1000
