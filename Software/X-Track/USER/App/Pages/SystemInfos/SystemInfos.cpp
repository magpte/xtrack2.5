#include "SystemInfos.h"
#include "../App/Version.h"

using namespace Page;

SystemInfos::SystemInfos()
    : timer(nullptr)
    , skyUpdateCounter(0)
{
}

SystemInfos::~SystemInfos()
{

}

void SystemInfos::onCustomAttrConfig()
{

}

void SystemInfos::onViewLoad()
{
    Model.Init();
    View.Create(_root);
    AttachEvent(_root);

    SystemInfosView::item_t* item_grp = ((SystemInfosView::item_t*)&View.ui);

    for (int i = 0; i < sizeof(View.ui) / sizeof(SystemInfosView::item_t); i++)
    {
        AttachEvent(item_grp[i].icon);
    }

    // sky 不在 View.ui 里面，上面那个循环覆盖不到，单独补一下。
    AttachEvent(View.sky.icon);
}

void SystemInfos::onViewDidLoad()
{

}

void SystemInfos::onViewWillAppear()
{
    Model.SetStatusBarStyle(DataProc::STATUS_BAR_STYLE_BLACK);

    View.Group_Init();

    // 200ms 定时器（5Hz 轮询），每次只刷新当前焦点条目的数据。
    // 对于时间类条目（GPS UTC、RTC）5Hz 足以保证秒数平滑递增，
    // 而每次回调只处理一组数据，执行时间极短，不会出现跳周期。
    timer = lv_timer_create(onTimerUpdate, 200, this);
    lv_timer_ready(timer);
    skyUpdateCounter = 0;
    cache.valid = false;
    cache.lastFocused = nullptr;

    View.SetScrollToY(_root, -LV_VER_RES, LV_ANIM_OFF);
    lv_obj_set_style_opa(_root, LV_OPA_COVER, 0);
}

void SystemInfos::onViewDidAppear()
{
    lv_group_t* group = lv_group_get_default();
    LV_ASSERT_NULL(group);
    View.onFocus(group);
}

void SystemInfos::onViewWillDisappear()
{
    View.Group_Deinit();
}

void SystemInfos::onViewDidDisappear()
{
    if (timer)
    {
        lv_timer_del(timer);
        timer = nullptr;
    }
}

void SystemInfos::onViewUnload()
{
    View.Delete();
    Model.Deinit();
}

void SystemInfos::onViewDidUnload()
{

}

void SystemInfos::AttachEvent(lv_obj_t* obj)
{
    lv_obj_add_event_cb(obj, onEvent, LV_EVENT_ALL, this);
}

