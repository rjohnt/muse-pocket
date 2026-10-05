// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
// See tabula.h. This follows tools/tabula/compose.py line for line.
#include "tabula.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace pocket_tabula {
namespace {
constexpr int W = 480, H = 800, LEFT = 24, RIGHT = 456;
constexpr int PLATE_TOP = 14, PLATE_BOX = 424;
constexpr int RULE_Y = 450, DAY_Y = 461, DAY_RULE_Y = 492;
constexpr int VERSE_TOP = 500, VERSE_BOTTOM = 762, FOOT_RULE_Y = 768, FOOT_Y = 775;
// Faces by position in the pack's type.
constexpr int TEXT = 0, INITIAL = 3, CAPITALS = 6, SMALL = 7, SIZES = 3, FACES = 8;
constexpr int CAPITAL_SPACING = 4;
constexpr int64_t STALE_AFTER = 86400;
const char* const DAYS[7] = {"DOMINICA", "FERIA II", "FERIA III", "FERIA IV", "FERIA V", "FERIA VI", "SABBATO"};
const char* const HOURS[8] = {"AD MATUTINUM", "AD LAUDES", "AD PRIMAM", "AD TERTIAM", "AD SEXTAM", "AD NONAM",
                              "AD VESPERAS", "AD COMPLETORIUM"};
const char* const NO_CLOCK = "HORA IGNOTA";
const char* const VERSICLE = "\xe2\x84\xa3";
const char* const RESPONSE = "\xe2\x84\x9f";
const char* const ELLIPSIS = "\xe2\x80\xa6";
const char* const SEPARATOR = "  \xc2\xb7  ";

uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t u16(const uint8_t* p) { return p[0] | p[1] << 8; }

struct Glyph { const uint8_t* bits; int w, h, advance, x, y; };
struct Face { const uint8_t* glyphs = nullptr; const uint8_t* bits = nullptr; uint32_t count = 0; int line = 0; };
struct Plate { const uint8_t* rows; int w, h; };

const uint8_t* data_ = nullptr;
size_t size_ = 0;
Face faces[FACES];
uint32_t plates_at = 0, plate_count = 0, verses_at = 0, verse_count = 0;

uint32_t next(std::string_view s, size_t& pos) {
    unsigned char c = s[pos++];
    if (c < 0x80) return c;
    int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    uint32_t code = c & (0x3f >> more);
    while (more-- > 0 && pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80) code = code << 6 | (s[pos++] & 0x3f);
    return code;
}
bool find(const Face& f, uint32_t code, Glyph& out) {
    uint32_t lo = 0, hi = f.count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t* g = f.glyphs + 16 * mid;
        uint32_t at = u32(g);
        if (at == code) {
            out = {f.bits + u32(g + 4), g[8], g[9], g[10], static_cast<int8_t>(g[11]), static_cast<int8_t>(g[12])};
            return true;
        }
        if (at < code) lo = mid + 1; else hi = mid;
    }
    return false;
}
// The glyph for a character, or the question mark's where the face lacks it.
bool glyph(const Face& f, uint32_t code, Glyph& out) { return find(f, code, out) || find(f, '?', out); }

