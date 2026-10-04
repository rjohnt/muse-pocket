// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
// Portable Pocket screens: Muse, Watches, Next up, Hours and Settings. No
// hardware or RTOS dependencies, so the host tests render the same pixels.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pocket_ui {
constexpr int W = 480, H = 800, AVATAR = 480, AVATAR_Y = 88;
constexpr int MAX_WATCHES = 10, MAX_ROWS = 12, WATCHES_PER_PAGE = 5;
enum class View { Muse, Watches, NextUp, Hours, Settings, Sleeping };
struct Watch { char label[81]; char state[33]; char note[201]; int64_t checked; };
struct Cards {
    bool has_watches = false; int64_t watches_updated = 0; int watch_count = 0; Watch watches[MAX_WATCHES] = {};
    bool has_next = false; int64_t next_updated = 0, when = 0, ends = 0; char title[121] = ""; char detail[241] = "";
};
struct State {
    View view = View::Muse;
    const char* name = "Muse Pocket";
    const char* caption = "";
    const char* connection = "";
    bool connected = false;
    int battery = -1;
    const uint8_t* avatar = nullptr;  // AVATAR rows of W grey pixels, or null
    int64_t now = 0;                  // Unix time; local time comes from TZ
    bool clock_valid = false, h12 = false;
    int rite = 0;                     // 0 monastic, 1 modern
    int text_size = 1;                // Hours text: 0 small, 1 medium, 2 large
    const char* zone = "Central";
    bool following = true;
    int reading_hour = 0, page = 0;
    const char* rows[MAX_ROWS] = {};
    int row_count = 0, selected = 0;
    const Cards* cards = nullptr;
};
struct Hours {
    bool valid; int minute, active, reading; uint32_t date;  // date is yyyymmdd
    int count; int visible[8];
};
enum class Action { None, Settings, Muse, Watches, NextUp, OpenHours, CloseHours,
                    PrevHour, NextHour, SelectHour, Now, PrevPage, NextPage, Row };
struct Hit { Action action = Action::None; int value = 0; };

void set_pack(const uint8_t* data, size_t size);
const char* hour_name(int hour, int rite);
Hours hours(const State& state);
bool next_up_visible(const State& state);
int page_count(const State& state);
void render(uint8_t* canvas, const State& state);
Hit hit(const State& state, int x, int y);
// ISO 8601 with an explicit offset or Z, as the Pocket commands require.
bool parse_time(const char* iso, int64_t* out);
}
