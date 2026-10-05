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
constexpr int STRIP_L = 24, STRIP_R = 414, BODY_TOP = 186, REST_TOP = 110, BODY_BOTTOM = 756, FOOTER_Y = 768;
// The beads at the end of the hour strip open the common prayers.
constexpr int BEADS_L = 420, BEADS_R = 456, PRAYER_TOP = 116, PRAYER_REST = 58, LIST_TOP = 112, LIST_PITCH = 56;
constexpr int PRAYERS_RITE = 2, MAX_PRAYERS = 11;
constexpr int ROWS_Y = 90, ROW_PITCH = 46;
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
// Two tabs share the full width evenly, each label centred in its half.
void tabs(Raster& r, int y, int selected) {
    const char* labels[] = {"Watchlist", "Next up"};
    for (int i = 0; i < 2; ++i) {
        int x0 = i * W / 2, width = measure(labels[i], Face::UI);
        r.text(labels[i], x0 + (W / 2 - width) / 2, y);
        if (i == selected) r.rect(i == 0 ? LEFT : W / 2, y + 30, W / 2 - LEFT, 3);
    }
    r.dots(LEFT, y + 32, WIDE);
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
    if (!c || !c->has_watches) return "No watchlist received yet.";
    if (!c->watch_count) return "Nothing on the watchlist.";
    char b[64];
    std::snprintf(b, sizeof(b), "%d on the watchlist", c->watch_count);
    return b;
}
// Draws the character as large as the space allows. The size comes from the
// extent of its solid ink, ignoring faint shadows and stray specks, and it is
// centred on its weight rather than its outline, so a shadow or an
// outstretched arm does not push the body to one side. Enlargement is capped
// so a small drawing does not turn to blocks.
void character(uint8_t* canvas, const uint8_t* avatar, int top, int bottom) {
    static int columns[W], rows[AVATAR];
    std::memset(columns, 0, sizeof(columns));
    std::memset(rows, 0, sizeof(rows));
    for (int y = 0; y < AVATAR; ++y) for (int x = 0; x < W; ++x) {
        int ink = 255 - avatar[y * W + x];
        if (ink > 11) { columns[x] += ink; rows[y] += ink; }
    }
    constexpr int SOLID = 600;  // about three black pixels in a line
    int x0 = 0, x1 = W - 1, y0 = 0, y1 = AVATAR - 1;
    while (x0 < W && columns[x0] < SOLID) ++x0;
    while (x1 > x0 && columns[x1] < SOLID) --x1;
    while (y0 < AVATAR && rows[y0] < SOLID) ++y0;
    while (y1 > y0 && rows[y1] < SOLID) --y1;
    int area = bottom - top;
    if (x0 >= W || y0 >= AVATAR || area < 16) return;
    int sw = x1 - x0 + 1, sh = y1 - y0 + 1;
    int64_t weight = 0, moment = 0;
    for (int x = x0; x <= x1; ++x) { weight += columns[x]; moment += static_cast<int64_t>(columns[x]) * x; }
    // Fixed point, 1/256: destination pixels per source pixel.
    int scale = std::min({(W - 16) * 256 / sw, area * 256 / sh, 448});
    int dw = sw * scale / 256, dh = sh * scale / 256;
    // Left edge of the solid ink on screen: centre of weight in the middle,
    // moved only as far as needed to keep the whole body on the screen.
    int centre = static_cast<int>(moment / weight);
    int left = W / 2 - (centre - x0) * scale / 256;
    left = std::clamp(left, std::min(8, W - 8 - dw), std::max(8, W - 8 - dw));
    int start = top + (area - dh) / 2;
    // Everything in the picture is drawn, including what lies outside the
    // solid ink, as long as it lands inside the character's area.
    for (int y = top; y < bottom; ++y) {
        int fy = (y - start) * 65536 / scale + y0 * 256;
        if (fy < 0 || fy >= (AVATAR - 1) * 256) continue;
        int sy = fy / 256, wy = fy % 256;
        const uint8_t* a = avatar + sy * W;
        const uint8_t* b = a + W;
        uint8_t* out = canvas + y * W;
        for (int x = 0; x < W; ++x) {
            int fx = (x - left) * 65536 / scale + x0 * 256;
            if (fx < 0 || fx >= (W - 1) * 256) continue;
            int sx = fx / 256, wx = fx % 256;
            int upper = a[sx] * (256 - wx) + a[sx + 1] * wx, lower = b[sx] * (256 - wx) + b[sx + 1] * wx;
            out[x] = static_cast<uint8_t>((upper * (256 - wy) + lower * wy) >> 16);
        }
    }
}
void muse(Raster& r, uint8_t* canvas, const State& s, const Hours& h) {
    // Asleep, the screen keeps the name and last caption but drops what would
    // go stale while it sits there: the clock, the battery and the tabs.
    bool asleep = s.view == View::Sleeping;
    if (asleep) { r.text("Muse", LEFT, 12); r.rect(LEFT, RULE_Y, WIDE, 2); }
    else { header(r, s, "Muse", false); hud(r, s, h); }
    // The name and caption sit directly above the tabs; the character takes
    // whatever is left above them.
    auto lines = wrap(s.caption ? s.caption : "", Face::UI, WIDE);
    int shown = static_cast<int>(std::min<size_t>(4, lines.size()));
    int floor_y = TABS_Y - 12;
    if (!s.connected && !asleep) {
        floor_y -= 22;
        r.centred(fit(s.connection ? s.connection : "", Face::Small, WIDE), floor_y, Face::Small);
        floor_y -= 4;
    }
    int caption_y = floor_y - shown * 25, name_y = caption_y - 50;
    for (int i = 0; i < shown; ++i) {
        std::string line = i + 1 == shown && lines.size() > static_cast<size_t>(shown) ? fit(lines[i] + " \xe2\x80\xa6", Face::UI, WIDE) : lines[i];
        r.centred(line, caption_y + i * 25, Face::UI);
    }
    r.centred(fit(s.name ? s.name : "", Face::Title, WIDE), name_y, Face::Title);
    if (s.avatar) character(canvas, s.avatar, asleep ? RULE_Y + 10 : AVATAR_Y + 4, name_y - 2);
    if (asleep) {
        r.dots(LEFT, TABS_Y - 4, WIDE);
        std::string since = s.clock_valid ? "Asleep since " + date_text(s.now, s.h12) : std::string("Asleep");
        r.centred(since, TABS_Y + 8, Face::Small);
        r.centred("Press POWER to wake", TABS_Y + 30, Face::Small);
        return;
    }
    tabs(r, TABS_Y, -1);
    r.text(fit(summary(s), Face::Small, WIDE), LEFT, TABS_Y + 38, Face::Small);
}
void watches(Raster& r, const State& s, const Hours& h) {
    header(r, s, "Muse", true);
    hud(r, s, h);
    tabs(r, CARD_TABS_Y, 0);
    const Cards* c = s.cards;
    if (!c || !c->has_watches) { r.text("No watchlist received yet.", LEFT, 150); return; }
    stamp(r, s, c->watches_updated, 144);
    if (!c->watch_count) { r.text("Nothing on the watchlist.", LEFT, 186); return; }
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
int flow(const pocket_office::Office& office, Raster* r, int target, int first_top = BODY_TOP, int rest_top = REST_TOP) {
    // Only the first page carries the title, so later pages start higher.
    int page = 0, y = first_top;
    auto top = [&] { return page == 0 ? first_top : rest_top; };
    auto place = [&](std::string_view line, Face face, int reserve) {
        int h = font(face).line_height;
        if (y + h + reserve > BODY_BOTTOM && y > top()) { ++page; y = rest_top; }
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
            // A prayer with no Latin is set in the larger face itself.
            Face second = t.latin.empty() ? Face::Latin : Face::English;
            auto latin = wrap(t.latin, Face::Latin, WIDE), english = wrap(t.english, second, WIDE);
            // A phrase and its translation stay on one page when they can.
            int pair = static_cast<int>(latin.size()) * font(Face::Latin).line_height + static_cast<int>(english.size()) * font(second).line_height;
            if (y + pair > BODY_BOTTOM && y > top() && pair <= BODY_BOTTOM - BODY_TOP) { ++page; y = rest_top; }
            for (auto& line : latin) if (!place(line, Face::Latin, 0)) return page;
            for (auto& line : english) if (!place(line, second, 0)) return page;
            y += t.kind == pocket_office::TextKind::Phrase ? 9 : 22;
        } else {
            std::string label(t.english.empty() ? t.latin : t.english);
            bool heading = t.kind == pocket_office::TextKind::Heading;
            if (heading) { label = upper(label); if (y > top()) y += 12; }
            // Keep a heading with the start of what follows it.
            for (auto& line : wrap(label, Face::Small, WIDE)) if (!place(line, Face::Small, heading ? 90 : 0)) return page;
            y += heading ? 12 : 14;
        }
    }
    return page + 1;
}
// Counting a long office walks all of its text, so remember the last answer.
int pages_of(const pocket_office::Office& office, uint32_t key, int first_top = BODY_TOP, int rest_top = REST_TOP) {
    static uint32_t cached_key = 0;
    static int cached = 1;
    if (key != cached_key) { cached = flow(office, nullptr, -1, first_top, rest_top); cached_key = key; }
    return cached;
}
uint32_t prayer_key(const State& s) { return 0x80000000u | static_cast<uint32_t>(s.prayer * 4 + s.text_size); }
// A ring of beads with a small cross hanging from it.
void beads(Raster& r, int cx, int cy) {
    for (int i = 0; i < 10; ++i) {
        float angle = 6.2832f * i / 10 - 1.5708f;
        r.rect(cx + static_cast<int>(std::lround(9 * std::cos(angle))) - 1, cy + static_cast<int>(std::lround(9 * std::sin(angle))) - 1, 3, 3);
    }
    r.rect(cx, cy + 11, 2, 12);
    r.rect(cx - 3, cy + 14, 8, 2);
}
void footer(Raster& r, int page, int pages) {
    r.dots(LEFT, 762, WIDE);
    char b[32];
    std::snprintf(b, sizeof(b), "%d / %d", page + 1, pages);
    r.centred(b, FOOTER_Y, Face::Small);
    if (page > 0) r.text("\xe2\x80\xb9 Previous", LEFT, FOOTER_Y, Face::Small);
    if (page + 1 < pages) r.right("Next \xe2\x80\xba", RIGHT, FOOTER_Y, Face::Small);
}
// The day's hours as a strip, ending in the beads that open the prayers.
void strip(Raster& r, const State& s, const Hours& h, bool prayers) {
    int position = 0;
    for (int i = 0; i < h.count; ++i) if (h.visible[i] == h.active) position = i;
    for (int i = 0; i < h.count; ++i) {
        int a = STRIP_L + (STRIP_R - STRIP_L) * i / h.count, b = STRIP_L + (STRIP_R - STRIP_L) * (i + 1) / h.count;
        segment(r, a + 3, 56, b - a - 6, i, position, h.valid);
        std::string label(hour_name(h.visible[i], s.rite), 3);
        r.text(label, a + (b - a - measure(label, Face::Small)) / 2, 68, Face::Small);
        if (!prayers && h.visible[i] == h.reading) r.outline(a, 50, b - a, 44);
    }
    beads(r, (BEADS_L + BEADS_R) / 2, 63);
    if (prayers) r.outline(BEADS_L, 50, BEADS_R - BEADS_L, 44);
}
uint32_t office_key(const State& s, const Hours& h) { return (h.date * 16 + h.reading * 2 + (s.rite ? 1 : 0)) * 4 + s.text_size; }
void message(Raster& r, std::string_view text) {
    r.rect(LEFT, BODY_TOP + 4, 1, 120, 0);
    paragraph(r, text, LEFT + 14, BODY_TOP + 8, Face::UI, WIDE - 14, 5);
}
void office(Raster& r, const State& s, const Hours& h) {
    header(r, s, "Hours", true);
    strip(r, s, h, false);
    // The strip above already shows which hour is active; the title block
    // names the hour being read and appears on its first page only.
    auto title = [&](std::string_view profile) {
        r.text(hour_name(h.reading, s.rite), LEFT, 98, Face::Title);
        r.right("Now", RIGHT, 114);
        if (s.following) r.rect(RIGHT - measure("Now", Face::UI), 142, measure("Now", Face::UI), 2);
        r.text(fit(profile, Face::Small, WIDE), LEFT, 152, Face::Small);
    };
    const char* tradition = s.rite == 0 ? "Benedictine" : "Modern Liturgy of the Hours";
    pocket_office::Office found;
    if (!h.valid) {
        title(tradition);
        message(r, "The clock is not set yet. Hours follows the date, so it needs Wi-Fi once to fetch the time.");
        return;
    }
    if (!pack.find(h.date, s.rite, h.reading, found)) {
        title(std::string(tradition) + " \xc2\xb7 Texts not loaded");
        char range[160];
        uint32_t a = pack.first_date(), b = pack.last_date();
        std::snprintf(range, sizeof(range), "This date/tradition is not in the offline prayer pack. The pack covers %04u-%02u-%02u to %04u-%02u-%02u.",
                      unsigned(a / 10000), unsigned(a / 100 % 100), unsigned(a % 100), unsigned(b / 10000), unsigned(b / 100 % 100), unsigned(b % 100));
        message(r, range);
        return;
    }
    int pages = pages_of(found, office_key(s, h)), page = std::clamp(s.page, 0, pages - 1);
    if (page == 0) title(found.title);
    flow(found, &r, page);
    footer(r, page, pages);
}
// Common prayers: a list reached from the beads on the hour strip, then the
// chosen prayer in the same reader as the hours.
void prayers(Raster& r, const State& s, const Hours& h) {
    header(r, s, "Prayers", true);
    pocket_office::Office found;
    if (s.prayer < 0) {
        strip(r, s, h, true);
        int count = prayer_count();
        for (int i = 0; i < count; ++i) {
            if (!pack.find(0, PRAYERS_RITE, i, found)) continue;
            int y = LIST_TOP + i * LIST_PITCH;
            if (i == s.prayer_selected) r.text("\xe2\x80\xba", LEFT, y + 6, Face::Latin);
            r.text(fit(found.title, Face::Latin, WIDE - 26), LEFT + 26, y + 6, Face::Latin);
            r.dots(LEFT, y + LIST_PITCH - 6, WIDE);
        }
        if (!count) r.text("No prayers are bundled in this build.", LEFT, LIST_TOP);
        r.centred("RIGHT: next    LEFT: previous    POWER: open", FOOTER_Y, Face::Small);
        return;
    }
    if (!pack.find(0, PRAYERS_RITE, s.prayer, found)) return;
    int pages = pages_of(found, prayer_key(s), PRAYER_TOP, PRAYER_REST), page = std::clamp(s.page, 0, pages - 1);
    if (page == 0) r.text(fit(found.title, Face::Title, WIDE), LEFT, 56, Face::Title);
    flow(found, &r, page, PRAYER_TOP, PRAYER_REST);
    footer(r, page, pages);
}
void settings(Raster& r, const State& s) {
    header(r, s, "Settings", false);
    for (int i = 0; i < s.row_count; ++i) {
        int y = ROWS_Y + i * ROW_PITCH;
        if (i == s.selected) {
            r.rect(LEFT - 10, y - 10, WIDE + 20, 2); r.rect(LEFT - 10, y + 32, WIDE + 20, 2);
            r.text("\xe2\x80\xba", LEFT - 4, y);
        }
        r.text(fit(s.rows[i] ? s.rows[i] : "", Face::UI, WIDE - 24), LEFT + 22, y);
    }
    r.text("RIGHT: next row    POWER: change", LEFT, 762, Face::Small);
    r.text("Hold POWER on Return to restore CrossPoint", LEFT, 778, Face::Small);
}
// Tabula follows the clock like the Muse screen's hour line, whatever hour
// was last being read, and always in the monastic order of eight Hours.
void tabula(uint8_t* canvas, const State& s) {
    State now = s;
    now.following = true;
    now.rite = 0;
    Hours h = hours(now);
    int y = h.date / 10000, m = h.date / 100 % 100, d = h.date % 100;
    // Sakamoto's day of the week, 0 for Sunday.
    static const int shift[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int weekday = 0;
    if (h.valid) { int year = y - (m < 3); weekday = (year + year / 4 - year / 100 + year / 400 + shift[m - 1] + d) % 7; }
    pocket_tabula::render(canvas, h.date, weekday, h.active, h.valid, s.tabula, s.now);
}
}  // namespace

