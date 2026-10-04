// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
// Read-only view of the dated prayer pack built by tools/office/build_firmware_pack.py.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pocket_office {
// A Phrase is part of a prayer with more of the same prayer following it.
enum class TextKind : uint8_t { Heading = 0, Prayer = 1, Note = 2, Phrase = 3 };
struct Text { TextKind kind; std::string_view latin, english; };
struct Office { std::string_view title; uint32_t count = 0; const uint8_t* blocks = nullptr; };
class Pack {
public:
    // The data must outlive the pack; nothing is copied.
    bool open(const uint8_t* data, size_t size);
    bool find(uint32_t yyyymmdd, int rite, int hour, Office& out) const;
    bool text(const Office& office, uint32_t index, Text& out) const;
    uint32_t first_date() const { return first_; }
    uint32_t last_date() const { return last_; }
private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    uint32_t texts_ = 0, offices_ = 0, texts_at_ = 0, offices_at_ = 0, first_ = 0, last_ = 0;
};
}
