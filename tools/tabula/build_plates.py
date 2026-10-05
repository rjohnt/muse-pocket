#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the calendar-indexed pack of Jacques Callot's saints for Tabula.

Callot's "Les Images de tous les Saincts et Saintes de l'Annee" (Paris, Israel
Henriet, 1636) has one small etching for each entry of the Roman calendar. The
Metropolitan Museum of Art released its impressions under CC0 and donated the
scans to Wikimedia Commons, which is where this tool reads them: each file
description there carries the Met's title, accession number, object page and
licence. The Met's own Open Access CSV, when given, confirms the public-domain
flag for every accession.

Steps, each of which reuses what the last one left in the work directory:

  index    list the series on Commons and write catalogue.json
  fetch    download the scans the chosen plates need (kept out of git)
  process  crop to the plate, threshold to one bit, write plates/ and plates.json
  report   write COVERAGE.md and PROVENANCE.md

Run with no step to do all four. Only `index` and `fetch` use the network.
"""
import argparse
import csv
import json
import re
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from datetime import date, timedelta
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WORK = ROOT / ".references/tabula"
API = "https://commons.wikimedia.org/w/api.php"
AGENT = "muse-pocket-tabula/0.1 (https://github.com/rjohnt/muse-pocket; offline e-paper calendar)"
CATEGORY = "Prints by Jacques Callot in the Metropolitan Museum of Art"
SERIES = "Les Images De Tous Les Saincts"
LICENCE = "CC0 1.0"
LICENCE_URL = "https://creativecommons.org/publicdomain/zero/1.0/"
MONTHS = ["January", "February", "March", "April", "May", "June", "July", "August",
          "September", "October", "November", "December"]
# The plate is drawn inside this box on the 480x800 panel.
BOX_W, BOX_H = 336, 424
SCAN_WIDTH = 960
# How much darker than the surrounding paper a pixel must be to count as ink.
THRESHOLD = 30


def get(params):
    url = API + "?" + urllib.parse.urlencode(dict(params, format="json", formatversion=2))
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def field(wikitext, name):
    match = re.search(r"^\s*\|\s*" + re.escape(name) + r"\s*=\s*(.*)$", wikitext, re.M)
    return match.group(1).strip() if match else ""


def plate_date(title):
    """Month and day from the Met's title, or None when it names no day."""
    match = re.search(r"\b(" + "|".join(MONTHS) + r")\s+(\d{1,2})(?:st|nd|rd|th)?\b", title)
    if not match:
        return None
    month, day = MONTHS.index(match.group(1)) + 1, int(match.group(2))
    try:
        date(2024, month, day)
    except ValueError:
        return None
    return f"{month:02d}-{day:02d}"


def subject(title):
    """The Met's title without the date and the name of the series."""
    text = re.split(r",?\s*(?:from\s+)?(?:" + "|".join(MONTHS) + r")\s+\d", title)[0]
    text = re.split(r",?\s*from\s+[\"“]?Les Images", text)[0]
    return text.strip(" ,")


def order(accession):
    """Sort key that follows the order of the plates in the Met's albums."""
    return [int(n) for n in re.findall(r"\d+", accession)]


