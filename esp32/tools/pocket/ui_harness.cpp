// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
// Host checks for the portable Pocket screens. Usage: ui_harness PACK [OUTDIR]
// With OUTDIR, each scene is also written as a PGM for visual review.
#include "../../main/office/ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace pocket_ui;
static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static std::vector<uint8_t> canvas(W * H);
static const char* outdir = nullptr;
static int ink() { int n = 0; for (uint8_t p : canvas) n += p != 255; return n; }
static int ink_between(int y0, int y1) { int n = 0; for (int i = y0 * W; i < y1 * W; ++i) n += canvas[i] != 255; return n; }
static void scene(const char* name, const State& s) {
    render(canvas.data(), s);
    if (!outdir) return;
    std::ofstream out(std::string(outdir) + "/" + name + ".pgm", std::ios::binary);
    out << "P5\n" << W << " " << H << "\n255\n";
    out.write(reinterpret_cast<const char*>(canvas.data()), canvas.size());
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (argc > 2) outdir = argv[2];
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
    tzset();
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> pack((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(pack.size() > 1000);
    set_pack(pack.data(), pack.size());

    int64_t t = 0;
    CHECK(parse_time("2026-10-03T14:14:00-05:00", &t) && t == 1791054840);
    CHECK(parse_time("2026-10-03T19:14:00Z", &t) && t == 1791054840);
    CHECK(parse_time("2026-10-03T19:14Z", &t) && t == 1791054840);
    CHECK(parse_time("2026-10-03T19:14:00.250+0000", &t) && t == 1791054840);
    CHECK(!parse_time("2026-10-03T19:14:00", &t));
    CHECK(!parse_time("2026-13-03T19:14:00Z", &t));
    CHECK(!parse_time("yesterday", &t));

    std::vector<uint8_t> avatar(W * AVATAR);
    for (int y = 0; y < AVATAR; ++y) for (int x = 0; x < W; ++x) {
        int dx = x - 240, dy = y - 240;
        avatar[y * W + x] = dx * dx + dy * dy < 180 * 180 ? 96 + (x + y) % 96 : 255;
    }
    Cards cards;
    State s;
    s.cards = &cards;
    s.name = "Geraldo";
    s.caption = "Running errands in the background. Ping me if anything needs you.";
    s.connection = "Connected to Muse";
    s.connected = true;
    s.battery = 87;
    s.avatar = avatar.data();
    s.clock_valid = true;
    parse_time("2026-10-03T14:14:00-05:00", &s.now);

    Hours h = hours(s);
    CHECK(h.valid && h.minute == 14 * 60 + 14 && h.active == 4 && h.reading == 4 && h.date == 20261003 && h.count == 8);
    scene("muse", s);
    CHECK(ink_between(AVATAR_Y, AVATAR_Y + AVATAR) > 20000);
    CHECK(hit(s, 440, 20).action == Action::Settings);
    CHECK(hit(s, 200, 66).action == Action::OpenHours);
    CHECK(hit(s, 40, 750).action == Action::Watches);
    CHECK(hit(s, 150, 750).action == Action::NextUp);
    CHECK(hit(s, 240, 300).action == Action::None);

    // Before Matins, yesterday's Compline is still being followed.
    State early = s;
    parse_time("2026-10-04T03:00:00-05:00", &early.now);
    h = hours(early);
    CHECK(h.active == 7 && h.date == 20261003);
    early.rite = 1;
    h = hours(early);
    CHECK(h.count == 7 && std::strcmp(hour_name(0, 1), "Readings") == 0);

    State unpaired;
    unpaired.cards = &cards;
    unpaired.caption = "Pair with Muse to get started";
    unpaired.connection = "Open Muse > Add gadget";
    scene("unpaired", unpaired);
    CHECK(!hours(unpaired).valid);

    s.view = View::Watches;
    scene("watches-empty", s);
    cards.has_watches = true;
    parse_time("2026-10-03T13:50:00-05:00", &cards.watches_updated);
    cards.watch_count = 7;
    for (int i = 0; i < 7; ++i) {
        std::snprintf(cards.watches[i].label, sizeof(cards.watches[i].label), "Watch number %d with a long label that must be clipped", i + 1);
        std::snprintf(cards.watches[i].state, sizeof(cards.watches[i].state), i % 2 ? "waiting" : "open");
        std::snprintf(cards.watches[i].note, sizeof(cards.watches[i].note), "A note that explains what is being watched and runs past the first line of the card so it wraps.");
        cards.watches[i].checked = cards.watches_updated - i * 3600;
    }
    CHECK(page_count(s) == 2);
    scene("watches", s);
    CHECK(hit(s, 400, 500).action == Action::NextPage);
    CHECK(hit(s, 40, 20).action == Action::Muse);
    CHECK(hit(s, 400, 20).action == Action::Muse);
    CHECK(hit(s, 440, 20).action == Action::Settings);
    s.page = 1;
    scene("watches-2", s);
    s.page = 0;

    s.view = View::NextUp;
    CHECK(!next_up_visible(s));
    scene("next-empty", s);
    cards.has_next = true;
    cards.next_updated = cards.watches_updated - 2 * 86400;
    parse_time("2026-10-03T16:30:00-05:00", &cards.when);
    parse_time("2026-10-03T17:30:00-05:00", &cards.ends);
    std::snprintf(cards.title, sizeof(cards.title), "Vespers with the community");
    std::snprintf(cards.detail, sizeof(cards.detail), "Abbey church. Bring the hymnal and arrive ten minutes early.");
    CHECK(next_up_visible(s));
    scene("next", s);
    State later = s;
    later.now = cards.ends + 3600;
    CHECK(!next_up_visible(later));

    s.view = View::Hours;
    int pages = page_count(s);
    CHECK(pages > 3);
    scene("hours", s);
    int first = ink_between(204, 756);
    CHECK(first > 5000);
    s.page = pages - 1;
    scene("hours-last", s);
    CHECK(ink_between(204, 756) > 100);
    s.page = pages + 50;  // out-of-range pages clamp instead of drawing nothing
    render(canvas.data(), s);
    CHECK(ink_between(204, 756) > 100);
    s.page = 1;
    scene("hours-2", s);
    s.page = 0;
    CHECK(hit(s, 440, 20).action == Action::Settings);
    CHECK(hit(s, 400, 20).action == Action::CloseHours);
    CHECK(hit(s, 30, 70).action == Action::PrevHour);
    CHECK(hit(s, 450, 70).action == Action::NextHour);
    Hit pick = hit(s, 80, 70);
    CHECK(pick.action == Action::SelectHour && pick.value == 0);
    pick = hit(s, 410, 70);
    CHECK(pick.action == Action::SelectHour && pick.value == 7);
    CHECK(hit(s, 430, 120).action == Action::Now);
    CHECK(hit(s, 60, 500).action == Action::PrevPage);
    CHECK(hit(s, 400, 500).action == Action::NextPage);

    // Every bundled office paginates and every page draws something.
    State sweep = s;
    sweep.following = false;
    int offices = 0, most = 0;
    for (int day = 0; day < 32; ++day) {
        sweep.now = s.now + static_cast<int64_t>(day - 1) * 86400;
        for (int hour = 0; hour < 8; ++hour) {
            sweep.reading_hour = hour;
            int count = page_count(sweep);
            CHECK(count >= 1);
            most = count > most ? count : most;
            sweep.page = count - 1;
            render(canvas.data(), sweep);
            CHECK(ink_between(204, 756) > 100);
            ++offices;
        }
    }
    std::printf("offices %d, longest %d pages\n", offices, most);

    // Larger reading text means more pages; smaller means fewer.
    State sized = s;
    sized.text_size = 0;
    int small_pages = page_count(sized);
    scene("hours-small", sized);
    sized.text_size = 2;
    int large_pages = page_count(sized);
    scene("hours-large", sized);
    CHECK(small_pages < pages && pages < large_pages);
    CHECK(ink_between(204, 756) > 5000);

    State held = s;
    held.following = false;
    held.reading_hour = 6;
    scene("hours-held", held);
    State modern = s;
    modern.rite = 1;
    CHECK(page_count(modern) == 1);
    scene("hours-modern", modern);
    State outside = s;
    parse_time("2027-01-10T09:00:00-06:00", &outside.now);
    CHECK(page_count(outside) == 1);
    scene("hours-outside", outside);
    State unset = unpaired;
    unset.view = View::Hours;
    scene("hours-noclock", unset);

    State menu = s;
    menu.view = View::Settings;
    const char* rows[] = {"Brightness: 25%", "Warmth: 50%", "Refresh: every 5s", "Orientation: normal", "Tradition: Benedictine",
                          "Clock: 24-hour", "Timezone: Central", "Sleep", "Return to CrossPoint", "Back to Muse"};
    menu.row_count = 10;
    for (int i = 0; i < 10; ++i) menu.rows[i] = rows[i];
    CHECK(page_count(menu) == 1);
    menu.selected = 8;
    scene("settings", menu);
    for (int i = 0; i < 10; ++i) {
        Hit row = hit(menu, 200, 96 + i * 56 + 10);
        CHECK(row.action == Action::Row && row.value == i);
    }
    CHECK(hit(menu, 200, 790).action == Action::None);

    State asleep = s;
    asleep.view = View::Sleeping;
    scene("sleeping", asleep);
    CHECK(hit(asleep, 440, 20).action == Action::None);
    CHECK(ink() > 0);
    std::printf(failures ? "%d checks failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
