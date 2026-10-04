// Muse Pocket: portrait e-paper UI and physical controls for the X4 Pro.
#include "sdkconfig.h"
#include "pocket.h"
#include "BoardConfig.h"
#include "XteinkDetect.h"
#include "driver/Ssd1677Driver.h"
#include "driver/Uc8179Driver.h"
#include "driver/Uc8279X4Driver.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_netif_sntp.h"
#include "cJSON.h"
#include "nvs.h"
#include "office/ui.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sys/time.h>
extern "C" {
#include "led_status.h"
#include "happy_anim.h"
}
// Dated prayer pack built from tools/office/office-pack.json at compile time.
extern const uint8_t office_pack_start[] asm("_binary_office_bin_start");
extern const uint8_t office_pack_end[] asm("_binary_office_bin_end");

namespace {
using pocket_ui::Action;
using pocket_ui::View;
constexpr int W=pocket_ui::W, H=pocket_ui::H, AVATAR=pocket_ui::AVATAR, AVATAR_Y=pocket_ui::AVATAR_Y;
// Settings rows. Recovery needs a deliberate hold and is never activated by a tap.
enum { ROW_LIGHT, ROW_WARMTH, ROW_CADENCE, ROW_FLIP, ROW_TEXT, ROW_RITE, ROW_CLOCK, ROW_ZONE, ROW_SLEEP, ROW_RECOVERY, ROW_BACK, ROWS };
const char* const text_sizes[]={"small","medium","large"};
constexpr int64_t CLOCK_VALID_AFTER=1700000000;
const char* const zone_names[]={"Central","UTC","Eastern","Mountain","Pacific","Rome","London"};
const char* const zone_rules[]={"CST6CDT,M3.2.0,M11.1.0","UTC0","EST5EDT,M3.2.0,M11.1.0","MST7MDT,M3.2.0,M11.1.0","PST8PDT,M3.2.0,M11.1.0","CET-1CEST,M3.5.0,M10.5.0/3","GMT0BST,M3.5.0/1,M10.5.0"};
constexpr int ZONES=7;
constexpr size_t FRAME=800*480/8;
constexpr uint8_t bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
const char* TAG="link.pocket.ui";
freeink::EpdBus bus;
freeink::PanelDriver* panel;
SemaphoreHandle_t lock_, image_done;
TaskHandle_t renderer;
uint8_t *canvas, *avatar, *staging, *frame, *shown;
bool custom_avatar=false, custom_status=false, loading=false, menu=false, flipped=false, sleeping=false;
bool recovery=false, force_full=true, initialized=false;
// Reading state for the pages reached from the Muse screen.
View page_view=View::Muse;
bool following=true, h12=false, interactive=false, sntp_started=false;
int rite=0, zone=0, text_size=1, prayer=-1, prayer_selected=0, reading_hour=0, page=0, commands_received=0;
uint32_t page_key=0;
char last_command[32]="";
char custom_name[64]="";
pocket_ui::Cards cards;
char setting_rows[ROWS][48];
int selected=0, brightness=25, warmth=50, cadence=5, refreshes=0, battery=-1;
int64_t last_status_us=0, last_draw_us=0;
uint32_t requested_frame=0, finished_frame=0;
led_state_t state=LED_STATE_BOOT;
char title[64]="Muse Pocket", status[241]="Pair with Muse to get started";
i2c_master_bus_handle_t i2c_bus;
i2c_master_dev_handle_t touch, gauge;
const int cadences[]={2,5,15,30};

const char* connection(led_state_t s) {
    switch(s) {
    case LED_STATE_SETUP_IDLE: case LED_STATE_BLE_ADVERTISING: return "Open Muse > Add gadget";
    case LED_STATE_BLE_CONNECTED: return "Phone connected";
    case LED_STATE_PAIRING_CONFIRM_REQUIRED: return "Press LEFT to confirm";
    case LED_STATE_WS_CONNECTED: return "Connected to Muse";
    case LED_STATE_WS_DISCONNECTED: return "Reconnecting";
    case LED_STATE_UNPAIRED: return "Pairing required";
    case LED_STATE_ERROR: return "Connection error";
    case LED_STATE_WIFI_CONNECTING: return "Joining Wi-Fi";
    default: return "Connecting";
    }
}
void notify() { if(renderer) xTaskNotifyGive(renderer); }
uint32_t request_frame() {
    xSemaphoreTake(lock_,portMAX_DELAY);uint32_t ticket=++requested_frame;xSemaphoreGive(lock_);
    notify();return ticket;
}
bool await_frame(uint32_t ticket) {
    int64_t deadline=esp_timer_get_time()+20000000;
    while(esp_timer_get_time()<deadline) {
        xSemaphoreTake(lock_,portMAX_DELAY);bool done=finished_frame>=ticket;xSemaphoreGive(lock_);
        if(done) return bus.healthy();
        xSemaphoreTake(image_done,pdMS_TO_TICKS(100));
    }
    return false;
}
void save_settings() {
    nvs_handle_t h;
    if(nvs_open("muse_pocket",NVS_READWRITE,&h)!=ESP_OK) return;
    nvs_set_u8(h,"light",brightness); nvs_set_u8(h,"warmth",warmth);
    nvs_set_u8(h,"cadence",cadence); nvs_set_u8(h,"flip",flipped);
    nvs_set_u8(h,"rite",rite); nvs_set_u8(h,"h12",h12); nvs_set_u8(h,"zone",zone);
    nvs_set_u8(h,"tsize",text_size);
    nvs_commit(h); nvs_close(h);
}
void light(int b, int warm) {
    uint32_t total=std::clamp(b,0,100)*1023/100;
    uint32_t amber=total*std::clamp(warm,0,100)/100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_4,total-amber);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_4);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_5,amber);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_5);
}
void light_init() {
    ledc_timer_config_t t={}; t.speed_mode=LEDC_LOW_SPEED_MODE;
    t.duty_resolution=LEDC_TIMER_10_BIT; t.timer_num=LEDC_TIMER_2;
    t.freq_hz=25000; t.clk_cfg=LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    for(int channel=4;channel<=5;++channel) {
        ledc_channel_config_t c={}; c.gpio_num=channel==4?8:9;
        c.speed_mode=LEDC_LOW_SPEED_MODE; c.channel=static_cast<ledc_channel_t>(channel);
        c.timer_sel=LEDC_TIMER_2; ESP_ERROR_CHECK(ledc_channel_config(&c));
    }
    nvs_handle_t h; uint8_t value;
    if(nvs_open("muse_pocket",NVS_READONLY,&h)==ESP_OK) {
        if(nvs_get_u8(h,"light",&value)==ESP_OK) brightness=std::min<int>(100,value);
        if(nvs_get_u8(h,"warmth",&value)==ESP_OK) warmth=std::min<int>(100,value);
        if(nvs_get_u8(h,"cadence",&value)==ESP_OK && (value==2||value==5||value==15||value==30)) cadence=value;
        if(nvs_get_u8(h,"flip",&value)==ESP_OK) flipped=value!=0;
        if(nvs_get_u8(h,"rite",&value)==ESP_OK) rite=value==1;
        if(nvs_get_u8(h,"h12",&value)==ESP_OK) h12=value!=0;
        if(nvs_get_u8(h,"zone",&value)==ESP_OK&&value<ZONES) zone=value;
        if(nvs_get_u8(h,"tsize",&value)==ESP_OK&&value<3) text_size=value;
        size_t length=sizeof(custom_name);
        if(nvs_get_str(h,"name",custom_name,&length)!=ESP_OK) custom_name[0]=0;
        nvs_close(h);
    }
    light(brightness,warmth);
}
bool i2c_read(i2c_master_dev_handle_t dev,uint16_t reg,uint8_t* data,size_t n,bool wide) {
    if(!dev) return false;
    uint8_t r[]={uint8_t(reg>>8),uint8_t(reg)};
    return i2c_master_transmit_receive(dev,wide?r:r+1,wide?2:1,data,n,50)==ESP_OK;
}
void peripherals_init() {
    pinMode(2,OUTPUT); digitalWrite(2,LOW); // active-low touch rail
    pinMode(4,OUTPUT); digitalWrite(4,LOW);
    pinMode(10,OUTPUT); digitalWrite(10,LOW); delay(10);
    digitalWrite(4,HIGH); delay(60); pinMode(10,INPUT); delay(80);
    i2c_master_bus_config_t cfg={}; cfg.i2c_port=I2C_NUM_0;
    cfg.sda_io_num=GPIO_NUM_39; cfg.scl_io_num=GPIO_NUM_38;
    cfg.clk_source=I2C_CLK_SRC_DEFAULT; cfg.glitch_ignore_cnt=7;
    cfg.flags.enable_internal_pullup=true;
    if(i2c_new_master_bus(&cfg,&i2c_bus)!=ESP_OK) return;
    i2c_device_config_t dev={}; dev.dev_addr_length=I2C_ADDR_BIT_LEN_7; dev.scl_speed_hz=100000;
    for(uint8_t address:{0x5d,0x14}) {
        if(i2c_master_probe(i2c_bus,address,50)==ESP_OK) {
            dev.device_address=address;
            i2c_master_bus_add_device(i2c_bus,&dev,&touch); break;
        }
    }
    dev.device_address=0x63;
    if(i2c_master_probe(i2c_bus,0x63,50)==ESP_OK) i2c_master_bus_add_device(i2c_bus,&dev,&gauge);
}
void rect(int x,int y,int w,int h,uint8_t color) {
    int x0=std::max(0,x),y0=std::max(0,y),x1=std::min(W,x+w),y1=std::min(H,y+h);
    for(int row=y0;row<y1;++row) if(x1>x0) memset(canvas+row*W+x0,color,x1-x0);
}
uint8_t luma(uint16_t rgb) {
    int r=((rgb>>11)&31)*255/31,g=((rgb>>5)&63)*255/63,b=(rgb&31)*255/31;
    return (r*77+g*150+b*29)>>8;
}
void default_character() {
    constexpr int scale=5, x0=(W-HAPPY_ANIM_WIDTH*scale)/2,y0=AVATAR_Y+70;
    for(int y=0;y<HAPPY_ANIM_HEIGHT;++y) for(int x=0;x<HAPPY_ANIM_WIDTH;++x) {
        uint8_t c=happy_anim_frames[0][y*HAPPY_ANIM_WIDTH+x];
        uint16_t be=happy_anim_palette[c];
        rect(x0+x*scale,y0+y*scale,scale,scale,c?luma((be>>8)|(be<<8)):255);
    }
}
void apply_zone() { setenv("TZ",zone_rules[zone],1); tzset(); }
bool clock_valid(int64_t now) { return now>CLOCK_VALID_AFTER; }
// Call with lock_ held. Row text lives in setting_rows until the next call.
pocket_ui::State snapshot() {
    pocket_ui::State s;
    s.view=sleeping?View::Sleeping:menu?View::Settings:page_view;
    s.name=custom_name[0]?custom_name:title; s.caption=status; s.connection=connection(state);
    s.connected=state==LED_STATE_WS_CONNECTED; s.battery=battery;
    s.avatar=custom_avatar?avatar:nullptr;
    s.now=time(nullptr); s.clock_valid=clock_valid(s.now); s.h12=h12; s.rite=rite; s.text_size=text_size; s.zone=zone_names[zone];
    s.following=following; s.reading_hour=reading_hour; s.cards=&cards;
    s.prayer=prayer; s.prayer_selected=prayer_selected;
    if(brightness) snprintf(setting_rows[ROW_LIGHT],48,"Brightness: %d%%",brightness);
    else snprintf(setting_rows[ROW_LIGHT],48,"Brightness: off");
    snprintf(setting_rows[ROW_WARMTH],48,"Warmth: %d%%",warmth);
    snprintf(setting_rows[ROW_CADENCE],48,"Refresh: every %ds",cadence);
    snprintf(setting_rows[ROW_FLIP],48,"Orientation: %s",flipped?"flipped":"normal");
    snprintf(setting_rows[ROW_TEXT],48,"Hours text: %s",text_sizes[text_size]);
    snprintf(setting_rows[ROW_RITE],48,"Tradition: %s",rite?"Modern (texts not loaded)":"Benedictine");
    snprintf(setting_rows[ROW_CLOCK],48,"Clock: %s",h12?"12-hour":"24-hour");
    snprintf(setting_rows[ROW_ZONE],48,"Timezone: %s",zone_names[zone]);
    snprintf(setting_rows[ROW_SLEEP],48,"Sleep");
    snprintf(setting_rows[ROW_RECOVERY],48,"%s",recovery?"Return to CrossPoint":"CrossPoint not verified");
    snprintf(setting_rows[ROW_BACK],48,"Back to Muse");
    for(int i=0;i<ROWS;++i) s.rows[i]=setting_rows[i];
    s.row_count=ROWS; s.selected=selected;
    // A new hour or day starts from its first page.
    if(page_view==View::Hours) {
        pocket_ui::Hours h=pocket_ui::hours(s);
        // Text size is left out: resizing keeps the reader near the same place.
        uint32_t key=h.date*16+h.reading*2+rite;
        if(key!=page_key) {page_key=key;page=0;}
    }
    // Settings and sleep keep the reading place of the page beneath them.
    if(s.view==page_view) page=std::clamp(page,0,pocket_ui::page_count(s)-1);
    s.page=page;
    return s;
}
void compose() {
    pocket_ui::State s=snapshot();
    pocket_ui::render(canvas,s);
    if(!s.avatar&&(s.view==View::Muse||s.view==View::Sleeping)) default_character();
}
void encode() {
    memset(frame,255,FRAME);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
        int px=flipped?H-1-y:y,py=flipped?x:W-1-x;
        uint8_t g=canvas[y*W+x];
        if(g<int(bayer[y%4][x%4])*16+8) frame[py*100+px/8]&=~(0x80>>(px%8));
    }
}
void render_task(void*) {
    TickType_t wait=portMAX_DELAY;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,wait);
        wait=portMAX_DELAY;
        xSemaphoreTake(lock_,portMAX_DELAY);
        int64_t now=esp_timer_get_time();
        bool immediate=menu||sleeping||force_full||interactive||requested_frame>finished_frame;
        if(!immediate && last_status_us>last_draw_us && now-last_draw_us<cadence*1000000LL) {
            wait=pdMS_TO_TICKS(std::max<int64_t>(1,(cadence*1000000LL-(now-last_draw_us))/1000));
            xSemaphoreGive(lock_); continue;
        }
        compose(); encode();
        bool changed=memcmp(frame,shown,FRAME)!=0;
        bool full=force_full||refreshes>=10;
        bool go_to_sleep=sleeping;
        uint32_t ticket=requested_frame;
        force_full=false;interactive=false;
        xSemaphoreGive(lock_);
        if((changed || full) && bus.healthy()) {
            panel->display(bus,frame,shown,full?freeink::RefreshMode::Full:freeink::RefreshMode::Fast,true);
            if(bus.healthy()) {
                memcpy(shown,frame,FRAME);
                xSemaphoreTake(lock_,portMAX_DELAY);
                refreshes=full?0:refreshes+1; last_draw_us=now;
                xSemaphoreGive(lock_);
            }
        }
        if(go_to_sleep) {
            light(0,warmth);
            if(bus.healthy()) panel->deepSleep(bus);
            digitalWrite(2,HIGH);
            pinMode(5,OUTPUT);digitalWrite(5,HIGH);
        }
        xSemaphoreTake(lock_,portMAX_DELAY);finished_frame=ticket;xSemaphoreGive(lock_);
        xSemaphoreGive(image_done);
        if(go_to_sleep) vTaskSuspend(nullptr);
    }
}
void sleep_now() {
    xSemaphoreTake(lock_,portMAX_DELAY); sleeping=true;force_full=true;xSemaphoreGive(lock_);
    if(!await_frame(request_frame())) {
        ESP_LOGE(TAG,"sleep screen or panel power-down failed; leaving recovery buttons active");
        return;
    }
    gpio_hold_en(GPIO_NUM_14);
    gpio_hold_en(GPIO_NUM_1);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_ext1_wakeup_io(1ULL<<3,ESP_EXT1_WAKEUP_ANY_LOW);
    while(digitalRead(3)==LOW) delay(30);
    esp_deep_sleep_start();
}
void activate() {
    xSemaphoreTake(lock_,portMAX_DELAY);
    switch(selected) {
    case ROW_LIGHT: {
        // Finer steps at the dim end, where the eye notices them most.
        static const int levels[]={0,5,10,25,50,75,100};
        int next=0;
        for(int level:levels) if(level>brightness) {next=level;break;}
        brightness=next;light(brightness,warmth);save_settings();break;
    }
    case ROW_WARMTH: warmth=(warmth+25)%125; light(brightness,warmth);save_settings();break;
    case ROW_CADENCE: for(int i=0;i<4;++i) if(cadence==cadences[i]) {cadence=cadences[(i+1)%4];break;} save_settings();break;
    case ROW_FLIP: flipped=!flipped;force_full=true;save_settings();break;
    case ROW_TEXT: {
        // Stay at the same proportion of the office when the page count changes.
        pocket_ui::State reading=snapshot();reading.view=View::Hours;
        int before=pocket_ui::page_count(reading);
        text_size=(text_size+1)%3;save_settings();
        reading.text_size=text_size;
        if(page_view==View::Hours) page=page*pocket_ui::page_count(reading)/before;
        break;
    }
    case ROW_RITE: rite=!rite;following=true;page=0;save_settings();break;
    case ROW_CLOCK: h12=!h12;save_settings();break;
    case ROW_ZONE: zone=(zone+1)%ZONES;apply_zone();save_settings();break;
    case ROW_SLEEP: xSemaphoreGive(lock_);sleep_now();return;
    case ROW_RECOVERY: break; // Recovery requires a deliberate hold, never a tap.
    case ROW_BACK: menu=false;force_full=true;break;
    }
    xSemaphoreGive(lock_);notify();
}
// Call with lock_ held.
void show(View view) {
    // Coming back from the prayers keeps the hour that was being read.
    if(view==View::Hours&&page_view!=View::Hours&&page_view!=View::Prayers) following=true;
    if(view==View::Prayers) {prayer=-1;prayer_selected=0;}
    if(page_view!=view) {page=0;force_full=true;}
    page_view=view;
}
void pick_hour(const pocket_ui::Hours& h,int step) {
    int at=0;
    for(int i=0;i<h.count;++i) if(h.visible[i]==h.reading) at=i;
    following=false;reading_hour=h.visible[(at+step+h.count)%h.count];page=0;
}
// Turning a page pins the hour being read, so the clock cannot move it mid-prayer.
void turn(const pocket_ui::State& s,int step) {
    if(page_view==View::Hours&&following) {following=false;reading_hour=pocket_ui::hours(s).reading;}
    page=std::clamp(page+step,0,pocket_ui::page_count(s)-1);
}
// Returns true when a settings row was tapped and should be activated.
bool apply(pocket_ui::Hit hit) {
    xSemaphoreTake(lock_,portMAX_DELAY);
    pocket_ui::State s=snapshot();
    bool row=false;
    switch(hit.action) {
    case Action::None: xSemaphoreGive(lock_);return false;
    case Action::Settings: menu=true;selected=0;force_full=true;break;
    case Action::Muse: if(menu){menu=false;force_full=true;}else show(View::Muse);break;
    case Action::Watches: show(View::Watches);break;
    case Action::NextUp: show(View::NextUp);break;
    case Action::OpenHours: show(View::Hours);break;
    case Action::CloseHours: show(View::Muse);break;
    case Action::PrevHour: pick_hour(pocket_ui::hours(s),-1);break;
    case Action::NextHour: pick_hour(pocket_ui::hours(s),1);break;
    case Action::SelectHour: show(View::Hours);following=false;reading_hour=hit.value;page=0;break;
    case Action::Prayers: if(page_view==View::Prayers){prayer=-1;page=0;}else show(View::Prayers);break;
    case Action::OpenPrayer: prayer=prayer_selected=std::clamp(hit.value,0,pocket_ui::prayer_count()-1);page=0;break;
    case Action::Now: following=true;page=0;break;
    case Action::PrevPage: turn(s,-1);break;
    case Action::NextPage: turn(s,1);break;
    case Action::Row: selected=std::clamp(hit.value,0,ROWS-1);row=true;break;
    }
    interactive=true;
    xSemaphoreGive(lock_);
    return row;
}
// RIGHT steps through everything without touch: Muse, Watchlist, Next up, then
// each page of the current hour, and back to Muse.
void advance() {
    xSemaphoreTake(lock_,portMAX_DELAY);
    if(sleeping) {xSemaphoreGive(lock_);return;}
    if(menu) selected=(selected+1)%ROWS;
    else {
        pocket_ui::State s=snapshot();
        bool more=page<pocket_ui::page_count(s)-1;
        switch(page_view) {
        case View::Muse: show(View::Watches);break;
        case View::Watches: if(more) turn(s,1); else show(View::NextUp);break;
        case View::NextUp: show(View::Hours);break;
        case View::Hours: if(more) turn(s,1); else show(View::Prayers);break;
        case View::Prayers:
            // In the list RIGHT moves the marker; in a prayer it turns the page.
            if(prayer<0) {if(prayer_selected+1<pocket_ui::prayer_count()) ++prayer_selected; else show(View::Muse);}
            else if(more) turn(s,1); else {prayer=-1;page=0;}
            break;
        default: show(View::Muse);break;
        }
    }
    interactive=true;
    xSemaphoreGive(lock_);notify();
}
// The mirror of advance(): the other button steps back the same way.
void retreat() {
    xSemaphoreTake(lock_,portMAX_DELAY);
    if(sleeping) {xSemaphoreGive(lock_);return;}
    if(menu) selected=(selected+ROWS-1)%ROWS;
    else {
        pocket_ui::State s=snapshot();
        switch(page_view) {
        case View::Watches: if(page>0) turn(s,-1); else show(View::Muse);break;
        case View::NextUp: show(View::Watches);break;
        case View::Hours: if(page>0) turn(s,-1); else show(View::NextUp);break;
        case View::Prayers:
            if(prayer<0) {if(prayer_selected>0) --prayer_selected; else show(View::Hours);}
            else if(page>0) turn(s,-1); else {prayer=-1;page=0;}
            break;
        default: break;
        }
    }
    interactive=true;
    xSemaphoreGive(lock_);notify();
}
void toggle_menu() {
    xSemaphoreTake(lock_,portMAX_DELAY);
    if(!sleeping) {menu=!menu;selected=0;force_full=true;}
    xSemaphoreGive(lock_);notify();
}
// Wi-Fi time keeps the hours and countdowns honest. Started once Muse is
// connected, when the network stack is certainly up.
void keep_time() {
    static int64_t last_minute=0;
    xSemaphoreTake(lock_,portMAX_DELAY);
    bool start=!sntp_started&&state==LED_STATE_WS_CONNECTED;
    if(start) sntp_started=true;
    xSemaphoreGive(lock_);
    if(start) {
        esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if(esp_netif_sntp_init(&config)!=ESP_OK) ESP_LOGW(TAG,"time sync unavailable");
    }
    int64_t minute=time(nullptr)/60;
    if(minute!=last_minute) {
        bool first=last_minute==0;
        last_minute=minute;
        if(!first&&clock_valid(minute*60)) notify();
    }
}
void input_task(void*) {
    bool was_right=false,was_power=false,fired=false,right_fired=false,touched=false;
    int64_t right_down=0,power_down=0,last_battery=0;
    for(;;) {
        int64_t now=esp_timer_get_time();
        bool right=digitalRead(7)==LOW,power=digitalRead(3)==LOW;
        if(right&&!was_right) {right_down=now;right_fired=false;}
        // Hold RIGHT for Settings; a short press moves on.
        if(right&&!right_fired&&now-right_down>=800000) {right_fired=true;toggle_menu();}
        if(!right&&was_right&&!right_fired&&now-right_down>=50000) advance();
        if(power&&!was_power) {power_down=now;fired=false;}
        if(power&&!fired&&now-power_down>=3000000) {
            fired=true;
            xSemaphoreTake(lock_,portMAX_DELAY);bool restore=menu&&selected==ROW_RECOVERY&&recovery;xSemaphoreGive(lock_);
            if(restore) pocket_return_to_crosspoint(); else sleep_now();
        }
        if(!power&&was_power&&!fired&&now-power_down>=50000) {
            xSemaphoreTake(lock_,portMAX_DELAY);bool in_menu=menu,away=page_view!=View::Muse,praying=page_view==View::Prayers;
            int listed=prayer,marked=prayer_selected;xSemaphoreGive(lock_);
            if(in_menu) activate();
            // Among the prayers POWER opens the marked one, or returns to the list.
            else if(praying) {apply(listed<0?pocket_ui::Hit{Action::OpenPrayer,marked}:pocket_ui::Hit{Action::Prayers,0});notify();}
            // POWER is the way home from Watchlist, Next up and Hours.
            else if(away) {apply({Action::CloseHours,0});notify();}
            else {xSemaphoreTake(lock_,portMAX_DELAY);force_full=true;xSemaphoreGive(lock_);notify();}
        }
        uint8_t touch_status=0;
        if(i2c_read(touch,0x814e,&touch_status,1,true)&&(touch_status&0x80)) {
            uint8_t point[8]={};
            bool contact=(touch_status&0x0f)&&i2c_read(touch,0x8150,point,8,true);
            if(contact&&!touched) {
                int x=point[0]|point[1]<<8,y=point[2]|point[3]<<8;
                xSemaphoreTake(lock_,portMAX_DELAY);
                if(flipped) {x=W-1-x;y=H-1-y;}
                pocket_ui::Hit target=pocket_ui::hit(snapshot(),x,y);
                xSemaphoreGive(lock_);
                if(apply(target)) activate();else notify();
            }
            touched=contact;
            uint8_t clear[]={0x81,0x4e,0};i2c_master_transmit(touch,clear,sizeof(clear),50);
        }
        if(now-last_battery>=60000000 || !last_battery) {
            uint8_t soc=0;
            if(i2c_read(gauge,0x04,&soc,1,false)&&soc<=100) {
                xSemaphoreTake(lock_,portMAX_DELAY);battery=soc;xSemaphoreGive(lock_);notify();
            }
            last_battery=now;
        }
        keep_time();
        was_right=right;was_power=power;delay(30);
    }
}
} // namespace