def index(args):
    pages, more = [], {}
    while True:
        reply = get(dict(action="query", generator="search", gsrnamespace=6, gsrlimit=50,
                         gsrsearch=f'incategory:"{CATEGORY}" insource:"{SERIES}"',
                         prop="revisions|imageinfo", rvprop="content", rvslots="main",
                         iiprop="url|size|sha1", iiurlwidth=SCAN_WIDTH, **more))
        pages += reply.get("query", {}).get("pages", [])
        if "continue" not in reply:
            break
        more = reply["continue"]
        time.sleep(0.5)
    met = met_flags(args.met_csv)
    plates = []
    for page in pages:
        text = page["revisions"][0]["slots"]["main"]["content"]
        info = page["imageinfo"][0]
        title = field(text, "title")
        source = re.match(r"https://www\.metmuseum\.org/art/collection/search/(\d+)", field(text, "source"))
        accession = field(text, "accession number")
        if SERIES.lower() not in title.lower() or not source or not accession:
            continue
        plates.append(dict(
            date=plate_date(title), subject=subject(title), title=title, accession=accession,
            met_object=int(source.group(1)), met_url=source.group(0),
            met_public_domain=met.get(accession),
            commons_file=page["title"], commons_url=info["descriptionurl"],
            scan_url=info["thumburl"].split("?")[0], original_sha1=info["sha1"],
            state=field(text, "medium"), credit=field(text, "credit line"),
            licence=LICENCE if "{{Cc-zero}}" in field(text, "permission") else "unverified"))
    plates.sort(key=lambda p: (p["date"] or "99-99", order(p["accession"])))
    catalogue = dict(series="Les Images de tous les Saincts et Saintes de l'Année", artist="Jacques Callot",
                     publisher="Israël Henriet, Paris, 1636", holder="The Metropolitan Museum of Art, New York",
                     listed_from=f"Wikimedia Commons, Category:{CATEGORY}",
                     met_open_access_csv="checked" if met else "not checked", plates=plates)
    (HERE / "catalogue.json").write_text(json.dumps(catalogue, ensure_ascii=False, indent=1) + "\n")
    print(f"catalogue: {len(plates)} plates, {sum(p['date'] is None for p in plates)} without a day")


