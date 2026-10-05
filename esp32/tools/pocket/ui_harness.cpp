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
constexpr int HUD_TOP = 48;
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
    // POCKET_AVATAR may name a raw 480x480 grey image for visual review.
    if (const char* path = getenv("POCKET_AVATAR")) {
        std::ifstream raw(path, std::ios::binary);
        raw.read(reinterpret_cast<char*>(avatar.data()), avatar.size());
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
    // The character is enlarged into the free space, and the name and caption
    // sit just above the tabs rather than under a fixed square.
    int widest = 0;
    for (int y = AVATAR_Y; y < 600; ++y) {
        int first = W, last = -1;
        for (int x = 0; x < W; ++x) if (canvas[y * W + x] != 255) { first = x < first ? x : first; last = x; }
        widest = last - first > widest ? last - first : widest;
    }
    if (!getenv("POCKET_AVATAR")) CHECK(widest > 420);
    CHECK(ink_between(622, 726) > 1500);
    CHECK(ink_between(726, 736) == 0);
    CHECK(hit(s, 440, 20).action == Action::Settings);
    CHECK(hit(s, 200, 66).action == Action::OpenHours);
    CHECK(hit(s, 40, 750).action == Action::Watches);
    CHECK(hit(s, 230, 750).action == Action::Watches);
    CHECK(hit(s, 250, 750).action == Action::NextUp);
    CHECK(hit(s, 450, 750).action == Action::NextUp);
    CHECK(hit(s, 240, 300).action == Action::None);

    // A faint shadow trailing to one side must not pull the body off centre.
    if (!getenv("POCKET_AVATAR")) {
        std::vector<uint8_t> lopsided(W * AVATAR, 255);
        for (int y = 0; y < AVATAR; ++y) for (int x = 0; x < W; ++x) {
            int dx = x - 300, dy = y - 220;
            if (dx * dx + dy * dy < 120 * 120) lopsided[y * W + x] = 40;
            else if (y > 350 && y < 362 && x > 20 && x < 300) lopsided[y * W + x] = 236;
        }
        State offset = s;
        offset.avatar = lopsided.data();
        scene("muse-shadow", offset);
        int64_t mass = 0, sum = 0;
        for (int y = AVATAR_Y; y < 600; ++y) for (int x = 0; x < W; ++x) if (canvas[y * W + x] < 128) { ++mass; sum += x; }
        CHECK(mass > 20000);
        int middle = static_cast<int>(sum / (mass ? mass : 1));
        CHECK(middle > W / 2 - 6 && middle < W / 2 + 6);
    }

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
    int first = ink_between(110, 756);
    CHECK(first > 5000);
    s.page = pages - 1;
    scene("hours-last", s);
    CHECK(ink_between(110, 756) > 100);
    s.page = pages + 50;  // out-of-range pages clamp instead of drawing nothing
    render(canvas.data(), s);
    CHECK(ink_between(110, 756) > 100);
    s.page = 1;
    scene("hours-2", s);
    s.page = 0;
    CHECK(hit(s, 440, 20).action == Action::Settings);
    CHECK(hit(s, 400, 20).action == Action::CloseHours);
    Hit pick = hit(s, 30, 70);
    CHECK(pick.action == Action::SelectHour && pick.value == 0);
    pick = hit(s, 400, 70);
    CHECK(pick.action == Action::SelectHour && pick.value == 7);
    CHECK(hit(s, 430, 120).action == Action::Now);
    // Later pages drop the title block, so the same spot turns the page.
    State onward = s;
    onward.page = 1;
    CHECK(hit(onward, 430, 120).action == Action::NextPage);
    CHECK(hit(onward, 80, 70).action == Action::SelectHour);
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
            CHECK(ink_between(110, 756) > 100);
            ++offices;
        }
    }
    std::printf("offices %d, longest %d pages\n", offices, most);

    // The beads at the end of the hour strip lead to the common prayers.
    CHECK(hit(s, 440, 70).action == Action::Prayers);
    CHECK(hit(s, 370, 70).action == Action::SelectHour);
    State praying = s;
    praying.view = View::Prayers;
    int listed = prayer_count();
    CHECK(listed == 4);
    scene("prayers", praying);
    CHECK(page_count(praying) == 1);
    for (int i = 0; i < listed; ++i) {
        Hit row = hit(praying, 200, 112 + i * 56 + 20);
        CHECK(row.action == Action::OpenPrayer && row.value == i);
    }
    CHECK(hit(praying, 400, 20).action == Action::OpenHours);
    CHECK(hit(praying, 100, 70).action == Action::SelectHour);
    for (int i = 0; i < listed; ++i) {
        praying.prayer = i;
        praying.page = 0;
        int count = page_count(praying);
        CHECK(count >= 1);
        std::string name = "prayer-" + std::to_string(i);
        scene(name.c_str(), praying);
        CHECK(ink_between(56, 756) > 3000);
        praying.page = count - 1;
        render(canvas.data(), praying);
        CHECK(ink_between(56, 756) > 300);
    }
    praying.prayer = 1;
    praying.page = 1;
    scene("prayer-1-page-2", praying);
    CHECK(hit(praying, 400, 20).action == Action::Prayers);
    CHECK(hit(praying, 400, 400).action == Action::NextPage);

    // Larger reading text means more pages; smaller means fewer.
    State sized = s;
    sized.text_size = 0;
    int small_pages = page_count(sized);
    scene("hours-small", sized);
    sized.text_size = 2;
    int large_pages = page_count(sized);
    scene("hours-large", sized);
    CHECK(small_pages < pages && pages < large_pages);
    CHECK(ink_between(110, 756) > 5000);

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
                          "Clock: 24-hour", "Timezone: Central", "Sleep", "Return to CrossPoint", "Back to Muse",
                          "Mode: Tabula", "Hours text: medium", "Screen kept: yes (spiffs 80K)"};
    menu.row_count = 13;
    for (int i = 0; i < 13; ++i) menu.rows[i] = rows[i];
    CHECK(page_count(menu) == 1);
    menu.selected = 8;
    scene("settings", menu);
    // Thirteen rows, as the reader has, end above the hints at the foot.
    CHECK(ink_between(90 + 12 * 46, 90 + 13 * 46 - 12) > 100);
    for (int i = 0; i < 13; ++i) {
        Hit row = hit(menu, 200, 90 + i * 46 + 10);
        CHECK(row.action == Action::Row && row.value == i);
    }
    CHECK(hit(menu, 200, 790).action == Action::None);

    // Tabula: the altar-card screen. POCKET_TABULA names its pack; the pixels
    // themselves are held to the reference composer by test_tabula.py.
    if (const char* path = getenv("POCKET_TABULA")) {
        std::ifstream tabula_file(path, std::ios::binary);
        static std::vector<uint8_t> tabula_pack((std::istreambuf_iterator<char>(tabula_file)), std::istreambuf_iterator<char>());
        CHECK(pocket_tabula::open(tabula_pack.data(), tabula_pack.size()));
        pocket_tabula::Status sources;
        sources.count = 2;
        std::snprintf(sources.sources[0].name, sizeof(sources.sources[0].name), "inbox");
        std::snprintf(sources.sources[0].state, sizeof(sources.sources[0].state), "clear");
        sources.sources[0].checked = s.now - 600;
        std::snprintf(sources.sources[1].name, sizeof(sources.sources[1].name), "backup");
        std::snprintf(sources.sources[1].state, sizeof(sources.sources[1].state), "ok");
        sources.sources[1].checked = s.now - 3 * 86400;
        State card = s;
        card.view = View::Tabula;
        card.tabula = &sources;
        // An hour held on the Hours screen must not hold Tabula back.
        card.following = false;
        card.reading_hour = 0;
        scene("tabula", card);
        CHECK(ink_between(14, 440) > 8000);    // the plate
        CHECK(ink_between(456, 490) > 300);    // day and Hour
        CHECK(ink_between(500, 762) > 3000);   // versicle and response
        CHECK(ink_between(770, 800) > 200);    // status
        int with_status = ink_between(764, 800);
        // The same Hour gives the same panel; the next Hour a different one.
        std::vector<uint8_t> first(canvas);
        State same = card;
        same.now += 600;
        sources.sources[0].checked += 600;
        render(canvas.data(), same);
        CHECK(first == canvas);
        State none = card;
        parse_time("2026-10-03T15:00:00-05:00", &none.now);
        render(canvas.data(), none);
        CHECK(first != canvas);
        card.tabula = nullptr;
        scene("tabula-no-status", card);
        CHECK(ink_between(764, 800) == 0 && with_status > 0);
        State dark = unpaired;
        dark.view = View::Tabula;
        scene("tabula-noclock", dark);
        CHECK(ink_between(14, 440) > 1000 && ink_between(500, 762) > 1000);
        CHECK(page_count(card) == 1);
        CHECK(hit(card, 460, 20).action == Action::Settings);
        CHECK(hit(card, 240, 400).action == Action::None);
        CHECK(hit(card, 240, 790).action == Action::None);
    }

    State asleep = s;
    asleep.view = View::Sleeping;
    scene("sleeping", asleep);
    // Asleep keeps the name and last caption, without the hour line or tabs.
    CHECK(ink_between(622, 726) > 1500);
    CHECK(ink_between(HUD_TOP, HUD_TOP + 30) == 0 || getenv("POCKET_AVATAR"));
    CHECK(hit(asleep, 440, 20).action == Action::None);
    CHECK(ink() > 0);
    std::printf(failures ? "%d checks failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
