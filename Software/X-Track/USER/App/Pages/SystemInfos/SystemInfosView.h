#ifndef __SYSTEM_INFOS_VIEW_H
#define __SYSTEM_INFOS_VIEW_H

#include "../Page.h"
#include "Common/HAL/HAL_Def.h"

namespace Page
{

class SystemInfosView
{
public:
    void Create(lv_obj_t* root);
    void Delete();
    void Group_Init();
    void Group_Deinit();

public:
    typedef struct
    {
        lv_obj_t* cont;
        lv_obj_t* icon;
        lv_obj_t* labelInfo;
        lv_obj_t* labelData;
    } item_t;

    struct
    {
        item_t sport;
        item_t gps;
        item_t imu;
        item_t rtc;
        item_t battery;
        item_t storage;
        item_t system;
    } ui;

    // 天球图不是文字信息行，跟 item_t 的结构对不上，单独定义。
    typedef struct
    {
        lv_obj_t* cont;
        lv_obj_t* icon;
        lv_obj_t* plot;
        // 卫星点直接画在 plot 上（见 onSkyPlotDraw），不再为每颗卫星建
        // 一个 lv_obj——32 个对象要占掉将近 10KB 的 LVGL 堆（总共只有
        // LV_MEM_SIZE = 40KB），这一页加上后台缓存的 Dialplate 就足以
        // 把堆用到 85% 以上，之后任何一次绘制/动画分配失败都会变成
        // 空指针解引用（固件里 LV_USE_ASSERT_MALLOC 是关的）。
        HAL::Sky_Info_t info;
        float course;
    } sky_t;

    sky_t sky;

public:
    void SetSport(
        float trip,
        const char* time,
        float maxSpd
    );
    void SetGPS(
        double lat,
        double lng,
        float alt,
        const char* utc,
        float course,
        float speed
    );
    void SetIMU(
        int step,
        const char* info
    );
    void SetRTC(
        const char* dateTime
    );
    void SetBattery(
        int usage,
        float voltage,
        const char* state
    );
    void SetStorage(
        const char* detect,
        const char* size,
        const char* type,
        const char* version
    );
    void SetSystem(
        const char* firmVer,
        const char* authorName,
        const char* lvglVer,
        const char* bootTime,
        const char* compilerName,
        const char* bulidTime
    );
    void SetSky(HAL::Sky_Info_t* info);
    void SetSkyCourse(float course);

    void SetScrollToY(lv_obj_t* obj, lv_coord_t y, lv_anim_enable_t en);
    static void onSkyPlotDraw(lv_event_t* event);
    static void onFocus(lv_group_t* e);

private:
    struct
    {
        lv_style_t icon;
        lv_style_t focus;
        lv_style_t info;
        lv_style_t data;
    } style;

private:
    void Style_Init();
    void Style_Reset();
    void Item_Create(
        item_t* item,
        lv_obj_t* par,
        const char* name,
        const char* img_src,
        const char* infos
    );
    void SkyPlot_Create(lv_obj_t* par);
};

}

#endif // !__VIEW_H