def met_flags(path):
    """Accession number -> the Met's own public-domain flag, from its Open Access CSV."""
    if not path or not Path(path).exists():
        return {}
    flags = {}
    with open(path, newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            if "Callot" in row.get("Artist Display Name", ""):
                flags[row["Object Number"]] = row["Is Public Domain"] == "True"
    return flags


def catalogue():
    return json.loads((HERE / "catalogue.json").read_text())["plates"]


def usable(plate):
    return plate["date"] and plate["licence"] == LICENCE and plate["met_public_domain"] is not False


# ---- Calendar -----------------------------------------------------------------

def fold(text):
    """Lower case without accents, with the spellings that differ between Latin and French evened out."""
    text = unicodedata.normalize("NFD", text.lower().replace("æ", "e").replace("œ", "e"))
    text = "".join(c for c in text if not unicodedata.combining(c))
    text = text.replace("ph", "f").replace("h", "").replace("y", "i").replace("j", "i").replace("ae", "e")
    return re.sub(r"(.)\1", r"\1", text)


# Words that say what kind of day it is, not whose.
COMMON = {fold(w) for w in """saint sainte saints sancti sanctae sanctorum martyr martyrs martyris martyrum martyre
    martyres virgin virgins virginis virginum vierge vierges bishop eveque episcopi episcoporum episc confessor
    confessoris confessorum confesseur confessores abbot abbatis abbe pope papae pape doctor doctoris ecclesiae
    apostle apostles apostoli apostolorum apotre apotres beatae mariae marie mary festum feast fete from images
    their with companions compagnons compagnes sociorum socii king queen reine widow viduae veuve hermit ermite
    priest presbyteri pretre soldier soldat discipulorum octava octavam infra commemoratio vigilia
    classis duplex semiduplex simplex dominica feria sabbato secunda tertia quarta quinta sexta hebdomadam post
    pentecostes pentecosten januarii februarii martii aprilis maji junii julii augusti septembris octobris
    novembris decembris commemoration commemoratione translation translatio dedicatio dedicatione dedication
    dedicace basilica basilicae basilicas basiliques christi christ domini nostri jesu notre seigneur lord holy
    sacred sancta sanctus sanctissimi omnium fidelium pontificis pontificum archeveque archbishop evangeliste
    evangelist evangelistae patriarche patriarch penitent penitente wife femme elderly vieillard carthusian
    chartreux duchess duchesse monk moine""".split()}
# Feasts that are not named after a person, as the calendar and the Met's titles put them.
SAME = [("omnium sanctorum", "all saints"), ("fidelium defunctorum", "all souls"), ("rosario", "victories"),
        ("angelorum custodum", "guardian angel"), ("circumcisione", "circumcision"), ("epiphania", "epiphany"),
        ("purificatione", "purification"), ("annuntiatione", "annunciation"), ("visitatione", "visitation"),
        ("assumptione", "assumption"), ("nativitate", "nativity"), ("conceptione", "conception"),
        ("exaltatione", "exaltation"), ("inventione", "finding"), ("transfiguratione", "transfiguration"),
        ("praesentatione", "presentation"), ("innocentium", "innocents"), ("cathedra", "chair"),
        ("vincula", "chains"), ("ad nives", "snow"), ("decollatione", "beheading"), ("conversione", "conversion")]


def feast_name(title):
    return re.sub(r"~.*", "", title).strip()


def is_feast(title):
    """False for a Sunday, a feria or the Saturday office of Our Lady, which have no saint to compare."""
    return not re.match(r"(Dominica|Feria|Sanct. Mari. Sabbato|Die )", feast_name(title))


def stems(text):
    """Leading letters of the proper names in a title, in any of its languages."""
    return {w[:3] if len(w) < 6 else w[:4] for w in re.findall(r"[a-z]{4,}", fold(text)) if w not in COMMON}


def agrees(feast, plate):
    """True when the plate and the feast share a name: Placidi, Placide, Placid."""
    feast, title = feast_name(feast), plate["subject"]
    if any(a in feast.lower() and b in title.lower() for a, b in SAME):
        return True
    ours, theirs = stems(feast), stems(title)
    return any(a.startswith(b) or b.startswith(a) for a in ours for b in theirs)


def pack_feasts():
    """Date -> title of the day's office in the bundled prayer pack."""
    data = json.loads((ROOT / "tools/office/office-pack.json").read_text())
    feasts = {}
    for day, rites in data["days"].items():
        office = rites.get("monastic", {}).get("Lauds") or next(iter(rites.get("monastic", {}).values()), None)
        if office:
            feasts[day] = office["title"]
    return feasts


def year_feasts():
    """Month-day -> fixed feasts of the general Monastic 1963 calendar."""
    return json.loads((HERE / "calendar-m1963.json").read_text())["days"]


def extract_calendar(args):
    """Copy the fixed feasts out of Divinum Officium's Monastic 1963 calendar.

    Each of its calendars is written as changes to an older one, so the chain
    is read from the oldest forward; XXXXX removes a day's feast.
    """
    tables = Path(args.kalendar)
    versions = {}
    for line in (tables / "data.txt").read_text(encoding="utf-8").splitlines():
        cells = line.split(",")
        if len(cells) >= 4 and cells[0] not in versions:
            versions[cells[0]] = (cells[1], cells[4] if len(cells) > 4 else "")
    chain, version = [], "Monastic - 1963"
    while version:
        chain.append(versions[version][0])
        version = versions[version][1]
    days = {}
    for name in reversed(chain):
        for line in (tables / "Kalendaria" / f"{name}.txt").read_text(encoding="utf-8").splitlines():
            parts = line.split("=")
            if not re.fullmatch(r"\d\d-\d\d", parts[0]) or len(parts) < 2:
                continue
            names = [parts[i].strip() for i in range(2, len(parts) - 1, 2) if parts[i].strip()]
            if parts[1] == "XXXXX" or not names:
                days.pop(parts[0], None)
            else:
                days[parts[0]] = names
    out = dict(source="Divinum Officium, web/www/Tabulae/Kalendaria: " + ", ".join(f"{n}.txt" for n in reversed(chain)),
               revision="b6e94c5825ba656b223e78bd2c49cf973f2eea1a", licence="MIT; see tools/office/DIVINUM-LICENSE",
               note="Fixed feasts only. Sundays, ferias and movable feasts are not in this table.",
               days=dict(sorted(days.items())))
    (HERE / "calendar-m1963.json").write_text(json.dumps(out, ensure_ascii=False, indent=1) + "\n")
    print(f"calendar: {len(days)} days with a fixed feast")


def choose(plates, feasts_for):
    """One plate for each calendar day: the one that shares the feast's name, else the first."""
    by_day = {}
    for plate in plates:
        if usable(plate):
            by_day.setdefault(plate["date"], []).append(plate)
    chosen = {}
    for day, options in by_day.items():
        match = [p for p in options if any(agrees(f, p) for f in feasts_for(day) if is_feast(f))]
        chosen[day] = (match or options)[0]
    return chosen, by_day


def feasts_lookup():
    pack, year = pack_feasts(), year_feasts()
    by_month_day = {}
    for day, title in pack.items():
        by_month_day.setdefault(day[5:], []).append(title)
    return lambda day: by_month_day.get(day, []) + year.get(day, [])


def skipped():
    """Month-days to show the fallback panel for, from skip.txt."""
    days = set()
    for line in (HERE / "skip.txt").read_text().splitlines():
        entry = line.split("#")[0].strip()
        if not entry:
            continue
        if not re.fullmatch(r"\d\d-\d\d", entry):
            raise SystemExit(f"skip.txt: expected MM-DD, found {entry!r}")
        days.add(entry)
    return days


def pack_days():
    return sorted(day[5:] for day in pack_feasts())


# ---- Images -------------------------------------------------------------------

def scan_path(plate):
    return WORK / "scans" / (re.sub(r"[^0-9A-Za-z]+", "_", plate["accession"]).strip("_") + ".jpg")


def fetch(args):
    chosen, _ = choose(catalogue(), feasts_lookup())
    wanted = chosen.values() if args.range == "year" else [chosen[d] for d in pack_days() if d in chosen]
    (WORK / "scans").mkdir(parents=True, exist_ok=True)
    fetched = 0
    for plate in wanted:
        path = scan_path(plate)
        if path.exists() and path.stat().st_size > 1000:
            continue
        request = urllib.request.Request(plate["scan_url"], headers={"User-Agent": AGENT})
        for attempt in range(4):
            try:
                with urllib.request.urlopen(request, timeout=60) as response:
                    path.write_bytes(response.read())
                break
            except OSError as error:
                if attempt == 3:
                    print(f"could not fetch {plate['accession']}: {type(error).__name__}", file=sys.stderr)
                time.sleep(5 * (attempt + 1))
        fetched += 1
        time.sleep(0.4)
    print(f"scans: {fetched} fetched, {len(list((WORK / 'scans').glob('*.jpg')))} on disk")


def ink_mask(grey, margin):
    """White where a pixel is clearly darker than the paper around it."""
    from PIL import ImageChops, ImageFilter
    local = grey.filter(ImageFilter.GaussianBlur(max(6, grey.width // 24)))
    return ImageChops.subtract(local, grey).point(lambda v: 255 if v > margin else 0)


def crop_to_plate(grey):
    """Cut away the mount, leaving the sheet, which is trimmed close to the plate.

    The mount is blank, so the plate is everything between the first and last
    rows and columns that carry ink.
    """
    w, h = grey.size
    step = 3
    ink = ink_mask(grey.resize((w // step, h // step)), 40)
    sw, sh = ink.size
    data = ink.tobytes()
    cols = [sum(data[y * sw + x] > 0 for y in range(sh)) / sh for x in range(sw)]
    rows = [sum(data[y * sw + x] > 0 for x in range(sw)) / sw for y in range(sh)]

    def span(profile):
        busy = [i for i, v in enumerate(profile) if v > 0.03]
        return (busy[0], busy[-1] + 1) if busy else (0, len(profile))
    (x0, x1), (y0, y1) = span(cols), span(rows)
    if x1 - x0 < sw // 3 or y1 - y0 < sh // 3:
        return grey
    return grey.crop((max(0, x0 * step - 3), max(0, y0 * step - 3), min(w, x1 * step + 3), min(h, y1 * step + 3)))


def one_bit(grey):
    """Reduce to the box and threshold; line art is never dithered."""
    from PIL import Image, ImageChops, ImageFilter
    scale = min(BOX_W / grey.width, BOX_H / grey.height)
    size = (max(1, round(grey.width * scale)), max(1, round(grey.height * scale)))
    small = grey.resize(size, Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=1.0, percent=140, threshold=2))
    # Paper tone varies across a sheet, so each pixel is judged against its neighbourhood.
    ink = ink_mask(small, THRESHOLD)
    # A lone dot is foxing or paper grain, not a line.
    around = ink.filter(ImageFilter.Kernel((3, 3), (1, 1, 1, 1, 0, 1, 1, 1, 1), scale=1))
    ink = ImageChops.multiply(ink, around.point(lambda v: 255 if v else 0))
    return ink.point(lambda v: 0 if v else 255).convert("1")


def fallback_panel():
    """A plain ruled panel with a cross, shown for skipped and missing days."""
    from PIL import Image, ImageDraw
    w, h = 300, 380
    image = Image.new("1", (w, h), 1)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, w - 1, h - 1), outline=0, width=3)
    draw.rectangle((8, 8, w - 9, h - 9), outline=0, width=1)
    for corner_x in (20, w - 21):
        for corner_y in (20, h - 21):
            draw.regular_polygon((corner_x, corner_y, 6), 4, rotation=45, fill=0)
    cx, cy, arm, bar = w // 2, h // 2 - 16, 78, 10
    draw.rectangle((cx - bar // 2, cy - arm, cx + bar // 2, cy + arm + 44), fill=0)
    draw.rectangle((cx - arm + 18, cy - bar // 2, cx + arm - 18, cy + bar // 2), fill=0)
    for dx, dy in ((0, -arm), (0, arm + 44), (-arm + 18, 0), (arm - 18, 0)):
        draw.regular_polygon((cx + dx, cy + dy, 11), 4, rotation=45, fill=0)
    draw.ellipse((cx - 30, cy - 30, cx + 30, cy + 30), outline=0, width=2)
    return image


def process(args):
    from PIL import Image
    chosen, _ = choose(catalogue(), feasts_lookup())
    days = sorted(chosen) if args.range == "year" else [d for d in pack_days() if d in chosen]
    out = Path(args.out) if args.out else HERE / "plates"
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.png"):
        old.unlink()
    manifest, raw, missing = [], 0, 0
    for day in days:
        plate = chosen[day]
        if not scan_path(plate).exists():
            missing += 1
            continue
        with Image.open(scan_path(plate)) as scan:
            image = one_bit(crop_to_plate(scan.convert("L")))
        image.save(out / f"{day}.png", optimize=True)
        raw += (image.width + 7) // 8 * image.height
        manifest.append(dict(date=day, file=f"{day}.png", width=image.width, height=image.height,
                             **{k: plate[k] for k in ("subject", "accession", "met_object", "met_url",
                                                      "commons_file", "commons_url", "licence")}))
    fallback_panel().save(out / "fallback.png", optimize=True)
    stored = sum(p.stat().st_size for p in out.glob("*.png"))
    summary = dict(box=[BOX_W, BOX_H], range=args.range, plates=manifest, fallback="fallback.png",
                   licence_url=LICENCE_URL, raw_bytes=raw, png_bytes=stored)
    (out.parent / "plates.json" if not args.out else out / "plates.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=1) + "\n")
    print(f"plates: {len(manifest)} written, {missing} without a scan, "
          f"{raw / 1024:.0f} KiB as raw one-bit rows, {stored / 1024:.0f} KiB as PNG")


# ---- Reports ------------------------------------------------------------------

def other_day(feast, plates):
    """Where Callot put a feast that the calendar now keeps on another day.

    A name shared by many saints matches many days and says nothing, so only a
    name found on one or two days is reported.
    """
    days = sorted({p["date"] for p in plates if usable(p) and agrees(feast, p)})
    return days if len(days) <= 2 else []


def coverage(days, feasts_for, chosen, by_day, plates, label):
    """Rows of (day, feast, plate, verdict) and the three totals."""
    rows, counts = [], dict(agree=0, differ=0, none=0)
    for key, day in days:
        listed = feasts_for(key)
        feasts = [f for f in listed if is_feast(f)]
        feast = "; ".join(feast_name(f) for f in listed) if listed else "(no fixed feast)"
        if day not in chosen:
            counts["none"] += 1
            elsewhere = sorted({d for f in feasts for d in other_day(f, plates)})
            rows.append((key, feast, "no plate", "Callot has it on " + ", ".join(elsewhere) if elsewhere else ""))
            continue
        plate = chosen[day]
        names = "; ".join(p["subject"] for p in by_day[day])
        if any(agrees(f, plate) for f in feasts):
            counts["agree"] += 1
            rows.append((key, feast, names, "same feast"))
        else:
            counts["differ"] += 1
            elsewhere = sorted({d for f in feasts for d in other_day(f, plates)})
            note = "Callot has it on " + ", ".join(elsewhere) if elsewhere else ("" if feasts else "plate only")
            rows.append((key, feast, names, ("differs" + ("; " + note if note else "")) if feasts else "plate only"))
    return rows, counts


def table(rows):
    lines = ["| Day | Calendar | Callot, 1636 | |", "| --- | --- | --- | --- |"]
    lines += ["| " + " | ".join(cell.replace("|", "/") for cell in row) + " |" for row in rows]
    return "\n".join(lines)


def report(args):
    plates = catalogue()
    chosen, by_day = choose(plates, feasts_lookup())
    pack = pack_feasts()
    pack_rows, pack_counts = coverage([(d, d[5:]) for d in sorted(pack)], lambda key: [pack[key]], chosen, by_day, plates, "pack")
    year = year_feasts()
    all_days = [(date(2024, 1, 1) + timedelta(n)).strftime("%m-%d") for n in range(366)]
    year_rows, year_counts = coverage([(d, d) for d in all_days], lambda key: year.get(key, []), chosen, by_day, plates, "year")
    undated = [p for p in plates if not p["date"]]
    unusable = [p for p in plates if p["date"] and not usable(p)]
    with_feast = [d for d in all_days if any(is_feast(f) for f in year.get(d, []))]
    feast_with_plate = sum(d in chosen for d in with_feast)
    manifest = json.loads((HERE / "plates.json").read_text()) if (HERE / "plates.json").exists() else None
    out = ["# Coverage of Callot's saints against the bundled calendar", "",
           "Generated by `tools/tabula/build_plates.py report`. Do not edit.", "",
           f"The catalogue lists {len(plates)} impressions from the series: {len(plates) - len(undated)} carry a day in "
           f"the Met's title and {len(undated)} do not. They fall on {len(by_day)} of the 366 calendar days.", "",
           "A plate and a feast are counted as the same when their titles share a proper name "
           "(*Placidi*, *Placide*, *Placid*). This is a comparison of names, not a liturgical judgement: "
           "it misses a saint whose name is spelled very differently in French and Latin, and it cannot tell "
           "two saints of the same name apart. Read the tables before relying on a count.", "",
           "## Bundled prayer pack, 2 October to 2 November 2026", "",
           f"- {pack_counts['agree'] + pack_counts['differ']} of {len(pack)} days have a plate; {pack_counts['none']} do not.",
           f"- {pack_counts['agree']} plates show the feast the pack keeps that day; "
           f"{sum(1 for r in pack_rows if r[3].startswith('differs'))} show someone else; "
           f"{sum(1 for r in pack_rows if r[3] == 'plate only')} fall on a Sunday, a feria or a Saturday of Our Lady, "
           "where the pack has no saint to compare.", "",
           table(pack_rows), "",
           "## Whole year, general Monastic 1963 calendar", "",
           "The calendar column is the table of fixed feasts in Divinum Officium's Monastic 1963 calendar "
           "(`calendar-m1963.json`). It is not resolved against Sundays or movable feasts, so the office "
           "actually said on a given day of a given year can differ from it.", "",
           f"- {year_counts['agree'] + year_counts['differ']} of 366 days have a plate; {year_counts['none']} do not.",
           f"- {len(with_feast)} days have a fixed feast in the calendar; {feast_with_plate} of those have a plate.",
           f"- On {year_counts['agree']} days the plate shows a feast of that day.",
           f"- On {sum(1 for r in year_rows if r[3].startswith('differs'))} days the plate shows someone else, and on "
           f"{sum(1 for r in year_rows if r[3] == 'plate only')} the calendar has no fixed feast to compare.", "",
           table(year_rows), ""]
    if undated:
        out += ["## Impressions with no day in the Met's title", "",
                "\n".join(f"- {p['accession']}: {p['subject']}" for p in undated), ""]
    if unusable:
        out += ["## Impressions left out because the licence could not be confirmed", "",
                "\n".join(f"- {p['accession']}: {p['subject']}" for p in unusable), ""]
    (HERE / "COVERAGE.md").write_text("\n".join(out))

    prov = ["# Provenance of the Tabula plates", "",
            "Generated by `tools/tabula/build_plates.py report`. Do not edit.", "",
            "Jacques Callot (1592–1635), *Les Images de tous les Saincts et Saintes de l'Année*, etchings "
            "published by Israël Henriet, Paris, 1636. The impressions are in the Department of Drawings and "
            "Prints of The Metropolitan Museum of Art, New York, which released the images under "
            f"[Creative Commons Zero]({LICENCE_URL}) through its Open Access programme and donated the scans "
            "to Wikimedia Commons. CC0 asks for no attribution; it is given here so each plate can be traced.", "",
            "The bundled files are one-bit reductions of those scans, cropped to the plate. "
            "`fallback.png` was drawn by `build_plates.py` and is part of this repository's own source.", "",
            f"The Met's Open Access CSV was {'checked' if any(p['met_public_domain'] for p in plates) else 'not checked'} "
            "for the public-domain flag of every accession listed in `catalogue.json`.", ""]
    if manifest:
        prov += [f"## Bundled plates ({len(manifest['plates'])})", "",
                 "| Day | Subject | Accession | Met object | Commons file | Licence |", "| --- | --- | --- | --- | --- | --- |"]
        prov += [f"| {p['date']} | {p['subject']} | {p['accession']} | [{p['met_object']}]({p['met_url']}) | "
                 f"[{p['commons_file'][5:]}]({p['commons_url']}) | {p['licence']} |" for p in manifest["plates"]]
        prov += ["", "Every impression in the series, bundled or not, is listed with the same fields in `catalogue.json`.", ""]
    (HERE / "PROVENANCE.md").write_text("\n".join(prov))
    print(f"pack: {pack_counts}; year: {year_counts}; fixed feasts with a plate: {feast_with_plate}/{len(with_feast)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("step", nargs="?", choices=["index", "fetch", "process", "report", "calendar"])
    parser.add_argument("--range", choices=["pack", "year"], default="pack",
                        help="plates for the bundled prayer pack's days (default) or for every day")
    parser.add_argument("--met-csv", default=WORK / "MetObjects.csv",
                        help="the Met's Open Access MetObjects.csv, to confirm public-domain flags")
    parser.add_argument("--kalendar", default=ROOT / ".references/divinum-engine/web/www/Tabulae",
                        help="Divinum Officium's Tabulae directory, for the `calendar` step")
    parser.add_argument("--out", help="write plates somewhere other than tools/tabula/plates")
    args = parser.parse_args()
    steps = dict(index=index, fetch=fetch, process=process, report=report, calendar=extract_calendar)
    for name in ([args.step] if args.step else ["index", "fetch", "process", "report"]):
        steps[name](args)


if __name__ == "__main__":
    main()
