// Muse Pocket Office. SPDX-License-Identifier: Apache-2.0
#include "raster.h"
#include "font_data.h"
#include <algorithm>
#include <cstring>

namespace pocket_office {
namespace { int text_size = 1; }
void set_text_size(int size) { text_size = std::clamp(size, 0, 2); }
const Font& font(Face face) {
    switch (face) {
    case Face::Latin: return fonts[text_size * 2];
    case Face::English: return fonts[text_size * 2 + 1];
    case Face::UI: return fonts[6];
    case Face::Title: return fonts[7];
    default: return fonts[8];
    }
}
const Glyph* find(const Font& f,uint32_t code) {
    int lo=0,hi=f.count;
    while(lo<hi) { int mid=(lo+hi)/2; if(f.glyphs[mid].code<code) lo=mid+1; else hi=mid; }
    return lo<f.count&&f.glyphs[lo].code==code?&f.glyphs[lo]:nullptr;
}
bool font_has(Face face,uint32_t code) {return find(font(face),code)!=nullptr;}
uint32_t next_codepoint(std::string_view s,size_t& pos) {
    if(pos>=s.size()) return 0;
    uint8_t c=s[pos++]; if(c<128) return c;
    int n=c>=0xf0?3:c>=0xe0?2:c>=0xc0?1:0;
    if(!n||pos+n>s.size()) return '?';
    uint32_t value=c&((1<<(6-n))-1);
    for(int i=0;i<n;++i) {uint8_t b=s[pos];if((b&0xc0)!=0x80)return '?';++pos;value=(value<<6)|(b&0x3f);}
    return value;
}
int measure(std::string_view s,Face face) {
    const Font& f=font(face);int result=0;size_t pos=0;
    while(pos<s.size()) {auto g=find(f,next_codepoint(s,pos));if(!g)g=find(f,'?');result+=g?g->advance:0;}
    return result;
}
std::vector<std::string> wrap(std::string_view s,Face face,int available) {
    std::vector<std::string> lines;
    if(s.empty()) return lines;
    size_t pos=0;
    while(pos<s.size()) {
        while(pos<s.size()&&s[pos]==' ') ++pos;
        if(pos>=s.size())break;
        size_t start=pos,last_space=std::string::npos,end=pos;int used=0;
        while(pos<s.size()&&s[pos]!='\n') {
            size_t next=pos;uint32_t cp=next_codepoint(s,next);auto g=find(font(face),cp);if(!g)g=find(font(face),'?');
            int advance=g?g->advance:0;
            if(used+advance>available&&pos>start) break;
            if(s[pos]==' ')last_space=pos;
            used+=advance;end=next;pos=next;
        }
        if(pos<s.size()&&s[pos]!='\n'&&last_space!=std::string::npos&&last_space>start) {end=last_space;pos=last_space+1;}
        else if(pos<s.size()&&s[pos]=='\n') ++pos;
        // A single wide glyph still consumes input, so even narrow widths terminate.
        if(end==start) {next_codepoint(s,pos);end=pos;}
        std::string row(s.substr(start,end-start));while(!row.empty()&&row.back()==' ')row.pop_back();
        lines.push_back(row);
    }
    return lines;
}
void Raster::clear(){std::memset(pixels_,255,width*height);}
void Raster::rect(int x,int y,int w,int h,uint8_t color) {
    int x0=std::max(0,x),x1=std::min(width,x+w),y0=std::max(0,y),y1=std::min(height,y+h);
    for(int row=y0;row<y1;++row)if(x1>x0)std::memset(pixels_+row*width+x0,color,x1-x0);
}
void Raster::outline(int x,int y,int w,int h){rect(x,y,w,1);rect(x,y+h-1,w,1);rect(x,y,1,h);rect(x+w-1,y,1,h);}
void Raster::centred(std::string_view s,int y,Face face){text(s,(width-measure(s,face))/2,y,face);}
void Raster::right(std::string_view s,int x,int y,Face face){text(s,x-measure(s,face),y,face);}
void Raster::hatch(int x,int y,int w,int h){
    for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col)if((col+row)%3==0)rect(col,row,1,1);
}
void Raster::dots(int x,int y,int w){for(int col=x;col<x+w;col+=2)rect(col,y,1,1);}
void Raster::text(std::string_view s,int x,int y,Face face) {
    const Font& f=font(face);size_t pos=0;
    while(pos<s.size()) {
        auto g=find(f,next_codepoint(s,pos));if(!g)g=find(f,'?');if(!g)continue;
        for(int row=0;row<g->h;++row)for(int col=0;col<g->w;++col) {
            size_t bit=row*g->w+col;
            if(f.bits[g->offset+bit/8]&(0x80>>(bit%8)))rect(x+g->x+col,y+g->y+row,1,1);
        }
        x+=g->advance;
    }
}
}
