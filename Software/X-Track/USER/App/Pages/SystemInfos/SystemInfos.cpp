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

    /* 根据当前焦点条目，只刷新用户正在看的那一组数据。
     * 每个条目占满整屏（snap scrolling），不同条目之间互不可见，
     * 没必要把所有 7~8 组数据都 Pull + 更新 label——不可见的数据
     * 白白占用 CPU/总线时间，而且在中频/低频刷新叠加的那一拍，
     * 回调总耗时可能超过 200ms 定时器周期，导致 LVGL 跳过下一个
     * 周期，用户就会看到时间 2 秒 2 秒地跳。 */

    lv_group_t* group = lv_group_get_default();
    lv_obj_t* focused = group ? lv_group_get_focused(group) : nullptr;

    if (focused == View.ui.sport.icon)
    {
        float trip;
        float maxSpd;
        Model.GetSportInfo(&trip, buf, sizeof(buf), &maxSpd);
        View.SetSport(trip, buf, maxSpd);
    }
    else if (focused == View.ui.gps.icon)
    {
        float lat;
        float lng;
        float alt;
        float course;
        float speed;
        Model.GetGPSInfo(&lat, &lng, &alt, buf, sizeof(buf), &course, &speed);
        View.SetGPS(lat, lng, alt, buf, course, speed);
    }
    else if (focused == View.sky.icon)
    {
        /* Sky View 数据本身是 GSV 语句，模块按 0.5Hz 发送，HAL 侧
         * 还需要集齐三个星座才切换快照，实际有效更新远低于 5Hz。
         * 保持 5 秒的低频刷新（skyUpdateCounter == 0 时），既跟数据
         * 源节奏匹配，又避免每 200ms 都 memcpy + invalidate。 */
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
        Model.GetIMUInfo(&steps, buf, sizeof(buf));
        View.SetIMU(steps, buf);
    }
    else if (focused == View.ui.rtc.icon)
    {
        Model.GetRTCInfo(buf, sizeof(buf));
        View.SetRTC(buf);
    }
    else if (focused == View.ui.battery.icon)
    {
        int usage;
        float voltage;
        Model.GetBatteryInfo(&usage, &voltage, buf, sizeof(buf));
        View.SetBattery(usage, voltage, buf);
    }
    else if (focused == View.ui.storage.icon)
    {
        bool detect;
        const char* type = "-";
        Model.GetStorageInfo(&detect, &type, buf, sizeof(buf));
        View.SetStorage(
            detect ? "OK" : "ERROR",
            buf,
            type,
            VERSION_FILESYSTEM
        );
    }
    else if (focused == View.ui.system.icon)
    {
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

    /* skyUpdateCounter 无条件递增/回绕，即使当前不在 Sky View 页面。
     * 否则离开 Sky 再回来时计数器停在上次的值，要等剩余的周期数
     * 才会触发第一次刷新，体验上像是"回来后要等几秒才出数据"。 */
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
