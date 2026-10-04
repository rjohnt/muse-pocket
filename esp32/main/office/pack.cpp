// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
#include "pack.h"
#include <cstring>

namespace pocket_office {
namespace {
constexpr size_t header = 20, record = 20;
uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t u16(const uint8_t* p) { return p[0] | p[1] << 8; }
}
bool Pack::open(const uint8_t* data, size_t size) {
    data_ = nullptr;
    if (!data || size < header || std::memcmp(data, "MPO1", 4) != 0) return false;
    uint32_t texts = u32(data + 4), offices = u32(data + 8), texts_at = u32(data + 12), offices_at = u32(data + 16);
    if (texts_at > size || texts > (size - texts_at) / 4) return false;
    if (offices_at > size || offices > (size - offices_at) / record) return false;
    data_ = data; size_ = size; texts_ = texts; offices_ = offices; texts_at_ = texts_at; offices_at_ = offices_at;
    // Records are sorted by date; common prayers sit first, under date 0.
    first_ = last_ = 0;
    for (uint32_t i = 0; i < offices && !first_; ++i) first_ = u32(data + offices_at + record * i);
    if (first_) last_ = u32(data + offices_at + record * (offices - 1));
    return true;
}
bool Pack::find(uint32_t day, int rite, int hour, Office& out) const {
    if (!data_) return false;
    for (uint32_t i = 0; i < offices_; ++i) {
        const uint8_t* r = data_ + offices_at_ + record * i;
        if (u32(r) != day || r[4] != rite || r[5] != hour) continue;
        uint32_t title_len = u16(r + 6), title_at = u32(r + 8), count = u32(r + 12), blocks_at = u32(r + 16);
        if (title_at > size_ || title_len > size_ - title_at) return false;
        if (blocks_at > size_ || count > (size_ - blocks_at) / 2) return false;
        out.title = std::string_view(reinterpret_cast<const char*>(data_ + title_at), title_len);
        out.count = count; out.blocks = data_ + blocks_at;
        return true;
    }
    return false;
}
bool Pack::text(const Office& office, uint32_t index, Text& out) const {
    if (!data_ || index >= office.count) return false;
    uint32_t id = u16(office.blocks + 2 * index);
    if (id >= texts_) return false;
    uint32_t at = u32(data_ + texts_at_ + 4 * id);
    if (at > size_ || size_ - at < 5) return false;
    const uint8_t* p = data_ + at;
    size_t latin = u16(p + 1), english = u16(p + 3);
    if (latin + english > size_ - at - 5) return false;
    out.kind = p[0] <= 3 ? static_cast<TextKind>(p[0]) : TextKind::Note;
    out.latin = std::string_view(reinterpret_cast<const char*>(p + 5), latin);
    out.english = std::string_view(reinterpret_cast<const char*>(p + 5 + latin), english);
    return true;
}
}
