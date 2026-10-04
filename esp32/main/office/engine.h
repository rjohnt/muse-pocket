// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
#pragma once
#include "raster.h"
#include <array>
#include <vector>

namespace pocket_office {
enum class Mode { Muse, Office };
enum class Rite { Monastic, Modern };
enum class Layout { Paired, Columns };
enum class Hour { Matins, Lauds, Prime, Terce, Sext, None, Vespers, Compline };
enum class Kind { Heading, Prayer, Marker, Note };
struct Block { Kind kind; std::string latin,english; };
struct Clock { bool valid=false; int day=0,minute=0; };
struct Config {
    Rite rite=Rite::Monastic;
    Layout layout=Layout::Paired;
    int zone=0;
    // Personal default for modern mode; Clear Creek's published ordinary
    // schedule for monastic mode. Prime is unused in the modern profile.
    std::array<std::array<uint16_t,8>,2> minutes{{
        {{315,375,480,600,770,875,1080,1225}},
        {{315,375,480,600,770,875,1080,1225}}
    }};
};
struct MuseView { const char* name="Muse Pocket";const char* status="Pair with Muse to get started";const char* connection="Pairing required";int battery=-1;const uint8_t* avatar=nullptr; };
struct Row { int x,y;Face face;std::string text; };
struct Page { std::vector<Row> rows;size_t first=0,last=0;bool continued=false; };
const char* hour_name(Hour hour,Rite rite);
const char* rite_name(Rite rite);
const char* zone_name(int zone);
const char* zone_posix(int zone);
int day_number(int year,int month,int day);
std::vector<Block> ordinary(Rite rite,Hour hour);
std::vector<Page> paginate(const std::vector<Block>& blocks,Layout layout);
class Engine {
public:
    Engine();
    Config config;
    Mode mode=Mode::Muse;
    Hour selected=Hour::Vespers;
    Clock clock;
    bool following=true,pending=false;
    int selected_day=0;
    void tick(Clock value);
    void switch_mode(Mode value);
    void set_rite(Rite value);
    void set_layout(Layout value);
    bool set_schedule(Hour hour,int minute);
    void now();
    void cycle_hour();
    void choose_hour(Hour hour);
    void turn(int direction);
    void tap(int x,int y);
    int page_index() const;
    int page_count() const;
    const std::vector<Block>& blocks() const {return blocks_;}
    const std::vector<Page>& pages() const {return pages_;}
    void render(uint8_t* pixels,const MuseView& muse) const;
    std::string setting_label(int index) const;
private:
    struct Bookmark {int day=-1000000,page=0;};
    std::array<std::array<Bookmark,8>,2> bookmarks_{};
    std::vector<Block> blocks_;
    std::vector<Page> pages_;
    void rebuild();
    std::pair<Hour,int> scheduled() const;
    Bookmark& bookmark();
    const Bookmark& bookmark() const;
};
}
