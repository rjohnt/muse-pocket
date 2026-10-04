// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
#include "ui.h"
#include "pack.h"
#include "raster.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace pocket_ui {
using pocket_office::Face;
using pocket_office::Raster;
using pocket_office::font;
using pocket_office::measure;
using pocket_office::wrap;

namespace {
constexpr int LEFT = 24, RIGHT = W - 24, WIDE = RIGHT - LEFT;
constexpr int RULE_Y = 44, HUD_Y = 52, TABS_Y = 738, CARD_TABS_Y = 96;
constexpr int GEAR_X = 432, CLOSE_X = 392, ICON_Y = 11;
constexpr int STRIP_L = 58, STRIP_R = 422, BODY_TOP = 204, BODY_BOTTOM = 756, FOOTER_Y = 768;
constexpr int ROWS_Y = 96, ROW_PITCH = 56;
constexpr uint8_t GREY = 0xb0;
// Clear Creek's published ordinary schedule, minutes after local midnight.
constexpr int TIMES[8] = {315, 375, 480, 600, 770, 875, 1080, 1225};
const char* const NAMES[8] = {"Matins", "Lauds", "Prime", "Terce", "Sext", "None", "Vespers", "Compline"};
pocket_office::Pack pack;

std::string clock_text(int minute, bool h12) {
    char b[32];
    int h = minute / 60, m = minute % 60;
    if (h12) std::snprintf(b, sizeof(b), "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
    else std::snprintf(b, sizeof(b), "%02d:%02d", h, m);
    return b;
}
std::string date_text(int64_t when, bool h12) {
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    time_t t = static_cast<time_t>(when);
    struct tm lt = {};
    localtime_r(&t, &lt);
    char b[40];
    std::snprintf(b, sizeof(b), "%s, %s %d \xc2\xb7 ", days[lt.tm_wday % 7], months[lt.tm_mon % 12], lt.tm_mday);
    return b + clock_text(lt.tm_hour * 60 + lt.tm_min, h12);
}
std::string fit(std::string_view s, Face face, int available) {
    if (measure(s, face) <= available) return std::string(s);
    std::string out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t start = pos;
        pocket_office::next_codepoint(s, pos);
        std::string next = out + std::string(s.substr(start, pos - start));
        if (measure(next + "\xe2\x80\xa6", face) > available) break;
        out = next;
    }
    return out + "\xe2\x80\xa6";
}
std::string upper(std::string_view s) {
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned char c = out[i];
        if (c >= 'a' && c <= 'z') out[i] = c - 32;
        // Latin-1 lowercase letters are U+00E0..U+00FE, encoded C3 A0..BE.
        else if (c == 0xc3 && i + 1 < out.size()) {
            unsigned char d = out[i + 1];
            if (d >= 0xa0 && d <= 0xbe && d != 0xb7) out[i + 1] = d - 32;
            ++i;
        }
    }
    return out;
}
void gear(Raster& r, int x, int y) {
    for (int py = 0; py < 22; ++py) for (int px = 0; px < 22; ++px) {
        float dx = px - 10.5f, dy = py - 10.5f, d = std::sqrt(dx * dx + dy * dy);
        bool tooth = d >= 7.0f && d < 10.5f && std::cos(8 * std::atan2(dy, dx)) > 0.15f;
        bool ring = (d >= 6.0f && d < 8.0f) || (d >= 2.2f && d < 3.8f);
        if (tooth || ring) r.rect(x + px, y + py, 1, 1);
    }
}
void cross(Raster& r, int x, int y) {
    for (int i = 0; i < 16; ++i) { r.rect(x + i, y + i, 2, 2); r.rect(x + 15 - i, y + i, 2, 2); }
}
void header(Raster& r, const State& s, const char* label, bool closable) {
    r.text(label, LEFT, 12);
    gear(r, GEAR_X, ICON_Y);
    if (closable) cross(r, CLOSE_X + 3, ICON_Y + 3);
    if (s.battery >= 0) {
        char b[24];
        std::snprintf(b, sizeof(b), "%d%%", s.battery);
        r.right(b, (closable ? CLOSE_X : GEAR_X) - 14, 15, Face::Small);
    }
    r.rect(LEFT, RULE_Y, WIDE, 2);
}
void segment(Raster& r, int x, int y, int w, int index, int active, bool valid) {
    if (!valid) r.rect(x, y, w, 8, GREY);
    else if (index < active) r.hatch(x, y, w, 8);
    else if (index == active) r.rect(x, y, w, 8);
    else r.rect(x, y, w, 8, GREY);
}
void hud(Raster& r, const State& s, const Hours& h) {
    std::string name = h.valid ? hour_name(h.active, s.rite) : "Hours";
    std::string clock = h.valid ? clock_text(h.minute, s.h12) : "--:--";
    r.text(name, LEFT, HUD_Y);
    r.right(clock, RIGHT, HUD_Y);
    int x0 = LEFT + measure(name, Face::UI) + 16, x1 = RIGHT - measure(clock, Face::UI) - 16;
    int position = 0;
    for (int i = 0; i < h.count; ++i) if (h.visible[i] == h.active) position = i;
    for (int i = 0; i < h.count; ++i) {
        int a = x0 + (x1 - x0) * i / h.count, b = x0 + (x1 - x0) * (i + 1) / h.count;
        segment(r, a, HUD_Y + 9, b - a - 3, i, position, h.valid);
    }
    r.dots(LEFT, HUD_Y + 32, WIDE);
}
int tab_split() { return LEFT + measure("Watches", Face::UI) + 14; }
void tabs(Raster& r, int y, int selected) {
    int second = tab_split() + 14;
    r.text("Watches", LEFT, y);
    r.text("Next up", second, y);
    r.dots(LEFT, y + 32, WIDE);
    if (selected == 0) r.rect(LEFT, y + 30, measure("Watches", Face::UI), 3);
    if (selected == 1) r.rect(second, y + 30, measure("Next up", Face::UI), 3);
}
bool stale(const State& s, int64_t updated) { return s.clock_valid && s.now - updated >= 86400; }
void stamp(Raster& r, const State& s, int64_t updated, int y) {
    bool old = stale(s, updated);
    std::string label = std::string(old ? "Stale \xc2\xb7 " : "Updated \xc2\xb7 ") + date_text(updated, s.h12);
    r.text(label, LEFT + (old ? 8 : 0), y, Face::Small);
    if (!old) return;
    int w = measure(label, Face::Small) + 16;
    r.dots(LEFT, y - 5, w); r.dots(LEFT, y + 24, w);
    for (int row = y - 5; row < y + 25; row += 2) { r.rect(LEFT, row, 1, 1); r.rect(LEFT + w - 1, row, 1, 1); }
}
int paragraph(Raster& r, std::string_view s, int x, int y, Face face, int available, size_t limit) {
    auto lines = wrap(s, face, available);
    for (size_t i = 0; i < std::min(limit, lines.size()); ++i) {
        std::string line = i + 1 == limit && lines.size() > limit ? fit(lines[i] + " \xe2\x80\xa6", face, available) : lines[i];
        r.text(line, x, y, face);
        y += font(face).line_height;
    }
    return y;
}
std::string summary(const State& s) {
    const Cards* c = s.cards;
    if (!c || !c->has_watches) return "No watch digest received yet.";
    if (!c->watch_count) return "No open watches.";
    char b[64];
    std::snprintf(b, sizeof(b), "%d open watch%s", c->watch_count, c->watch_count == 1 ? "" : "es");
    return b;
}
void muse(Raster& r, uint8_t* canvas, const State& s, const Hours& h) {
    header(r, s, "Muse", false);
    hud(r, s, h);
    if (s.avatar) std::memcpy(canvas + AVATAR_Y * W, s.avatar, static_cast<size_t>(AVATAR) * W);
    std::string name = fit(s.name ? s.name : "", Face::Title, WIDE);
    r.centred(name, 566, Face::Title);
    auto lines = wrap(s.caption ? s.caption : "", Face::UI, WIDE);
    size_t shown = std::min<size_t>(s.connected ? 4 : 3, lines.size());
    for (size_t i = 0; i < shown; ++i) {
        std::string line = i + 1 == shown && lines.size() > shown ? fit(lines[i] + " \xe2\x80\xa6", Face::UI, WIDE) : lines[i];
        r.centred(line, 618 + static_cast<int>(i) * 25, Face::UI);
    }
    if (!s.connected) r.centred(fit(s.connection ? s.connection : "", Face::Small, WIDE), 702, Face::Small);
    tabs(r, TABS_Y, -1);
    r.text(fit(summary(s), Face::Small, WIDE), LEFT, TABS_Y + 38, Face::Small);
}
void watches(Raster& r, const State& s, const Hours& h) {
    header(r, s, "Muse", true);
    hud(r, s, h);
    tabs(r, CARD_TABS_Y, 0);
    const Cards* c = s.cards;
    if (!c || !c->has_watches) { r.text("No watch digest received yet.", LEFT, 150); return; }
    stamp(r, s, c->watches_updated, 144);
    if (!c->watch_count) { r.text("No open watches.", LEFT, 186); return; }
    int pages = page_count(s), page = std::clamp(s.page, 0, pages - 1), y = 182;
    for (int i = page * WATCHES_PER_PAGE; i < std::min(c->watch_count, (page + 1) * WATCHES_PER_PAGE); ++i) {
        const Watch& w = c->watches[i];
        int state = measure(w.state, Face::Small);
        r.text(fit(w.label, Face::UI, WIDE - state - 14), LEFT, y);
        r.right(w.state, RIGHT, y + 4, Face::Small);
        int after = paragraph(r, w.note, LEFT, y + 28, Face::Small, WIDE, 2);
        r.text("Checked " + date_text(w.checked, s.h12), LEFT, after + 3, Face::Small);
        r.dots(LEFT, y + 106, WIDE);
        y += 114;
    }
    if (pages > 1) {
        char b[32];
        std::snprintf(b, sizeof(b), "%d / %d", page + 1, pages);
        r.centred(b, FOOTER_Y, Face::Small);
    }
}
void next_up(Raster& r, const State& s, const Hours& h) {
    header(r, s, "Muse", true);
    hud(r, s, h);
    tabs(r, CARD_TABS_Y, 1);
    const Cards* c = s.cards;
    if (!next_up_visible(s)) { r.text("No upcoming event received yet.", LEFT, 150); return; }
    stamp(r, s, c->next_updated, 144);
    r.text(date_text(c->when, s.h12), LEFT, 190, Face::Small);
    int y = paragraph(r, c->title, LEFT, 216, Face::Title, WIDE, 3);
    y = paragraph(r, c->detail, LEFT, y + 10, Face::UI, WIDE, 8);
    if (!s.clock_valid) return;
    int64_t seconds = c->when - s.now;
    char b[48] = "Started";
    if (seconds > 0) std::snprintf(b, sizeof(b), "In %dh %dm", static_cast<int>(seconds / 3600), static_cast<int>(seconds % 3600 / 60));
    r.text(b, LEFT, y + 12);
}
// Flows one office down the reading area. Without a raster it counts pages;
// with one it draws `target` and stops once past it. Blocks may continue
// across a page break.
int flow(const pocket_office::Office& office, Raster* r, int target) {
    int page = 0, y = BODY_TOP;
    auto place = [&](std::string_view line, Face face, int reserve) {
        int h = font(face).line_height;
        if (y + h + reserve > BODY_BOTTOM && y > BODY_TOP) { ++page; y = BODY_TOP; }
        if (r && page > target) return false;
        if (r && page == target) r->text(line, LEFT, y, face);
        y += h;
        return true;
    };
    for (uint32_t i = 0; i < office.count; ++i) {
        pocket_office::Text t;
        if (!pack.text(office, i, t)) continue;
        if (t.kind == pocket_office::TextKind::Prayer || t.kind == pocket_office::TextKind::Phrase) {
            // Each Latin phrase sits directly above its own translation.
            auto latin = wrap(t.latin, Face::Latin, WIDE), english = wrap(t.english, Face::English, WIDE);
            // A phrase and its translation stay on one page when they can.
            int pair = static_cast<int>(latin.size()) * font(Face::Latin).line_height + static_cast<int>(english.size()) * font(Face::English).line_height;
            if (y + pair > BODY_BOTTOM && y > BODY_TOP && pair <= BODY_BOTTOM - BODY_TOP) { ++page; y = BODY_TOP; }
            for (auto& line : latin) if (!place(line, Face::Latin, 0)) return page;
            for (auto& line : english) if (!place(line, Face::English, 0)) return page;
            y += t.kind == pocket_office::TextKind::Phrase ? 9 : 22;
        } else {
            std::string label(t.english.empty() ? t.latin : t.english);
            bool heading = t.kind == pocket_office::TextKind::Heading;
            if (heading) { label = upper(label); if (y > BODY_TOP) y += 12; }
            // Keep a heading with the start of what follows it.
            for (auto& line : wrap(label, Face::Small, WIDE)) if (!place(line, Face::Small, heading ? 90 : 0)) return page;
            y += heading ? 12 : 14;
        }
    }
    return page + 1;
}
// Counting a long office walks all of its text, so remember the last answer.
int pages_of(const pocket_office::Office& office, uint32_t key) {
    static uint32_t cached_key = 0;
    static int cached = 1;
    if (key != cached_key) { cached = flow(office, nullptr, -1); cached_key = key; }
    return cached;
}
uint32_t office_key(const State& s, const Hours& h) { return (h.date * 16 + h.reading * 2 + (s.rite ? 1 : 0)) * 4 + s.text_size; }
void message(Raster& r, std::string_view text) {
    r.rect(LEFT, BODY_TOP + 4, 1, 120, 0);
    paragraph(r, text, LEFT + 14, BODY_TOP + 8, Face::UI, WIDE - 14, 5);
}
void office(Raster& r, const State& s, const Hours& h) {
    header(r, s, "Hours", true);
    r.text("\xe2\x86\x90", LEFT, 56);
    r.right("\xe2\x86\x92", RIGHT, 56);
    int position = 0;
    for (int i = 0; i < h.count; ++i) if (h.visible[i] == h.active) position = i;
    for (int i = 0; i < h.count; ++i) {
        int a = STRIP_L + (STRIP_R - STRIP_L) * i / h.count, b = STRIP_L + (STRIP_R - STRIP_L) * (i + 1) / h.count;
        segment(r, a + 4, 56, b - a - 8, i, position, h.valid);
        std::string label(hour_name(h.visible[i], s.rite), 3);
        r.text(label, a + (b - a - measure(label, Face::Small)) / 2, 68, Face::Small);
        if (h.visible[i] == h.reading) r.outline(a, 50, b - a, 44);
    }
    r.text(hour_name(h.reading, s.rite), LEFT, 98, Face::Title);
    r.right("Now", RIGHT, 114);
    if (s.following) r.rect(RIGHT - measure("Now", Face::UI), 142, measure("Now", Face::UI), 2);
    std::string status = s.following ? "Following the clock" : std::string("Reading held \xc2\xb7 ") + hour_name(h.active, s.rite) + " active now";
    status += std::string(" \xc2\xb7 ") + s.zone + " time";
    r.text(fit(status, Face::Small, WIDE), LEFT, 174, Face::Small);
    const char* tradition = s.rite == 0 ? "Benedictine" : "Modern Liturgy of the Hours";
    pocket_office::Office found;
    if (!h.valid) {
        r.text(tradition, LEFT, 152, Face::Small);
        message(r, "The clock is not set yet. Hours follows the date, so it needs Wi-Fi once to fetch the time.");
        return;
    }
    if (!pack.find(h.date, s.rite, h.reading, found)) {
        r.text(std::string(tradition) + " \xc2\xb7 Texts not loaded", LEFT, 152, Face::Small);
        char range[160];
        uint32_t a = pack.first_date(), b = pack.last_date();
        std::snprintf(range, sizeof(range), "This date/tradition is not in the offline prayer pack. The pack covers %04u-%02u-%02u to %04u-%02u-%02u.",
                      unsigned(a / 10000), unsigned(a / 100 % 100), unsigned(a % 100), unsigned(b / 10000), unsigned(b / 100 % 100), unsigned(b % 100));
        message(r, range);
        return;
    }
    r.text(fit(found.title, Face::Small, WIDE), LEFT, 152, Face::Small);
    int pages = pages_of(found, office_key(s, h)), page = std::clamp(s.page, 0, pages - 1);
    flow(found, &r, page);
    r.dots(LEFT, 762, WIDE);
    char b[32];
    std::snprintf(b, sizeof(b), "%d / %d", page + 1, pages);
    r.centred(b, FOOTER_Y, Face::Small);
    if (page > 0) r.text("\xe2\x80\xb9 Previous", LEFT, FOOTER_Y, Face::Small);
    if (page + 1 < pages) r.right("Next \xe2\x80\xba", RIGHT, FOOTER_Y, Face::Small);
}
void settings(Raster& r, const State& s) {
    header(r, s, "Settings", false);
    for (int i = 0; i < s.row_count; ++i) {
        int y = ROWS_Y + i * ROW_PITCH;
        if (i == s.selected) {
            r.rect(LEFT - 10, y - 14, WIDE + 20, 2); r.rect(LEFT - 10, y + 38, WIDE + 20, 2);
            r.text("\xe2\x80\xba", LEFT - 4, y);
        }
        r.text(fit(s.rows[i] ? s.rows[i] : "", Face::UI, WIDE - 24), LEFT + 22, y);
    }
    r.text("RIGHT: next row    POWER: change", LEFT, 760, Face::Small);
    r.text("Hold POWER on Return to restore CrossPoint", LEFT, 778, Face::Small);
}
}  // namespace

