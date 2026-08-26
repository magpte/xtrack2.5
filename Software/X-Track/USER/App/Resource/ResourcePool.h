#ifndef __RESOURCE_POOL_H
#define __RESOURCE_POOL_H

#include "lvgl/lvgl.h"

namespace ResourcePool
{

void Init();
lv_font_t* GetFont(const char* name);
const void* GetImage(const char* name);

namespace Fonts
{
    extern const lv_font_t* const Bahnschrift_13;
    extern const lv_font_t* const Bahnschrift_17;
    extern const lv_font_t* const Bahnschrift_32;
    extern const lv_font_t* const Bahnschrift_65;
    extern const lv_font_t* const AgencyB_36;
}

namespace Images
{
    extern const void* const Alarm;
    extern const void* const Battery;
    extern const void* const Battery_Info;
    extern const void* const Bicycle;
    extern const void* const Compass;
    extern const void* const Gps_Arrow_Dark;
    extern const void* const Gps_Arrow_Default;
    extern const void* const Gps_Arrow_Light;
    extern const void* const Gps_Pin;
    extern const void* const Gyroscope;
    extern const void* const Locate;
    extern const void* const Map_Location;
    extern const void* const Menu;
    extern const void* const Origin_Point;
    extern const void* const Pause;
    extern const void* const Satellite;
    extern const void* const Sd_Card;
    extern const void* const Start;
    extern const void* const Stop;
    extern const void* const Storage;
    extern const void* const System_Info;
    extern const void* const Time_Info;
    extern const void* const Trip;
}

}

#endif

