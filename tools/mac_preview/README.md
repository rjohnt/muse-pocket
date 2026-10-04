# Pocket preview on macOS

This application uses the separate [Muse Gadget for macOS](https://github.com/rjohnt/muse-gadget-macos)
transport. Pocket features live here: a large Muse avatar, Watches, What’s Next,
and an Hours view opened from the compact timeline. Latin and English use distinct
typography; bilingual prayer sections remain together when their word order differs.

## Run from a development checkout

Use the current adapter checkout, which exposes `muse_mac.host.Application`.
Keep the two checkouts next to each other.

```sh
uv venv .venv-preview --python 3.12
uv pip install --python .venv-preview/bin/python -e '../muse-gadget-macos[test]'
(cd ../muse-gadget-macos && scripts/build-native.sh)
.venv-preview/bin/python tools/mac_preview/run.py preview
```

The offline preview requires no SDK token, Bluetooth, or Muse account. It starts
with empty Muse content. For live content, follow the adapter’s private-token and
pairing instructions, then run:

```sh
.venv-preview/bin/python tools/mac_preview/run.py run \
  --native-app '../muse-gadget-macos/build/Muse Mac Gadget.app'
```

Use `pair` instead of `run` for initial setup. Pairing state is shared with the
adapter’s default state directory; do not reset an existing working pairing.
`--request-avatar` and `--request-cards` are opt-in requests sent to your Muse.
Never use private content in public screenshots.

```sh
.venv-preview/bin/python -m pytest tools/mac_preview/test_preview.py
```

## Current limits

The Hours view reads a local [dated prayer pack](../office/README.md) without Muse.
The bundled pack covers October 2–November 2, 2026. It uses the general Monastic
1963 calendar, not verified Clear Creek local propers. Modern Hours text is not
bundled. Missing dates or rites must remain visibly unavailable.

This is a Mac application prototype. The firmware has its own implementation of
these screens; no Hours behavior on a physical reader has been verified.
