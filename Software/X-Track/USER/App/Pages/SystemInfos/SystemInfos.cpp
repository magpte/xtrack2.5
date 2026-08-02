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

    // 【修改点1】：将 LVGL 定时器从 1000ms 改为 200ms，以 5Hz 频率轮询来避免跳秒
    timer = lv_timer_create(onTimerUpdate, 200, this);
    lv_timer_ready(timer);
    skyUpdateCounter = 0;

    View.SetScrollToY(_root, -LV_VER_RES, LV_ANIM_OFF);
    lv_obj_set_style_opa(_root, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(_root, 300, 0);
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

    lv_obj_fade_out(_root, 300, 0);
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

    /* =======================================================
     * 1. 高频刷新 (200ms / 5Hz)：仅针对时间高度敏感的数据
     * ======================================================= */
    
    /* GPS */
    float lat;
    float lng;
    float alt;
    float course;
    float speed;
    Model.GetGPSInfo(&lat, &lng, &alt, buf, sizeof(buf), &course, &speed);
    View.SetGPS(lat, lng, alt, buf, course, speed);

    /* RTC */
    Model.GetRTCInfo(buf, sizeof(buf));
    View.SetRTC(buf);

    /* =======================================================
     * 2. 中频刷新 (1000ms / 1Hz)：针对常规且耗费解析算力的数据
     * ======================================================= */
    // 因为定时器是 200ms，当计数器对 5 取余为 0 时，恰好是 1000ms
    if (skyUpdateCounter % 5 == 0) 
    {
        /* Sport */
        float trip;
        float maxSpd;
        Model.GetSportInfo(&trip, buf, sizeof(buf), &maxSpd);
        View.SetSport(trip, buf, maxSpd);

        /* IMU */
        int steps;
        Model.GetIMUInfo(&steps, buf, sizeof(buf));
        View.SetIMU(steps, buf);

        /* Power */
        int usage;
        float voltage;
        Model.GetBatteryInfo(&usage, &voltage, buf, sizeof(buf));
        View.SetBattery(usage, voltage, buf);

        /* Storage */
        bool detect;
        const char* type = "-";
        Model.GetStorageInfo(&detect, &type, buf, sizeof(buf));
        View.SetStorage(
            detect ? "OK" : "ERROR",
            buf,
            type,
            VERSION_FILESYSTEM
        );

        /* System */
        DataProc::MakeTimeString(lv_tick_get(), buf, sizeof(buf));
        View.SetSystem(
            VERSION_FIRMWARE_NAME " " VERSION_SOFTWARE,
            VERSION_AUTHOR_NAME,
            VERSION_LVGL,
            buf,
            VERSION_COMPILER,
            VERSION_BUILD_TIME
        );
    }

    /* =======================================================
     * 3. 低频刷新 (5000ms / 0.2Hz)：针对慢速更新的天球图数据
     * ======================================================= */
    /* Sky View —— 每 5 秒刷新一次，跟 GSV 数据本身的节流频率对上 */
    if (skyUpdateCounter == 0)
    {
        HAL::Sky_Info_t sky;
        Model.GetSkyInfo(&sky);
        View.SetSky(&sky);
    }
    
    /* 更新计数器 */
    skyUpdateCounter++;
    
    // 【修改点2】：现在按 200ms 一次计算，25次正好是 5000ms
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
