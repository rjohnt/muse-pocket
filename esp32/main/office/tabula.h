// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
// Tabula: an altar-card screen for a reader left standing on a desk. The day's
// saint by Callot, the day and Hour, the Hour's versicle and response in Latin
// and one line of agent status. Portable, so the host tests draw it too.
//
// tools/tabula/compose.py is the reference for every pixel drawn here; the
// tests compare the two. Change them together.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pocket_tabula {
constexpr int MAX_SOURCES = 6;
// One agent or service the owner follows. `checked` is Unix time, 0 if it has
// never reported.
struct Source { char name[25]; char state[17]; char note[81]; int64_t checked; };
struct Status { int count = 0; Source sources[MAX_SOURCES] = {}; };

// The pack built by tools/tabula/pack.py. The data must outlive its use;
// nothing is copied. Returns false, and draws nothing later, if it is malformed.
bool open(const uint8_t* data, size_t size);
// `canvas` is 480x800 grey pixels. `date` is the office's yyyymmdd, `weekday`
// 0 for Sunday, `hour` 0 (Matins) to 7 (Compline). With `valid` false the
// clock is not set. `now` is Unix time, for marking stale status.
void render(uint8_t* canvas, uint32_t date, int weekday, int hour, bool valid, const Status* status, int64_t now);
}