void rect(uint8_t* c, int x, int y, int w, int h, uint8_t colour = 0) {
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
    for (int row = y0; row < y1; ++row) if (x1 > x0) std::memset(c + row * W + x0, colour, x1 - x0);
}
// Packed ink; `stride` is the bits in a row, w or w padded to a byte.
void blit(uint8_t* c, const uint8_t* bits, int w, int h, int x, int y, int stride) {
    for (int row = 0; row < h; ++row) for (int col = 0; col < w; ++col) {
        int bit = row * stride + col;
        if (!(bits[bit / 8] & (0x80 >> (bit % 8)))) continue;
        int px = x + col, py = y + row;
        if (px >= 0 && px < W && py >= 0 && py < H) c[py * W + px] = 0;
    }
}
int text(uint8_t* c, const Face& f, std::string_view s, int x, int y, int spacing = 0) {
    for (size_t pos = 0; pos < s.size();) {
        Glyph g;
        if (!glyph(f, next(s, pos), g)) continue;
        blit(c, g.bits, g.w, g.h, x + g.x, y + g.y, g.w);
        x += g.advance + spacing;
    }
    return x;
}
int measure(const Face& f, std::string_view s, int spacing = 0) {
    int total = 0, count = 0;
    for (size_t pos = 0; pos < s.size();) {
        Glyph g;
        if (glyph(f, next(s, pos), g)) { total += g.advance; ++count; }
    }
    return total + spacing * std::max(0, count - 1);
}
// A dotted outline, the mark of stale information on the other screens.
void dotted(uint8_t* c, int x, int y, int w, int h) {
    for (int col = x; col < x + w; col += 2) { rect(c, col, y, 1, 1); rect(c, col, y + h - 1, 1, 1); }
    for (int row = y; row < y + h; row += 2) { rect(c, x, row, 1, 1); rect(c, x + w - 1, row, 1, 1); }
}
void diamond(uint8_t* c, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        int span = radius - std::abs(dy);
        rect(c, cx - span, cy + dy, 2 * span + 1, 1);
    }
}
// Greedy lines of whole words. The first two lines have `narrow` to fill, the
// rest `wide`: the versicle's first lines stand beside its initial.
std::vector<std::string> wrap(const Face& f, std::string_view s, int narrow, int wide) {
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find(' ', pos);
        if (end == std::string_view::npos) end = s.size();
        std::string word(s.substr(pos, end - pos));
        pos = end + 1;
        if (word.empty()) continue;
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(f, trial) > (lines.size() < 2 ? narrow : wide)) {
            lines.push_back(line);
            line = word;
        } else line = trial;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}
