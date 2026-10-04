// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
#include "engine.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pocket_office {
namespace {
constexpr int body_top=182,body_bottom=704;
int index(Hour h){return static_cast<int>(h);}
int index(Rite r){return static_cast<int>(r);}
bool available(Hour h,Rite r){return r==Rite::Monastic||h!=Hour::Prime;}
std::string time_text(int minutes){char b[8];std::snprintf(b,sizeof(b),"%02d:%02d",minutes/60,minutes%60);return b;}
std::string fit(const std::string& s,Face f,int max){
    if(measure(s,f)<=max)return s;
    std::string out;size_t p=0;
    while(p<s.size()){size_t start=p;next_codepoint(s,p);std::string next=out+s.substr(start,p-start);if(measure(next+"...",f)>max)break;out=next;}
    return out+"...";
}
}
const char* hour_name(Hour h,Rite rite){
    static const char* names[]={"Matins","Lauds","Prime","Terce","Sext","None","Vespers","Compline"};
    return h==Hour::Matins&&rite==Rite::Modern?"Readings":names[index(h)];
}
const char* rite_name(Rite r){return r==Rite::Monastic?"Benedictine / Monastic 1963":"Modern Roman Office";}
const char* zone_name(int z){static const char* names[]={"Central","UTC","Eastern","Mountain","Pacific","Rome","London"};return names[std::clamp(z,0,6)];}
const char* zone_posix(int z){
    static const char* zones[]={"CST6CDT,M3.2.0,M11.1.0","UTC0","EST5EDT,M3.2.0,M11.1.0","MST7MDT,M3.2.0,M11.1.0","PST8PDT,M3.2.0,M11.1.0","CET-1CEST,M3.5.0,M10.5.0/3","GMT0BST,M3.5.0/1,M10.5.0"};
    return zones[std::clamp(z,0,6)];
}
int day_number(int y,int m,int d){
    // Gregorian civil day; subtraction is safe across months, years and leap days.
    y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yo=y-era*400;
    unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    return era*146097+static_cast<int>(yo*365+yo/4-yo/100+doy)-719468;
}
std::vector<Page> paginate(const std::vector<Block>& blocks,Layout layout){
    std::vector<Page> result(1);int y=body_top;
    auto fresh=[&](size_t i,bool continued){result.push_back(Page{});result.back().first=i;result.back().continued=continued;y=body_top;if(continued){result.back().rows.push_back({24,y,Face::UI,"Continued"});y+=30;}};
    for(size_t i=0;i<blocks.size();++i){
        const Block& b=blocks[i];std::vector<Row> rows;int h=0;
        if(b.kind!=Kind::Prayer){
            std::string label=b.english.empty()?b.latin:b.english;
            if(b.kind==Kind::Marker)label="[ "+label+" ]";
            auto lines=wrap(label,Face::UI,432);
            for(auto& line:lines){rows.push_back({24,h,Face::UI,line});h+=font(Face::UI).line_height;}
            h+=b.kind==Kind::Heading?12:18;
        }else if(layout==Layout::Paired){
            for(auto& line:wrap(b.latin,Face::Latin,432)){rows.push_back({24,h,Face::Latin,line});h+=font(Face::Latin).line_height;}
            if(!b.english.empty())h+=5;
            for(auto& line:wrap(b.english,Face::English,432)){rows.push_back({24,h,Face::English,line});h+=font(Face::English).line_height;}
            h+=18;
        }else{
            int left=0,right=0;
            for(auto& line:wrap(b.latin,Face::Latin,206)){rows.push_back({24,left,Face::Latin,line});left+=font(Face::Latin).line_height;}
            for(auto& line:wrap(b.english,Face::English,206)){rows.push_back({250,right,Face::English,line});right+=font(Face::English).line_height;}
            h=std::max(left,right)+20;
            std::stable_sort(rows.begin(),rows.end(),[](const Row& a,const Row& b){return a.y<b.y;});
        }
        // Keep headings with at least the start of the next content block.
        int reserve=b.kind==Kind::Heading?90:0;
        if(y+h+reserve>body_bottom&&!result.back().rows.empty())fresh(i,false);
        if(h<=body_bottom-body_top){
            for(auto row:rows){row.y+=y;result.back().rows.push_back(std::move(row));}
            y+=h;
        }else{
            // Rare oversized paired passages: retain all rows and label continuation.
            int base=0;
            for(size_t n=0;n<rows.size();){
                int row_y=rows[n].y;
                int required=0;size_t end=n;
                while(end<rows.size()&&rows[end].y==row_y){required=std::max(required,font(rows[end].face).line_height);++end;}
                if(y+row_y-base+required>body_bottom){fresh(i,true);base=row_y;}
                for(;n<end;++n){auto row=rows[n];row.y+=y-base;result.back().rows.push_back(std::move(row));}
            }
            if(!result.back().rows.empty()){const Row& last=result.back().rows.back();y=last.y+font(last.face).line_height+18;}
        }
        result.back().last=i;
    }
    return result;
}
Engine::Engine(){rebuild();}
Engine::Bookmark& Engine::bookmark(){return bookmarks_[index(config.rite)][index(selected)];}
const Engine::Bookmark& Engine::bookmark()const{return bookmarks_[index(config.rite)][index(selected)];}
void Engine::rebuild(){
    blocks_=ordinary(config.rite,selected);pages_=paginate(blocks_,config.layout);
    Bookmark& b=bookmark();if(b.day!=selected_day){b.day=selected_day;b.page=0;}
    b.page=std::clamp(b.page,0,static_cast<int>(pages_.size())-1);
}
std::pair<Hour,int> Engine::scheduled()const{
    Hour active=Hour::Compline;int day=clock.day-1;
    for(int i=0;i<8;++i){Hour h=static_cast<Hour>(i);if(available(h,config.rite)&&clock.minute>=config.minutes[index(config.rite)][i]){active=h;day=clock.day;}}
    return {active,day};
}
void Engine::tick(Clock value){
    clock=value;
    if(!clock.valid){pending=false;return;}
    auto next=scheduled();bool changed=selected!=next.first||selected_day!=next.second;
    if(following&&changed){selected=next.first;selected_day=next.second;rebuild();}
    pending=!following&&changed;
}
void Engine::switch_mode(Mode value){mode=value;}
void Engine::set_rite(Rite value){
    if(config.rite==value)return;config.rite=value;
    if(!available(selected,value))selected=Hour::Lauds;
    if(following&&clock.valid){auto next=scheduled();selected=next.first;selected_day=next.second;}
    rebuild();tick(clock);
}
void Engine::set_layout(Layout value){
    if(config.layout==value)return;
    size_t anchor=pages_[page_index()].first;config.layout=value;rebuild();
    int page=0;for(size_t i=0;i<pages_.size();++i)if(pages_[i].first<=anchor&&pages_[i].last>=anchor){page=static_cast<int>(i);break;}
    bookmark().page=page;
}
bool Engine::set_schedule(Hour h,int minute){
    if(!available(h,config.rite)||minute<0||minute>=1440)return false;
    const auto& times=config.minutes[index(config.rite)];int idx=index(h);
    for(int i=idx-1;i>=0;--i)if(available(static_cast<Hour>(i),config.rite)){if(minute<=times[i])return false;break;}
    for(int i=idx+1;i<8;++i)if(available(static_cast<Hour>(i),config.rite)){if(minute>=times[i])return false;break;}
    config.minutes[index(config.rite)][idx]=minute;tick(clock);return true;
}
void Engine::now(){following=true;tick(clock);}
void Engine::choose_hour(Hour hour){if(!available(hour,config.rite))return;following=false;selected=hour;selected_day=clock.valid?clock.day:selected_day;rebuild();tick(clock);}
void Engine::cycle_hour(){int i=index(selected);do{i=(i+1)%8;}while(!available(static_cast<Hour>(i),config.rite));choose_hour(static_cast<Hour>(i));}
void Engine::turn(int direction){
    int next=std::clamp(page_index()+direction,0,page_count()-1);
    if(next!=page_index()){bookmark().page=next;following=false;tick(clock);}
}
int Engine::page_index()const{return bookmark().page;}
int Engine::page_count()const{return static_cast<int>(pages_.size());}
void Engine::tap(int x,int y){
    if(x<0||x>=width||y<0||y>=height)return;
    if(y<58){if(x>=20&&x<112)switch_mode(Mode::Muse);else if(x>=112&&x<216)switch_mode(Mode::Office);return;}
    if(mode!=Mode::Office)return;
    if(y<112){if(x>=326)now();else cycle_hour();}
    else if(y>=740){turn(x<width/2?-1:1);}
}
std::string Engine::setting_label(int index_)const{
    if(index_==7)return std::string("Office: ")+(config.rite==Rite::Monastic?"Benedictine":"Modern Roman");
    if(index_==8)return std::string("Layout: ")+(config.layout==Layout::Paired?"Paired verses":"Columns");
    if(index_==9)return std::string("Timezone: ")+zone_name(config.zone);
    if(index_==10)return following?"Hour: automatic":"Hour: reading hold";
    if(index_>=11&&index_<19){Hour h=static_cast<Hour>(index_-11);if(!available(h,config.rite))return "Prime: not in modern Office";return std::string(hour_name(h,config.rite))+": "+time_text(config.minutes[index(config.rite)][index(h)]);}
    return "";
}
void Engine::render(uint8_t* pixels,const MuseView& muse)const{
    Raster r(pixels);r.clear();
    r.text("Muse",30,18);r.text("Office",127,18);
    r.rect(mode==Mode::Muse?24:119,49,mode==Mode::Muse?78:94,2);
    char bat[12];if(muse.battery>=0)std::snprintf(bat,sizeof(bat),"%d%%",muse.battery);else std::snprintf(bat,sizeof(bat),"--%%");
    r.text(bat,351,18);r.text("...",435,18);r.rect(24,58,432,1);
    if(mode==Mode::Muse){
        std::string name=fit(muse.name?muse.name:"Muse Pocket",Face::Title,432);
        r.text(name,(width-measure(name,Face::Title))/2,68,Face::Title);
        if(muse.avatar)std::memcpy(pixels+114*width,muse.avatar,width*480);
        else {r.outline(146,225,188,188);r.text("Muse",(width-measure("Muse",Face::Title))/2,296,Face::Title);r.text("Awaiting character",(width-measure("Awaiting character",Face::UI))/2,435);}
        r.rect(24,607,432,1);auto lines=wrap(muse.status?muse.status:"",Face::English,432);
        for(size_t i=0;i<std::min<size_t>(4,lines.size());++i)r.text(lines[i],24,618+static_cast<int>(i)*29,Face::English);
        std::string connection=fit(muse.connection?muse.connection:"",Face::UI,432);r.text(connection,(width-measure(connection,Face::UI))/2,745);
        r.text("Tap Office to pray",24,776);return;
    }
    r.text(hour_name(selected,config.rite),24,68,Face::Title);
    r.text(following?"AUTO":"HOLD",326,74);r.text("Now",408,74);
    r.text(rite_name(config.rite),24,115);
    std::string timing=clock.valid?time_text(clock.minute)+" "+zone_name(config.zone):"Clock not synced - select hour";
    r.text(timing,24,144);r.rect(24,176,432,1);
    const Page& page=pages_[page_index()];
    for(const Row& row:page.rows)r.text(row.text,row.x,row.y,row.face);
    r.rect(24,710,432,1);
    r.text(pending?"Hour changed - tap Now when ready":"Ordinary only / variable texts marked",24,717);
    r.text("< Previous",24,754);r.text("Next >",369,754);
    std::string number=std::to_string(page_index()+1)+" / "+std::to_string(page_count());r.text(number,(width-measure(number,Face::UI))/2,754);
    r.text(config.rite==Rite::Modern?"Latin + English study text":"Latin + English / Divinum Officium",24,780);
}
}