void set_pack(const uint8_t* data, size_t size) { pack.open(data, size); }
int prayer_count() {
    pocket_office::Office found;
    int count = 0;
    while (count < MAX_PRAYERS && pack.find(0, PRAYERS_RITE, count, found)) ++count;
    return count;
}
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
    pocket_office::set_text_size(s.text_size);
    if (s.view == View::Prayers) {
        pocket_office::Office prayer;
        if (s.prayer < 0 || !pack.find(0, PRAYERS_RITE, s.prayer, prayer)) return 1;
        return pages_of(prayer, prayer_key(s), PRAYER_TOP, PRAYER_REST);
    }
    if (s.view != View::Hours) return 1;
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
    case View::Prayers: prayers(r, s, h); break;
    case View::Settings: settings(r, s); break;
    case View::Sleeping: muse(r, canvas, s, h); break;
    case View::Tabula: tabula(canvas, s); break;
    }
}
Hit hit(const State& s, int x, int y) {
    if (x < 0 || x >= W || y < 0 || y >= H) return {};
    if (s.view == View::Sleeping) return {};
    // Tabula has no controls drawn on it; its top right corner opens Settings.
    if (s.view == View::Tabula) return x >= GEAR_X - 30 && y < 80 ? Hit{Action::Settings, 0} : Hit{};
    if (s.view == View::Settings) {
        if (y < RULE_Y + 8) return {Action::Muse, 0};
        int row = (y - (ROWS_Y - 10)) / ROW_PITCH;
        if (y >= ROWS_Y - 10 && row < s.row_count) return {Action::Row, row};
        return {};
    }
    if (y < RULE_Y + 4) {
        if (x >= GEAR_X - 10) return {Action::Settings, 0};
        // The cross steps back one level: a prayer to the list, the list to Hours.
        Action back = s.view == View::Hours ? Action::CloseHours
            : s.view == View::Prayers ? (s.prayer < 0 ? Action::OpenHours : Action::Prayers) : Action::Muse;
        if (s.view != View::Muse && (x >= CLOSE_X - 12 || x < 160)) return {back, 0};
        return {};
    }
    if (s.view == View::Prayers) {
        if (s.prayer >= 0) return {x < W / 3 ? Action::PrevPage : Action::NextPage, 0};
        if (y < 96) {
            if (x >= STRIP_R) return {};
            Hours h = hours(s);
            return {Action::SelectHour, h.visible[std::clamp((x - STRIP_L) * h.count / (STRIP_R - STRIP_L), 0, h.count - 1)]};
        }
        int row = (y - (LIST_TOP - 6)) / LIST_PITCH;
        if (y >= LIST_TOP - 6 && row < prayer_count()) return {Action::OpenPrayer, row};
        return {};
    }
    if (s.view == View::Hours) {
        Hours h = hours(s);
        if (y < 96) {
            // Tap an hour to read it; the beads open the prayers.
            if (x >= STRIP_R) return {Action::Prayers, 0};
            return {Action::SelectHour, h.visible[std::clamp((x - STRIP_L) * h.count / (STRIP_R - STRIP_L), 0, h.count - 1)]};
        }
        // The title block, with Now, is on the first page only.
        if (s.page == 0 && y < 150) return x >= 360 ? Hit{Action::Now, 0} : Hit{};
        if (s.page == 0 && y < BODY_TOP) return {};
        return {x < W / 3 ? Action::PrevPage : Action::NextPage, 0};
    }
    if (y < HUD_Y + 34) return {Action::OpenHours, 0};
    int tab_y = s.view == View::Muse ? TABS_Y : CARD_TABS_Y;
    if (y >= tab_y - 10 && y < tab_y + 40) return {x < W / 2 ? Action::Watches : Action::NextUp, 0};
    if (s.view == View::Muse) return y >= tab_y ? Hit{x < W / 2 ? Action::Watches : Action::NextUp, 0} : Hit{};
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
