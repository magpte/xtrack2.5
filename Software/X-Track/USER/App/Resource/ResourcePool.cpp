#include "ResourcePool.h"
#include <string.h>

extern "C" {
    LV_FONT_DECLARE(font_agencyb_36);
    LV_FONT_DECLARE(font_bahnschrift_13);
    LV_FONT_DECLARE(font_bahnschrift_17);
    LV_FONT_DECLARE(font_bahnschrift_32);
    LV_FONT_DECLARE(font_bahnschrift_65);

    LV_IMG_DECLARE(img_src_alarm);
    LV_IMG_DECLARE(img_src_battery);
    LV_IMG_DECLARE(img_src_battery_info);
    LV_IMG_DECLARE(img_src_bicycle);
    LV_IMG_DECLARE(img_src_compass);
    LV_IMG_DECLARE(img_src_gps_arrow_dark);
    LV_IMG_DECLARE(img_src_gps_arrow_default);
    LV_IMG_DECLARE(img_src_gps_arrow_light);
    LV_IMG_DECLARE(img_src_gps_pin);
    LV_IMG_DECLARE(img_src_gyroscope);
    LV_IMG_DECLARE(img_src_locate);
    LV_IMG_DECLARE(img_src_map_location);
    LV_IMG_DECLARE(img_src_menu);
    LV_IMG_DECLARE(img_src_origin_point);
    LV_IMG_DECLARE(img_src_pause);
    LV_IMG_DECLARE(img_src_satellite);
    LV_IMG_DECLARE(img_src_sd_card);
    LV_IMG_DECLARE(img_src_start);
    LV_IMG_DECLARE(img_src_stop);
    LV_IMG_DECLARE(img_src_storage);
    LV_IMG_DECLARE(img_src_system_info);
    LV_IMG_DECLARE(img_src_time_info);
    LV_IMG_DECLARE(img_src_trip);
}

/* 强类型直连常量定义（0 查找开销） */
const lv_font_t* const ResourcePool::Fonts::Bahnschrift_13  = &font_bahnschrift_13;
const lv_font_t* const ResourcePool::Fonts::Bahnschrift_17  = &font_bahnschrift_17;
const lv_font_t* const ResourcePool::Fonts::Bahnschrift_32  = &font_bahnschrift_32;
const lv_font_t* const ResourcePool::Fonts::Bahnschrift_65  = &font_bahnschrift_65;
const lv_font_t* const ResourcePool::Fonts::AgencyB_36      = &font_agencyb_36;

const void* const ResourcePool::Images::Alarm             = &img_src_alarm;
const void* const ResourcePool::Images::Battery           = &img_src_battery;
const void* const ResourcePool::Images::Battery_Info      = &img_src_battery_info;
const void* const ResourcePool::Images::Bicycle           = &img_src_bicycle;
const void* const ResourcePool::Images::Compass           = &img_src_compass;
const void* const ResourcePool::Images::Gps_Arrow_Dark    = &img_src_gps_arrow_dark;
const void* const ResourcePool::Images::Gps_Arrow_Default = &img_src_gps_arrow_default;
const void* const ResourcePool::Images::Gps_Arrow_Light   = &img_src_gps_arrow_light;
const void* const ResourcePool::Images::Gps_Pin           = &img_src_gps_pin;
const void* const ResourcePool::Images::Gyroscope         = &img_src_gyroscope;
const void* const ResourcePool::Images::Locate            = &img_src_locate;
const void* const ResourcePool::Images::Map_Location      = &img_src_map_location;
const void* const ResourcePool::Images::Menu              = &img_src_menu;
const void* const ResourcePool::Images::Origin_Point      = &img_src_origin_point;
const void* const ResourcePool::Images::Pause             = &img_src_pause;
const void* const ResourcePool::Images::Satellite         = &img_src_satellite;
const void* const ResourcePool::Images::Sd_Card           = &img_src_sd_card;
const void* const ResourcePool::Images::Start             = &img_src_start;
const void* const ResourcePool::Images::Stop              = &img_src_stop;
const void* const ResourcePool::Images::Storage           = &img_src_storage;
const void* const ResourcePool::Images::System_Info       = &img_src_system_info;
const void* const ResourcePool::Images::Time_Info         = &img_src_time_info;
const void* const ResourcePool::Images::Trip              = &img_src_trip;

struct ResourceItem {
    const char* name;
    const void* ptr;
};

/* 编译期静态只读表（存放在 Flash 中，按名称字母序严格排序，供二分查找使用） */
static const ResourceItem FontTable[] = {
    { "agencyb_36",      &font_agencyb_36 },
    { "bahnschrift_13",  &font_bahnschrift_13 },
    { "bahnschrift_17",  &font_bahnschrift_17 },
    { "bahnschrift_32",  &font_bahnschrift_32 },
    { "bahnschrift_65",  &font_bahnschrift_65 },
};

static const ResourceItem ImageTable[] = {
    { "alarm",              &img_src_alarm },
    { "battery",            &img_src_battery },
    { "battery_info",       &img_src_battery_info },
    { "bicycle",            &img_src_bicycle },
    { "compass",            &img_src_compass },
    { "gps_arrow_dark",     &img_src_gps_arrow_dark },
    { "gps_arrow_default",  &img_src_gps_arrow_default },
    { "gps_arrow_light",    &img_src_gps_arrow_light },
    { "gps_pin",            &img_src_gps_pin },
    { "gyroscope",          &img_src_gyroscope },
    { "locate",             &img_src_locate },
    { "map_location",       &img_src_map_location },
    { "menu",               &img_src_menu },
    { "origin_point",       &img_src_origin_point },
    { "pause",              &img_src_pause },
    { "satellite",          &img_src_satellite },
    { "sd_card",            &img_src_sd_card },
    { "start",              &img_src_start },
    { "stop",               &img_src_stop },
    { "storage",            &img_src_storage },
    { "system_info",        &img_src_system_info },
    { "time_info",          &img_src_time_info },
    { "trip",               &img_src_trip },
};

static const void* SearchResource(const ResourceItem* table, size_t count, const char* name)
{
    if (!name) return nullptr;

    int left = 0;
    int right = static_cast<int>(count) - 1;

    while (left <= right)
    {
        int mid = left + (right - left) / 2;
        int cmp = strcmp(name, table[mid].name);

        if (cmp == 0)
        {
            return table[mid].ptr;
        }
        else if (cmp < 0)
        {
            right = mid - 1;
        }
        else
        {
            left = mid + 1;
        }
    }

    return nullptr;
}

void ResourcePool::Init()
{
    // 静态只读表已在编译期确定，无需运行时堆分配初始化
}

lv_font_t* ResourcePool::GetFont(const char* name)
{
    const void* ptr = SearchResource(FontTable, sizeof(FontTable) / sizeof(FontTable[0]), name);
    if (!ptr)
    {
        LV_LOG_WARN("ResourcePool: Font '%s' not found, fallback to default", name ? name : "null");
        return (lv_font_t*)LV_FONT_DEFAULT;
    }
    return (lv_font_t*)ptr;
}

const void* ResourcePool::GetImage(const char* name)
{
    const void* ptr = SearchResource(ImageTable, sizeof(ImageTable) / sizeof(ImageTable[0]), name);
    if (!ptr)
    {
        LV_LOG_WARN("ResourcePool: Image '%s' not found", name ? name : "null");
    }
    return ptr;
}