void SystemInfos::Update()
{
    char buf[64];
    char tmpStr[128];

    /* 根据当前焦点条目，只刷新用户正在看的那一组数据。
     * 每个条目占满整屏（snap scrolling），不同条目之间互不可见，
     * 利用 4KB SRAM 脏数据缓存，只在信息真正发生改变时才驱动 LVGL 更新。 */

    lv_group_t* group = lv_group_get_default();
    lv_obj_t* focused = group ? lv_group_get_focused(group) : nullptr;

    if (!cache.valid || focused != cache.lastFocused)
    {
        cache.valid = true;
        cache.lastFocused = focused;
        cache.itemStr[0] = '\0';
        skyUpdateCounter = 0;
    }

    if (focused == View.ui.sport.icon)
    {
        float trip;
        float maxSpd;
        Model.GetSportInfo(&trip, buf, sizeof(buf), &maxSpd);
        snprintf(tmpStr, sizeof(tmpStr), "%.2f|%s|%.2f", trip, buf, maxSpd);
        if (strcmp(cache.itemStr, tmpStr) != 0)
        {
            strcpy(cache.itemStr, tmpStr);
            View.SetSport(trip, buf, maxSpd);
        }
    }
    else if (focused == View.ui.gps.icon)
    {
        double lat, lng;
        float alt, course, speed, pdop;
        Model.GetGPSInfo(&lat, &lng, &alt, buf, sizeof(buf), &course, &speed, &pdop);
        snprintf(tmpStr, sizeof(tmpStr), "%.5f|%.5f|%.1f|%s|%.1f|%.1f|%.1f", lat, lng, alt, buf, course, speed, pdop);
        if (strcmp(cache.itemStr, tmpStr) != 0)
        {
            strcpy(cache.itemStr, tmpStr);
            View.SetGPS(lat, lng, alt, buf, course, speed, pdop);
        }
    }
    else if (focused == View.sky.icon)
    {
        if (skyUpdateCounter % 5 == 0)
        {
            double lat, lng;
            float alt, course, speed, pdop;
            Model.GetGPSInfo(&lat, &lng, &alt, buf, sizeof(buf), &course, &speed, &pdop);
            View.SetSkyCourse(course);
        }

        if (skyUpdateCounter == 0)
        {
            HAL::Sky_Info_t sky;
            Model.GetSkyInfo(&sky);
            View.SetSky(&sky);
        }
    }
    else if (focused == View.ui.imu.icon)
    {
        int steps;
        if (Model.GetIMUInfo(&steps, buf, sizeof(buf)))
        {
            snprintf(tmpStr, sizeof(tmpStr), "%d|%s", steps, buf);
            if (strcmp(cache.itemStr, tmpStr) != 0)
            {
                strcpy(cache.itemStr, tmpStr);
                View.SetIMU(steps, buf);
            }
        }
    }
    else if (focused == View.ui.rtc.icon)
    {
        Model.GetRTCInfo(buf, sizeof(buf));
        if (strcmp(cache.itemStr, buf) != 0)
        {
            strcpy(cache.itemStr, buf);
            View.SetRTC(buf);
        }
    }
    else if (focused == View.ui.battery.icon)
    {
        int usage;
        float voltage;
        Model.GetBatteryInfo(&usage, &voltage, buf, sizeof(buf));
        snprintf(tmpStr, sizeof(tmpStr), "%d|%.2f|%s", usage, voltage, buf);
        if (strcmp(cache.itemStr, tmpStr) != 0)
        {
            strcpy(cache.itemStr, tmpStr);
            View.SetBattery(usage, voltage, buf);
        }
    }
    else if (focused == View.ui.storage.icon)
    {
        bool detect;
        const char* type = "-";
        Model.GetStorageInfo(&detect, &type, buf, sizeof(buf));
        snprintf(tmpStr, sizeof(tmpStr), "%d|%s|%s|%s", detect, buf, type, VERSION_FILESYSTEM);
        if (strcmp(cache.itemStr, tmpStr) != 0)
        {
            strcpy(cache.itemStr, tmpStr);
            View.SetStorage(
                detect ? "OK" : "ERROR",
                buf,
                type,
                VERSION_FILESYSTEM
            );
        }
    }
    else if (focused == View.ui.system.icon)
    {
        DataProc::MakeTimeString(lv_tick_get(), buf, sizeof(buf));
        if (strcmp(cache.itemStr, buf) != 0)
        {
            strcpy(cache.itemStr, buf);
            View.SetSystem(
                VERSION_FIRMWARE_NAME " " VERSION_SOFTWARE,
                VERSION_AUTHOR_NAME,
                VERSION_LVGL,
                buf,
                VERSION_COMPILER,
                VERSION_BUILD_TIME
            );
        }
    }

    skyUpdateCounter++;
    if (skyUpdateCounter >= 25)
    {
        skyUpdateCounter = 0;
    }
}

void SystemInfos::onTimerUpdate(lv_timer_t* timer)
{
    SystemInfos* instance = (SystemInfos*)timer->user_data;

    instance->Update();
}

void SystemInfos::onEvent(lv_event_t* event)
{
    SystemInfos* instance = (SystemInfos*)lv_event_get_user_data(event);
    LV_ASSERT_NULL(instance);

    lv_obj_t* obj = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED)
    {
        if (lv_obj_has_state(obj, LV_STATE_FOCUSED))
        {
            instance->_Manager->Pop();
        }
    }

    if (obj == instance->_root)
    {
        if (code == LV_EVENT_LEAVE)
        {
            instance->_Manager->Pop();
        }
    }
}
