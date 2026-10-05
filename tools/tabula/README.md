# Tabula: the altar-card screen

Tabula is the third display mode: a saint for the day, the day and hour, the
Hour's versicle and response in Latin, and one line of personal agent status.
This directory holds the picture pack and the tools that build and draw it.

| File | Purpose |
| --- | --- |
| `build_plates.py` | Lists the Callot series, fetches scans, makes the one-bit plates and both reports |
| `catalogue.json` | Every impression in the series: day, subject, accession, Met object, Commons file, licence |
| `calendar-m1963.json` | Fixed feasts of the general Monastic 1963 calendar, from Divinum Officium |
| `plates/`, `plates.json` | The bundled one-bit plates, named by month and day, and their manifest |
| `build_type.py`, `type.mtf` | The one-bit type: EB Garamond and Noto Sans, both OFL (`fonts/`) |
| `pack.py` | Packs type, plates and versicles into the one file the firmware embeds |
| `compose.py` | Draws the panel; the reference the firmware is held to |
| `golden/` | The panel for 5 October 2026 at Terce, and the status used for it |
| `skip.txt` | Days to show the plain fallback panel instead of the plate |
| `COVERAGE.md` | Which days have a plate, and where Callot's day differs from the calendar's feast |
| `PROVENANCE.md` | Source, accession and licence of each bundled plate |

## The plates

Jacques Callot's *Les Images de tous les Saincts et Saintes de l'Année* (1636)
has a small etching for each entry of the Roman calendar of its day; each plate
is lettered with the saint's Latin name and the date. The Metropolitan Museum's
impressions are CC0. The scans are read from Wikimedia Commons, where the Met
donated them and where each file names its accession and object page; the Met's
own Open Access CSV confirms the public-domain flag.

They are line art, so they are reduced and thresholded, never dithered, and
cropped to the sheet, which is trimmed close to the plate.

The plates are indexed by month and day as Callot dated them. Where a day has
several, the one sharing a name with that day's feast is taken, else the first.
The 1636 calendar is not the 1963 monastic one: read `COVERAGE.md`.

## Skipping a plate

Many plates are martyrdoms and some show nudity; the plate for 5 October is
both. Put any day that should not be shown where the reader sits into
`skip.txt`, one `MM-DD` to a line. A skipped day, and a day with no plate,
shows `plates/fallback.png`, a ruled panel with a cross drawn by the tool. The
skip list is read when the screen is composed and when the firmware is built,
so the plates need not be rebuilt; rebuild and reinstall the firmware for the
reader to follow it.

## Rebuilding

```sh
python3 tools/tabula/build_plates.py            # index, fetch, process, report
python3 tools/tabula/build_plates.py --range year --out /tmp/year process
```

Needs Python 3 with Pillow. `index` and `fetch` use the network and keep scans
under `.references/tabula/scans`, outside git. To check public-domain flags,
put the Met's `MetObjects.csv` from <https://github.com/metmuseum/openaccess>
at `.references/tabula/MetObjects.csv` first. `calendar` re-reads the feasts
from a Divinum Officium checkout (see `tools/office/README.md`).

Only the plates for the days of the bundled prayer pack are committed: 32
plates, about 410 KiB as PNG and 550 KiB as the raw rows the firmware carries.
The whole year is 357 plates, about 4.7 MiB as PNG and 6.1 MiB raw, which does
not fit beside the firmware in its 8 MiB slot without compression.

## Drawing a panel

```sh
python3 tools/tabula/compose.py --when 2026-10-05T10:30 --status tools/tabula/golden/status.json --out panel.png
```

`--when` is local time in `--tz` (default `America/Chicago`). The Hour follows
the same schedule as the Hours screen, and before Matins the previous day's
Compline is still the Hour. The versicle is the Hour's own, the last versicle
and response before the collect in the bundled office, with the doxology passed
over. A day outside the prayer pack shows *Deus, in adiutórium*. The initial is
a plain capital on a ruled ground drawn by the composer; no woodcut alphabet is
bundled.

## The status line

One line at the foot, from a small JSON document:

```json
{"updated": "2026-10-05T09:20:00-05:00",
 "status": [{"name": "inbox", "state": "clear", "note": "", "checked": "2026-10-05T09:18:00-05:00"}]}
```

- `updated` and each `checked` are Unix seconds or ISO 8601 with an offset.
- At most six sources, shown in the order given. `name` is up to 24 bytes,
  `state` up to 16 and the optional `note` up to 80, with no control characters.
- A source that has never reported has `checked` 0.
- Other keys are ignored. A document that does not match is refused whole.

A source checked more than a day ago, or never, is drawn in a dotted box, the
mark the Watchlist uses for stale cards. Notes are dropped first when the line
is too long, then sources from the end, with an ellipsis. With no document the
foot is left empty.

This line is for personal sources only. Do not point it at work systems.

## How the status line reaches the reader

The reader takes the document through one function, `pocket_set_tabula_status`,
and keeps the last accepted line in flash. Two routes feed it; neither puts an
address or a credential in the source.

1. **Your own content backend (the intended route).** The private packager can
   already give the reader the address and token of a server of yours
   (`--backend-stdin`, see `docs/build.md`), and the reader fetches from it by
   itself over Wi-Fi. That server serves the status document at
   `GET <address>/tabula`, with the reader's bearer token and an `ETag`. No
   computer has to be awake and nothing new is stored on the reader.
2. **A Muse command.** `pocket.set_tabula_status` carries the same document
   over the paired connection, as the Watchlist does.

The server's document also has `date`, `hour` and `panel_id`. The reader ignores
them: it draws the panel itself from the bundled pack and its own clock.

The backend fetch task (`esp32/main/pocket_backend.cpp`) is not on the branch
this was written on, so only route 2 is wired up here. Joining route 1 is a few
lines in that task, next to the Mass and reading-list fetches: every five
minutes request `<address>/tabula`, and on a `200` pass the body, with a
terminating zero, to `pocket_set_tabula_status`. That has not been written or
run.