extern "C" bool led_status_init(void) {
    lock_=xSemaphoreCreateMutex();image_done=xSemaphoreCreateBinary();
    canvas=static_cast<uint8_t*>(heap_caps_malloc(W*H,MALLOC_CAP_SPIRAM));
    avatar=static_cast<uint8_t*>(heap_caps_malloc(W*AVATAR,MALLOC_CAP_SPIRAM));
    staging=static_cast<uint8_t*>(heap_caps_malloc(W*AVATAR,MALLOC_CAP_SPIRAM));
    frame=static_cast<uint8_t*>(heap_caps_malloc(FRAME,MALLOC_CAP_SPIRAM));
    shown=static_cast<uint8_t*>(heap_caps_malloc(FRAME,MALLOC_CAP_SPIRAM));
    if(!lock_||!image_done||!canvas||!avatar||!staging||!frame||!shown) return false;
    memset(shown,0,FRAME);memset(avatar,255,W*AVATAR);
    gpio_deep_sleep_hold_dis();
    uint8_t version[5],flags;
    auto verdict=freeink::detectXteinkDisplayController(version,&flags);
    bool uc=verdict==freeink::DisplayControllerVerdict::Uc81xxConfirmed;
    if(verdict==freeink::DisplayControllerVerdict::Inconclusive) {
        ESP_LOGE(TAG,"panel identification inconclusive; recovery remains available");return false;
    }
    if(uc) {
        auto v=version[2];BoardConfig::ACTIVE.displayControllerVariant=v;
        bool uc8279=v==2||v==3||v==0x67||v==0x68||v==0x69;
        panel=uc8279?&freeink::uc8279X4Driver():&freeink::uc8179Driver();
        ESP_LOGI(TAG,"controller %s variant %02x",uc8279?"UC8279":"UC8179",v);
    } else {panel=&freeink::ssd1677Driver();ESP_LOGI(TAG,"controller SSD1677");}
    bus.begin({12,11,13,18,14,6},panel->spiHz(),panel->busyPolarity());panel->begin(bus);
    if(!bus.healthy()) return false;
    light_init();peripherals_init();recovery=pocket_recovery_available();
    apply_zone();
    pocket_ui::set_pack(office_pack_start,office_pack_end-office_pack_start);
    pinMode(7,INPUT_PULLUP);pinMode(3,INPUT_PULLUP);
    if(xTaskCreate(render_task,"pocket_display",12288,nullptr,3,&renderer)!=pdPASS)return false;
    if(xTaskCreate(input_task,"pocket_input",12288,nullptr,2,nullptr)!=pdPASS)return false;
    xSemaphoreTake(lock_,portMAX_DELAY);initialized=true;xSemaphoreGive(lock_);
    notify();return true;
}
extern "C" bool pocket_local_boot_ready(void) {
    if(!renderer||!lock_)return false;
    xSemaphoreTake(lock_,portMAX_DELAY);
    bool ready=initialized&&recovery&&last_draw_us>0&&bus.healthy();
    xSemaphoreGive(lock_);return ready;
}
extern "C" void led_status_set_state(led_state_t value) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);state=value;
    if(value==LED_STATE_WS_CONNECTED&&!custom_status)
        snprintf(status,sizeof(status),"Ready for your Muse");
    last_status_us=esp_timer_get_time();xSemaphoreGive(lock_);notify();
}
extern "C" void led_status_set_title(const char* value) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);snprintf(title,sizeof(title),"%s",value&&*value?value:"Muse Pocket");last_status_us=esp_timer_get_time();xSemaphoreGive(lock_);notify();
}
extern "C" void pocket_set_status(const char* value) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);custom_status=true;snprintf(status,sizeof(status),"%s",value?value:"");last_status_us=esp_timer_get_time();xSemaphoreGive(lock_);notify();
}
extern "C" bool pocket_set_name(const char* value) {
    if(!renderer||!value||strlen(value)>=sizeof(custom_name))return false;
    for(const char* c=value;*c;++c) if(static_cast<unsigned char>(*c)<32)return false;
    xSemaphoreTake(lock_,portMAX_DELAY);
    snprintf(custom_name,sizeof(custom_name),"%s",value);
    nvs_handle_t h;
    if(nvs_open("muse_pocket",NVS_READWRITE,&h)==ESP_OK) {
        if(custom_name[0]) nvs_set_str(h,"name",custom_name); else nvs_erase_key(h,"name");
        nvs_commit(h);nvs_close(h);
    }
    last_status_us=esp_timer_get_time();xSemaphoreGive(lock_);notify();return true;
}
extern "C" void pocket_set_frontlight(int value,int temperature) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);brightness=std::clamp(value,0,100);warmth=std::clamp(temperature,0,100);
    light(brightness,warmth);save_settings();xSemaphoreGive(lock_);notify();
}
// Display commands target the square character canvas, leaving status visible.
extern "C" bool led_status_display_info(int* width,int* height) {
    if(!renderer) return false;
    if(width) *width=W;
    if(height) *height=AVATAR;
    return true;
}
extern "C" int led_status_display_bits(void) {return 1;}
extern "C" bool led_status_draw_rect(int x,int y,int w,int h,const uint16_t* pixels) {
    if(!renderer||!bus.healthy()||!pixels||w<=0||h<=0||x<0||y<0||w>W-x||h>AVATAR-y)return false;
    xSemaphoreTake(lock_,portMAX_DELAY);
    if(!loading){memset(staging,255,W*AVATAR);loading=true;}
    const uint8_t* bytes=reinterpret_cast<const uint8_t*>(pixels);
    for(int row=0;row<h;++row)for(int col=0;col<w;++col){size_t i=(row*w+col)*2;staging[(y+row)*W+x+col]=luma(bytes[i]<<8|bytes[i+1]);}
    xSemaphoreGive(lock_);return true;
}
extern "C" void led_status_draw_done(void) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);std::swap(avatar,staging);custom_avatar=true;loading=false;force_full=true;xSemaphoreGive(lock_);
    await_frame(request_frame());
}
extern "C" void led_status_show_animation(void) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);custom_avatar=false;loading=false;force_full=true;xSemaphoreGive(lock_);notify();
}
extern "C" void led_status_set_voice(led_voice_t) {}
extern "C" void led_status_set_level(float) {}
extern "C" void led_status_show_volume(int) {}

