# Muse Pocket · Mac preview and Hours

A development fork of [viticci/muse-pocket](https://github.com/viticci/muse-pocket),
with a Mac preview for a personal e-paper companion. The original firmware targets
the **Xteink X4 Pro**. This fork’s features run in the Mac preview and are built
into the firmware; the firmware screens are checked by host rendering tests and
have not yet been verified on the reader.

## Preview features

- **Muse avatar:** a large character image and short caption, updated by explicit Muse commands.
- **Watches:** a curated list with per-item check times and stale-data indicators.
- **What’s Next:** the next event, weekday, local countdown, and expiry.
- **Hours:** the Liturgy of the Hours is a daily cycle of Christian prayer at set times.
  Follow the active prayer hour and read Latin alongside English in a focused,
  scrollable view that works offline, independently of Muse. The preview includes
  Benedictine prayers for October 2–November 2, 2026, from Divinum Officium’s general
  Monastic – 1963 calendar; Clear Creek’s local variations and text alignment still
  need review. Modern Hours texts are not yet included, and unavailable dates or
  traditions are clearly marked.

## Screenshots

The Muse screenshot shows the running Mac preview with Geraldo’s delivered avatar
and current caption. The Hours screenshot uses the offline prayer pack. Watches
and What’s Next appear farther down the scrollable Muse view.

<p>
  <img src="docs/images/mac-muse-live-preview.png" width="320" alt="Live Muse preview showing Geraldo’s avatar and caption, compact hour timeline, and Watches and Next up tabs">
  <img src="docs/images/mac-hours-preview.png" width="320" alt="Hours preview with active-hour progress strip and Latin followed by English prayer text">
</p>

## Try it on a Mac

Clone this fork and [muse-gadget-macos](https://github.com/rjohnt/muse-gadget-macos)
next to each other. From this checkout:

```sh
uv venv .venv-preview --python 3.12
uv pip install --python .venv-preview/bin/python -e '../muse-gadget-macos[test]'
.venv-preview/bin/python tools/mac_preview/run.py preview
```

The offline preview needs no token, Bluetooth, or reader. For pairing and real Muse
updates, follow the [Mac preview guide](tools/mac_preview/README.md). The separate
Mac adapter owns CoreBluetooth pairing and the encrypted SDK session; this fork
owns the Pocket UI and commands.

```sh
.venv-preview/bin/python -m pytest tools/mac_preview
```

See [prayer-pack provenance and regeneration](tools/office/README.md). This is a
development snapshot, not a complete perpetual liturgical calendar or a ready-to-flash
Hours release.

## Existing X4 Pro firmware

The inherited firmware displays a Muse character and caption, with brightness,
warmth, orientation, refresh and sleep settings. Its upstream hardware validation
does not validate this fork’s new preview features.

You need an **X4 Pro**, **CrossPoint 1.6.5 for X4 Pro** in the recovery slot, a microSD
card, Wi-Fi, Muse Developer mode, your own SDK token, and **ESP-IDF 6.0.1**.
The X3 and ordinary X4 are not supported by this firmware.

1. [Build and privately add your token](docs/build.md).
2. [Install, pair and recover](docs/install.md) using CrossPoint’s **app-only SD updater**.
3. [Use the existing firmware commands](docs/usage.md).

Keep the bootloader, partition table and CrossPoint recovery slot intact. Remote
firmware updates remain disabled. A packaged firmware image contains your token:
keep it local and never upload it to GitHub or an issue.

## Development and privacy

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md). The included
[muse-pocket skill](skills/muse-pocket/SKILL.md) covers pinned builds and safe SD updates.
Source, public prayer data, and explicitly shared screenshots belong in commits.
SDK tokens, pairing state, private watch/event data and private firmware stay local.

## License and upstream

Source retains [Apache-2.0](LICENSE) and the notices in [NOTICE](NOTICE).
Divinum Officium-derived text retains its [MIT notice](tools/office/DIVINUM-LICENSE).
The SDK’s license-excluded Jollybot asset is not bundled. The README screenshot
includes the author’s own Muse avatar, shared as an illustration outside the source-code license.
This community fork is not an official Xteink or Meta product. Pairing also requires
the [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms).