// The string, or as much of it as fits with an ellipsis.
std::string fit(const Face& f, const std::string& s, int width) {
    if (measure(f, s) <= width) return s;
    std::string out;
    for (size_t pos = 0; pos < s.size();) {
        size_t start = pos;
        next(s, pos);
        std::string longer = out + s.substr(start, pos - start);
        if (measure(f, longer + ELLIPSIS) > width) break;
        out = longer;
    }
    return out + ELLIPSIS;
}
// The capital to enlarge for a text's first letter, or 0. Accents are dropped.
uint32_t initial_of(std::string_view s, size_t& rest) {
    rest = 0;
    if (s.empty()) return 0;
    size_t pos = 0;
    uint32_t c = next(s, pos), base = 0;
    if (c >= 'A' && c <= 'Z') base = c;
    else if (c >= 'a' && c <= 'z') base = c - 32;
    else if (c == 0xc6 || c == 0xe6 || c == 0x1fc || c == 0x1fd) base = 0xc6;
    else if (c == 0x152 || c == 0x153) base = 0x152;
    else if (c >= 0xc0 && c <= 0xff && c != 0xd7 && c != 0xf7) {
        static const char folded[] = "AAAAAA\0CEEEEIIII\0NOOOOO\0\0UUUUY\0\0";
        base = static_cast<unsigned char>(folded[(c - 0xc0) % 32]);
        if (c == 0xff) base = 'Y';
    }
    if (!base) return 0;
    rest = pos;
    return base;
}
struct Layout {
    const Face* face; const Face* big;
    int line, column, box, box_width, beside, above, gap, height;
    uint32_t letter;
    std::vector<std::string> first, second;
};
Layout verse_layout(int size, std::string_view versicle, std::string_view response) {
    Layout l;
    l.face = &faces[TEXT + size]; l.big = &faces[INITIAL + size];
    l.line = l.face->line;
    l.column = LEFT + measure(*l.face, RESPONSE) + 8;
    size_t rest = 0;
    Glyph g;
    l.letter = initial_of(versicle, rest);
    if (l.letter && !find(*l.big, l.letter, g)) l.letter = 0;
    if (!l.letter) rest = 0;
    l.box = l.letter ? 2 * l.line : 0;
    // The box is square unless a wide capital needs more room.
    l.box_width = l.letter ? std::max(l.box - 6, g.w + 20) : 0;
    l.beside = l.letter ? l.column + l.box_width + 18 : l.column;
    l.first = wrap(*l.face, versicle.substr(rest), RIGHT - l.beside, RIGHT - l.column);
    l.second = wrap(*l.face, response, RIGHT - l.column, RIGHT - l.column);
    l.above = std::max(static_cast<int>(l.first.size()) * l.line, l.box);
    l.gap = l.line / 3;
    l.height = l.above + l.gap + static_cast<int>(l.second.size()) * l.line;
    return l;
}
// The capital in a ruled box, on a ground of small lozenges.
void draw_initial(uint8_t* c, const Face& big, uint32_t letter, int x, int y, int box, int across) {
    int top = y + 3, side = box - 6;
    rect(c, x, top, across, side);
    rect(c, x + 2, top + 2, across - 4, side - 4, 255);
    rect(c, x + 5, top + 5, across - 10, side - 10);
    rect(c, x + 6, top + 6, across - 12, side - 12, 255);
    for (int py = 10; py < side - 10; ++py) for (int px = 10; px < across - 10; ++px)
        if ((px + py) % 12 == 0 && (px - py) % 12 == 0) diamond(c, x + px, top + py, 1);
    Glyph g;
    if (!find(big, letter, g)) return;
    int gx = x + (across - g.w) / 2, gy = top + (side - g.h) / 2;
    // Clear a margin round the letter so it stands free of the ground.
    for (int row = 0; row < g.h; ++row) for (int col = 0; col < g.w; ++col) {
        int bit = row * g.w + col;
        if (!(g.bits[bit / 8] & (0x80 >> (bit % 8)))) continue;
        int x0 = std::max(x + 7, gx + col - 3), y0 = std::max(top + 7, gy + row - 3);
        rect(c, x0, y0, std::min(x + across - 7, gx + col + 4) - x0, std::min(top + side - 7, gy + row + 4) - y0, 255);
    }
    blit(c, g.bits, g.w, g.h, gx, gy, g.w);
}
void draw_verse(uint8_t* c, std::string_view versicle, std::string_view response) {
    int room = VERSE_BOTTOM - VERSE_TOP;
    Layout l;
    for (int size = 0; size < SIZES; ++size) {
        l = verse_layout(size, versicle, response);
        if (l.height <= room) break;
    }
    const Face& face = *l.face;
    int line = l.line;
    // Too long even at the smallest size: keep what fits and mark the cut.
    while (l.above + l.gap + static_cast<int>(l.second.size()) * line > room && l.second.size() > 1) {
        l.second.pop_back();
        l.second.back() = fit(face, l.second.back() + " " + ELLIPSIS, RIGHT - l.column);
    }
    int height = l.above + l.gap + static_cast<int>(l.second.size()) * line;
    int y = VERSE_TOP + std::max(0, (room - height) / 2);
    text(c, face, VERSICLE, LEFT, y);
    if (l.letter) draw_initial(c, *l.big, l.letter, l.column, y, l.box, l.box_width);
    for (int n = 0; n < static_cast<int>(l.first.size()); ++n) {
        if (y + (n + 1) * line > VERSE_BOTTOM) break;
        text(c, face, l.first[n], n < 2 ? l.beside : l.column, y + n * line);
    }
    y += l.above + l.gap;
    text(c, face, RESPONSE, LEFT, y);
    for (int n = 0; n < static_cast<int>(l.second.size()); ++n) {
        if (y + (n + 1) * line > VERSE_BOTTOM) break;
        text(c, face, l.second[n], l.column, y + n * line);
    }
}
bool is_stale(const Source& s, int64_t now) { return s.checked <= 0 || now - s.checked >= STALE_AFTER; }
// One line: each source's name and state, a dotted box round the stale ones.
void draw_status(uint8_t* c, const Status& status, int64_t now) {
    int count = std::clamp(status.count, 0, MAX_SOURCES);
    if (!count) return;
    const Face& face = faces[SMALL];
    int gap = measure(face, SEPARATOR), more_width = measure(face, std::string(" ") + ELLIPSIS);
    auto labels = [&](bool notes) {
        std::vector<std::string> out;
        for (int i = 0; i < count; ++i) {
            const Source& s = status.sources[i];
            std::string label = std::string(s.name) + " " + s.state;
            if (notes && s.note[0]) label += std::string(" (") + s.note + ")";
            out.push_back(label);
        }
        return out;
    };
    auto width = [&](const std::vector<std::string>& texts) {
        int total = gap * (static_cast<int>(texts.size()) - 1);
        for (size_t i = 0; i < texts.size(); ++i) total += measure(face, texts[i]) + (is_stale(status.sources[i], now) ? 10 : 0);
        return total;
    };
    std::vector<std::string> texts = labels(true);
    if (width(texts) > RIGHT - LEFT) texts = labels(false);
    // Still too wide: drop sources from the end and say so with an ellipsis.
    bool more = false;
    while (texts.size() > 1 && width(texts) + (more ? more_width : 0) > RIGHT - LEFT) { texts.pop_back(); more = true; }
    if (texts.size() == 1)
        texts[0] = fit(face, texts[0], RIGHT - LEFT - (is_stale(status.sources[0], now) ? 10 : 0) - (more ? more_width : 0));
    int total = width(texts) + (more ? more_width : 0);
    int x = LEFT + (RIGHT - LEFT - total) / 2;
    for (size_t n = 0; n < texts.size(); ++n) {
        if (n) x = text(c, face, SEPARATOR, x, FOOT_Y);
        if (is_stale(status.sources[n], now)) {
            int span = measure(face, texts[n]) + 10;
            dotted(c, x, FOOT_Y - 1, span, face.line + 3);
            text(c, face, texts[n], x + 5, FOOT_Y);
            x += span;
        } else x = text(c, face, texts[n], x, FOOT_Y);
    }
    if (more) text(c, face, std::string(" ") + ELLIPSIS, x, FOOT_Y);
}
// The plate for a day, or the fallback panel when the day has none.
bool plate(uint32_t month_day, Plate& out) {
    bool found = false;
    for (uint32_t i = 0; i < plate_count; ++i) {
        const uint8_t* r = data_ + plates_at + 12 * i;
        uint32_t day = u16(r);
        if (day != month_day && day != 0) continue;
        out = {data_ + u32(r + 8), u16(r + 2), u16(r + 4)};
        found = true;
        if (day == month_day) break;
    }
    return found;
}
// Versicle and response for an office, or the default for the Hour.
void verse(uint32_t date, int hour, std::string_view& versicle, std::string_view& response) {
    versicle = response = {};
    for (uint32_t i = 0; i < verse_count; ++i) {
        const uint8_t* r = data_ + verses_at + 20 * i;
        uint32_t day = u32(r);
        if (r[4] != hour || (day != 0 && day != date)) continue;
        versicle = std::string_view(reinterpret_cast<const char*>(data_ + u32(r + 8)), u16(r + 6));
        response = std::string_view(reinterpret_cast<const char*>(data_ + u32(r + 16)), u16(r + 12));
        if (day == date) break;
    }
}
bool inside(uint64_t at, uint64_t length) { return at <= size_ && length <= size_ - at; }
}  // namespace