void set_pack(const uint8_t* data, size_t size) { pack.open(data, size); }
const char* hour_name(int hour, int rite) {
    hour = std::clamp(hour, 0, 7);
    return hour == 0 && rite == 1 ? "Readings" : NAMES[hour];
}
Hours hours(const State& s) {
    Hours h = {};
    for (int i = 0; i < 8; ++i) if (s.rite == 0 || i != 2) h.visible[h.count++] = i;
    h.valid = s.clock_valid;
    h.active = 7;
    if (h.valid) {
        time_t t = static_cast<time_t>(s.now);
        struct tm lt = {};
        localtime_r(&t, &lt);
        h.minute = lt.tm_hour * 60 + lt.tm_min;
        for (int i = 0; i < h.count; ++i) if (h.minute >= TIMES[h.visible[i]]) h.active = h.visible[i];
        // Before Matins the previous day's Compline is still the active hour.
        if (s.following && h.minute < TIMES[0]) { t -= 86400; localtime_r(&t, &lt); }
        h.date = (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
    }
    h.reading = s.following ? h.active : std::clamp(s.reading_hour, 0, 7);
    if (s.rite != 0 && h.reading == 2) h.reading = 1;
    return h;
}
bool next_up_visible(const State& s) {
    const Cards* c = s.cards;
    if (!c || !c->has_next || !c->title[0]) return false;
    return !(s.clock_valid && s.now - c->ends >= 3600);
}
int page_count(const State& s) {
    if (s.view == View::Watches) {
        int n = s.cards ? s.cards->watch_count : 0;
        return std::max(1, (n + WATCHES_PER_PAGE - 1) / WATCHES_PER_PAGE);
    }
    if (s.view != View::Hours) return 1;
    pocket_office::set_text_size(s.text_size);
    Hours h = hours(s);
    pocket_office::Office found;
    if (!h.valid || !pack.find(h.date, s.rite, h.reading, found)) return 1;
    return pages_of(found, office_key(s, h));
}
void render(uint8_t* canvas, const State& s) {
    Raster r(canvas);
    r.clear();
    pocket_office::set_text_size(s.text_size);
    Hours h = hours(s);
    switch (s.view) {
    case View::Muse: muse(r, canvas, s, h); break;
    case View::Watches: watches(r, s, h); break;
    case View::NextUp: next_up(r, s, h); break;
    case View::Hours: office(r, s, h); break;
    case View::Settings: settings(r, s); break;
    case View::Sleeping:
        header(r, s, "Muse", false);
        if (s.avatar) std::memcpy(canvas + AVATAR_Y * W, s.avatar, static_cast<size_t>(AVATAR) * W);
        r.centred("Sleeping", 580, Face::Title);
        r.centred("Press POWER to wake", 650);
        break;
    }
}
Hit hit(const State& s, int x, int y) {
    if (x < 0 || x >= W || y < 0 || y >= H) return {};
    if (s.view == View::Sleeping) return {};
    if (s.view == View::Settings) {
        if (y < RULE_Y + 8) return {Action::Muse, 0};
        int row = (y - (ROWS_Y - 14)) / ROW_PITCH;
        if (y >= ROWS_Y - 14 && row < s.row_count) return {Action::Row, row};
        return {};
    }
    if (y < RULE_Y + 4) {
        if (x >= GEAR_X - 10) return {Action::Settings, 0};
        if (s.view != View::Muse && x >= CLOSE_X - 12) return {s.view == View::Hours ? Action::CloseHours : Action::Muse, 0};
        if (x < 160 && s.view != View::Muse) return {s.view == View::Hours ? Action::CloseHours : Action::Muse, 0};
        return {};
    }
    if (s.view == View::Hours) {
        Hours h = hours(s);
        if (y < 96) {
            if (x < STRIP_L) return {Action::PrevHour, 0};
            if (x >= STRIP_R) return {Action::NextHour, 0};
            return {Action::SelectHour, h.visible[std::clamp((x - STRIP_L) * h.count / (STRIP_R - STRIP_L), 0, h.count - 1)]};
        }
        if (y < 150) return x >= 360 ? Hit{Action::Now, 0} : Hit{};
        if (y < BODY_TOP) return {};
        return {x < W / 3 ? Action::PrevPage : Action::NextPage, 0};
    }
    if (y < HUD_Y + 34) return {Action::OpenHours, 0};
    int tab_y = s.view == View::Muse ? TABS_Y : CARD_TABS_Y;
    if (y >= tab_y - 10 && y < tab_y + 40) return {x < tab_split() + 7 ? Action::Watches : Action::NextUp, 0};
    if (s.view == View::Muse) return y >= tab_y ? Hit{Action::Watches, 0} : Hit{};
    if (s.view == View::Watches && y >= 180) return {x < W / 3 ? Action::PrevPage : Action::NextPage, 0};
    return {};
}
bool parse_time(const char* iso, int64_t* out) {
    if (!iso || !out) return false;
    int y, mo, d, h, mi, s = 0, used = 0;
    if (std::sscanf(iso, "%4d-%2d-%2dT%2d:%2d%n", &y, &mo, &d, &h, &mi, &used) != 5 || used != 16) return false;
    const char* p = iso + used;
    if (*p == ':') {
        if (std::sscanf(p, ":%2d%n", &s, &used) != 1 || used != 3) return false;
        p += used;
        if (*p == '.') { ++p; while (*p >= '0' && *p <= '9') ++p; }
    }
    int offset = 0;
    if (*p == 'Z' && !p[1]) offset = 0;
    else if (*p == '+' || *p == '-') {
        int oh, om = 0;
        used = 0;
        if (std::sscanf(p + 1, "%2d:%2d%n", &oh, &om, &used) != 2 || used != 5 || p[1 + used]) {
            om = 0;
            if (std::sscanf(p + 1, "%2d%2d%n", &oh, &om, &used) != 2 || used != 4 || p[1 + used]) return false;
        }
        if (oh > 23 || om > 59) return false;
        offset = (oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
    } else return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60 || y < 1970) return false;
    // Days from civil date; valid across months, years and leap days.
    y -= mo <= 2;
    int era = y / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
    *out = days * 86400 + h * 3600 + mi * 60 + s - offset;
    return true;
}
}  // namespace pocket_ui
