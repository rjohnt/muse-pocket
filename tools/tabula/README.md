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