bool open(const uint8_t* data, size_t size) {
    data_ = nullptr;
    if (!data || size < 28 || std::memcmp(data, "MPT1", 4) != 0) return false;
    size_ = size;
    uint32_t type_at = u32(data + 4), type_size = u32(data + 8);
    uint32_t p_at = u32(data + 12), p_count = u32(data + 16), v_at = u32(data + 20), v_count = u32(data + 24);
    if (!inside(type_at, type_size) || type_size < 8 + 16 * FACES || std::memcmp(data + type_at, "MTF1", 4) != 0) return false;
    if (u32(data + type_at + 4) < FACES) return false;
    if (!inside(p_at, 12ull * p_count) || !inside(v_at, 20ull * v_count)) return false;
    for (int i = 0; i < FACES; ++i) {
        const uint8_t* h = data + type_at + 8 + 16 * i;
        uint32_t count = u32(h), glyphs_at = u32(h + 4), bits_at = u32(h + 8);
        if (glyphs_at > type_size || 16ull * count > type_size - glyphs_at || bits_at > type_size) return false;
        const uint8_t* table = data + type_at + glyphs_at;
        for (uint32_t g = 0; g < count; ++g) {
            const uint8_t* r = table + 16 * g;
            if (static_cast<uint64_t>(bits_at) + u32(r + 4) + (r[8] * r[9] + 7) / 8 > type_size) return false;
            if (g && u32(r) <= u32(r - 16)) return false;
        }
        faces[i] = {table, data + type_at + bits_at, count, u16(h + 12)};
    }
    bool fallback = false;
    for (uint32_t i = 0; i < p_count; ++i) {
        const uint8_t* r = data + p_at + 12 * i;
        if (!inside(u32(r + 8), static_cast<uint64_t>((u16(r + 2) + 7) / 8) * u16(r + 4))) return false;
        fallback = fallback || u16(r) == 0;
    }
    for (uint32_t i = 0; i < v_count; ++i) {
        const uint8_t* r = data + v_at + 20 * i;
        if (!inside(u32(r + 8), u16(r + 6)) || !inside(u32(r + 16), u16(r + 12))) return false;
    }
    if (!fallback) return false;
    plates_at = p_at; plate_count = p_count; verses_at = v_at; verse_count = v_count;
    data_ = data;
    return true;
}
void render(uint8_t* c, uint32_t date, int weekday, int hour, bool valid, const Status* status, int64_t now) {
    std::memset(c, 255, W * H);
    if (!data_) return;
    hour = (hour % 8 + 8) % 8;
    Plate p;
    if (plate(valid ? date % 10000 : 0, p))
        blit(c, p.rows, p.w, p.h, (W - p.w) / 2, PLATE_TOP + (PLATE_BOX - p.h) / 2, (p.w + 7) / 8 * 8);
    // A double rule broken by a lozenge.
    for (int y : {RULE_Y, RULE_Y + 4}) {
        rect(c, LEFT, y, W / 2 - 14 - LEFT, 1);
        rect(c, W / 2 + 15, y, RIGHT - W / 2 - 15, 1);
    }
    diamond(c, W / 2, RULE_Y + 2, 7);
    diamond(c, W / 2 - 22, RULE_Y + 2, 2);
    diamond(c, W / 2 + 22, RULE_Y + 2, 2);
    const Face& capitals = faces[CAPITALS];
    std::string label = valid ? std::string(DAYS[(weekday % 7 + 7) % 7]) + " \xc2\xb7 " + HOURS[hour] : std::string(NO_CLOCK);
    text(c, capitals, label, (W - measure(capitals, label, CAPITAL_SPACING)) / 2, DAY_Y, CAPITAL_SPACING);
    rect(c, W / 2 - 60, DAY_RULE_Y, 121, 1);
    std::string_view versicle, response;
    verse(valid ? date : 0, hour, versicle, response);
    draw_verse(c, versicle, response);
    if (status && status->count > 0) {
        rect(c, LEFT, FOOT_RULE_Y, RIGHT - LEFT, 1);
        draw_status(c, *status, valid ? now : 0);
    }
}
}  // namespace pocket_tabula
