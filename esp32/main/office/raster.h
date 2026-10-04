// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pocket_office {
constexpr int width = 480, height = 800;
enum class Face { Latin, English, UI, Title, Small };
struct Glyph { uint32_t code; uint32_t offset; uint8_t w,h,advance; int8_t x,y; };
struct Font { const Glyph* glyphs; int count; const uint8_t* bits; int line_height; };
const Font& font(Face face);
// Reading text (Latin and English) comes in three sizes: 0 small, 1 medium, 2 large.
void set_text_size(int size);
uint32_t next_codepoint(std::string_view s, size_t& pos);
bool font_has(Face face, uint32_t code);
int measure(std::string_view s, Face face);
std::vector<std::string> wrap(std::string_view s, Face face, int available);
class Raster {
public:
    explicit Raster(uint8_t* pixels): pixels_(pixels) {}
    void clear();
    void rect(int x,int y,int w,int h,uint8_t color=0);
    void outline(int x,int y,int w,int h);
    void text(std::string_view s,int x,int y,Face face=Face::UI);
    void centred(std::string_view s,int y,Face face=Face::UI);
    void right(std::string_view s,int x,int y,Face face=Face::UI);
    // E-paper stand-ins for the preview's hatched and light-grey fills.
    void hatch(int x,int y,int w,int h);
    void dots(int x,int y,int w);
private:
    uint8_t* pixels_;
};
}