extern "C" void pocket_image_begin(void) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);memset(staging,255,W*AVATAR);loading=true;xSemaphoreGive(lock_);
}
extern "C" void pocket_image_abort(void) {
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);loading=false;xSemaphoreGive(lock_);
}
extern "C" bool pocket_image_complete(void) {
    if(!renderer)return false;
    xSemaphoreTake(lock_,portMAX_DELAY);std::swap(avatar,staging);custom_avatar=true;loading=false;force_full=true;xSemaphoreGive(lock_);
    return await_frame(request_frame());
}

namespace {
bool text_field(const cJSON* object,const char* key,char* out,size_t size,bool required) {
    const cJSON* item=cJSON_GetObjectItem(object,key);
    if(!item&&!required) {out[0]=0;return true;}
    if(!cJSON_IsString(item)||!item->valuestring||strlen(item->valuestring)>=size) return false;
    snprintf(out,size,"%s",item->valuestring);return true;
}
bool time_field(const cJSON* object,const char* key,int64_t* out) {
    const cJSON* item=cJSON_GetObjectItem(object,key);
    return cJSON_IsString(item)&&item->valuestring&&strlen(item->valuestring)<=50&&pocket_ui::parse_time(item->valuestring,out);
}
// A card's own timestamp sets the clock until Wi-Fi time arrives.
void seed_clock(int64_t updated) {
    if(clock_valid(time(nullptr))||!clock_valid(updated)) return;
    struct timeval tv={};tv.tv_sec=updated;settimeofday(&tv,nullptr);
}
void accepted(const char* command) {
    ++commands_received;snprintf(last_command,sizeof(last_command),"%s",command);
    last_status_us=esp_timer_get_time();
}
}
extern "C" bool pocket_set_watch_digest(const char* payload) {
    if(!renderer||!payload||strlen(payload)>16384) return false;
    cJSON* root=cJSON_Parse(payload);
    std::unique_ptr<pocket_ui::Cards> next(new pocket_ui::Cards());
    const cJSON* items=cJSON_GetObjectItem(root,"items");
    bool ok=cJSON_IsObject(root)&&time_field(root,"updated",&next->watches_updated)&&cJSON_IsArray(items)&&cJSON_GetArraySize(items)<=pocket_ui::MAX_WATCHES;
    const cJSON* item=nullptr;
    if(ok) cJSON_ArrayForEach(item,items) {
        pocket_ui::Watch& w=next->watches[next->watch_count];
        ok=cJSON_IsObject(item)&&text_field(item,"label",w.label,sizeof(w.label),true)&&text_field(item,"state",w.state,sizeof(w.state),true)
            &&text_field(item,"note",w.note,sizeof(w.note),true)&&time_field(item,"checked",&w.checked);
        if(!ok) break;
        ++next->watch_count;
    }
    cJSON_Delete(root);
    if(!ok) return false;
    seed_clock(next->watches_updated);
    xSemaphoreTake(lock_,portMAX_DELAY);
    cards.has_watches=true;cards.watches_updated=next->watches_updated;cards.watch_count=next->watch_count;
    memcpy(cards.watches,next->watches,sizeof(cards.watches));
    accepted("pocket.set_watch_digest");
    xSemaphoreGive(lock_);notify();return true;
}
extern "C" bool pocket_set_next_up(const char* payload) {
    if(!renderer||!payload||strlen(payload)>16384) return false;
    cJSON* root=cJSON_Parse(payload);
    std::unique_ptr<pocket_ui::Cards> next(new pocket_ui::Cards());
    bool ok=cJSON_IsObject(root)&&time_field(root,"updated",&next->next_updated)&&text_field(root,"title",next->title,sizeof(next->title),true);
    // An empty title clears the event; otherwise it needs a coherent time span.
    if(ok&&next->title[0]) ok=time_field(root,"when",&next->when)&&time_field(root,"ends",&next->ends)&&next->ends>=next->when
        &&text_field(root,"detail",next->detail,sizeof(next->detail),false);
    cJSON_Delete(root);
    if(!ok) return false;
    seed_clock(next->next_updated);
    xSemaphoreTake(lock_,portMAX_DELAY);
    cards.has_next=next->title[0]!=0;cards.next_updated=next->next_updated;cards.when=next->when;cards.ends=next->ends;
    memcpy(cards.title,next->title,sizeof(cards.title));memcpy(cards.detail,next->detail,sizeof(cards.detail));
    accepted("pocket.set_next_up");
    xSemaphoreGive(lock_);notify();return true;
}
extern "C" void pocket_previous(void) {
    if(renderer) retreat();
}
extern "C" void pocket_note_command(const char* command) {
    if(!renderer||!command)return;
    xSemaphoreTake(lock_,portMAX_DELAY);++commands_received;snprintf(last_command,sizeof(last_command),"%s",command);xSemaphoreGive(lock_);
}
// Connection and counters only; never the displayed private content.
extern "C" void pocket_get_status(const char** connection_out,int* received,char* last,size_t last_size) {
    if(connection_out)*connection_out="starting";
    if(received)*received=0;
    if(last&&last_size)last[0]=0;
    if(!renderer)return;
    xSemaphoreTake(lock_,portMAX_DELAY);
    if(connection_out)*connection_out=connection(state);
    if(received)*received=commands_received;
    if(last&&last_size)snprintf(last,last_size,"%s",last_command);
    xSemaphoreGive(lock_);
}
