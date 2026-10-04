# Display, settings and Muse commands

Muse Pocket is a companion screen. It receives a character image, caption,
watch list and next event through the Muse Gadget SDK's paired connection; it
does not expose a public web API or poll a separate activity feed. The Hours
view reads a dated prayer pack built into the firmware and needs no Muse.

## Screens

- **Muse:** the active prayer hour and clock, the character, its name and caption.
- **Watchlist** and **Next up:** the cards your Muse last sent, with the time they
  were updated. A card older than a day is marked stale; an event disappears an
  hour after it ends.
- **Hours:** the office for the active hour, Latin above English, one page at a
  time. Tap another hour on the strip to read it; **Now** returns to the clock.

- **Prayers:** common prayers (Angelus, the Rosary prayers, the Prayer of St
  Francis, a Sacred Heart prayer), opened from the beads at the end of the hour
  strip and read in the same way. In the list, **Right** moves the marker and
  **Power** opens the marked prayer; **Power** again returns to the list.

Tap the hour line to open Hours, a tab to open a card, the gear for Settings and
the cross to return to Muse. In Hours, tap the right of the text for the next page
and the left third for the previous one. Turning a page holds the hour you are
reading until you choose Now. The hour's title, with Now, is on its first page only.

Without touch, a short press of **Right** moves forward: Muse, Watchlist, Next up,
then each page of the current hour, then the prayer list. A short press of
**Left** steps back the same way: the previous page, list item or screen. A
short press of **Power** returns to Muse from the cards and Hours.

The clock is set from the network once Muse is connected, and keeps running
through sleep. Until then the hour line shows `--:--` and Hours asks for Wi-Fi.
The bundled pack covers October 2–November 2, 2026, general Monastic 1963
calendar; other dates and the modern tradition are shown as not loaded.

## Settings and controls

Hold **Right** for a second to open Settings, or tap the gear. Press Right or
Left to move through the rows and **Power** to change the selected setting.

| Setting | Choices |
| --- | --- |
| Brightness | Off, 5, 10, 25, 50, 75 or 100% |
| Warmth | Cool/warm balance |
| Refresh | 2, 5, 15 or 30 seconds |
| Orientation | Normal or flipped |
| Hours text | Small, medium or large Latin and English |
| Tradition | Benedictine; Modern has no bundled texts |
| Clock | 24-hour or 12-hour |
| Timezone | Central, UTC, Eastern, Mountain, Pacific, Rome or London |
| Sleep | Keep the character, name and last caption on screen and stop live updates |
| Return to CrossPoint | Hold Power for 3 seconds to return |
| Back to Muse | Return to the companion screen |

Settings are saved in the separate `muse_pocket` settings area. Power wakes a
sleeping reader. On the Muse screen, Power performs a clean refresh; holding it for 3 seconds sleeps the device.
Left confirms pairing or retries setup. Double-tapping Left rescans Wi-Fi;
holding it for 5 seconds resets Muse setup and forgets its Wi-Fi credentials.

Captions are batched according to the refresh setting. Image changes use a full
refresh; the display also performs a full refresh after ten incremental updates.

## Commands your Muse can call

| Command | Parameters | Result |
| --- | --- | --- |
| `display.draw_url` | `url`, optional `row` | Draw an image; keep the caption |
| `pocket.set_status` | `text`, up to 240 UTF-8 bytes | Set the caption below the character |
| `pocket.set_name` | `name`, up to 63 UTF-8 bytes | Set and save the name above the caption |
| `pocket.set_frontlight` | `brightness`, `warmth`, both 0–100 | Change and save the frontlight |
| `display.show_animation` | None | Return to the neutral placeholder icon |
| `pocket.set_watch_digest` | `payload`, a JSON string | Replace the watch list |
| `pocket.set_next_up` | `payload`, a JSON string | Replace the next event |
| `pocket.get_status` | None | Connection, accepted command count, last command |

The watch payload has `updated` and up to ten `items`, each with `label`,
`state`, `note` and `checked`. The event payload has `updated`, `title`, `when`,
`ends` and optional `detail`; an empty title clears it. Times are ISO 8601 with
an offset. A payload that does not match is refused and the previous card stays.
The reader keeps the last character, caption and cards in flash and shows them
straight after a restart or a wake, before Muse has reconnected; the cards keep
their own timestamps, so old ones are still marked stale. It borrows the
reader's spare data partition if that is blank, and otherwise the crash-dump
partition. The last row of Settings, **Screen kept**, says what is being kept
and where.

Use a **baseline JPEG** prepared for the **480×480 character canvas**, or
big-endian RGB565 data. Gray is converted to black and white with dithering.
An incomplete image download leaves the previous character visible.
Captions show up to four wrapped lines. Longer text can be accepted up to the
byte limit, but only the visible lines are drawn. Latin accents are drawn;
emoji and other scripts become `?`.

Example command parameters:

```json
{"text": "Reading your notes.\nNext: drafting a reply."}
```

```json
{"brightness": 25, "warmth": 50}
```

## Automatic character and status setup

After the paired session registers, the firmware sends one message to the Muse's
main chat, using the SDK's `/chat/stream` protocol. It asks the Muse to send its
own character through `display.draw_url` and update `pocket.set_status` when its
activity changes. A successfully accepted request is remembered for that boot
and Muse, so ordinary reconnects do not repeat it. Restarting asks again because
the current character is held in memory.

This is a request to the Muse, not a guaranteed activity subscription. The Muse
must execute the display commands and decide when its activity has changed.

## Troubleshooting

- **Connected, but no character:** ask your Muse to send its own character as a
  480×480 baseline JPEG using `display.draw_url`, then set the current activity
  with `pocket.set_status`. Ask it to tell you if either command fails.
- **Name still says Muse Pocket:** the paired identity carried no name. Ask your
  Muse to call `pocket.set_name` with its own name; the reader keeps it.
- **Caption stopped changing:** the Muse must send a new status command. It is
  not a timer that invents new activities.
- **Reconnecting:** check the chosen Wi-Fi network and the Muse service. Your
  previous image and caption remain visible during ordinary reconnects.
- **CrossPoint not verified:** follow the [recovery guide](install.md); do not
  weaken the pinned-image check to make the warning disappear.
- **Screen is blank or stuck:** the driver stops after an inconclusive panel
  probe or a busy timeout. Try the held-Right startup recovery path. Do not
  blindly flash a different model's pin map or partition table.

For a bug report, include the X4 Pro panel variant if known, source version, what
you pressed and a photo of the screen. Review logs before sharing. Never attach
a private `.bin`, SDK token, device token, Wi-Fi password or generated `sdkconfig`.
