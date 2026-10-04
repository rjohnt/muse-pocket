# Dated bilingual office packs

`build_daily.py` runs the pinned [Divinum Officium](https://github.com/DivinumOfficium/divinum-officium)
Perl engine locally. It resolves variable prayers ahead of time; the Mac preview
reads the generated pack offline. The firmware build packs the same JSON with
`build_firmware_pack.py`; `build_fonts.py` regenerates its glyph subsets.

`prayers.json` holds the common prayers shown under Hours. Each entry has a
title and blocks; put matching Latin and English on matching lines, or leave the
Latin out for an English-only prayer. The texts were entered by hand and should
be proofread against a trusted source before being relied on. If an entry uses a
character the fonts do not have yet, run `build_fonts.py` again.

The packer cuts each prayer into short pieces so a Latin line sits above its own
translation. Lines and psalm pause marks divide a prayer where both languages
have the same number; within those, clauses are matched in order by length.
Where the English orders its clauses differently, a piece can sit beside its
neighbour's translation.

The source revision is `b6e94c5825ba656b223e78bd2c49cf973f2eea1a`, using
**Monastic – 1963**, Latin and traditional English. This is the general monastic
calendar. Clear Creek local propers and its precise observance are not verified.
The English is not the modern approved Liturgy of the Hours translation.

```sh
git clone https://github.com/DivinumOfficium/divinum-officium .references/divinum-engine
git -C .references/divinum-engine checkout b6e94c5825ba656b223e78bd2c49cf973f2eea1a
python3 tools/office/build_daily.py --start 2026-10-02 --days 32
```

Requires Python 3 and Perl. The output is `office-pack.json`, with deduplicated
bilingual sections and date/hour references. Rebuild for dates beyond the bundled
October 2–November 2, 2026 range. A dated pack is not a perpetual calendar engine.
Text alignment is an extraction heuristic and still needs liturgical review.

Divinum Officium’s [MIT license](DIVINUM-LICENSE) is retained for the derived
content.
