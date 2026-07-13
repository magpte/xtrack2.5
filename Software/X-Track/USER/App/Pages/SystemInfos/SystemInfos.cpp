#include "SystemInfos.h"
#include "../App/Version.h"

using namespace Page;

SystemInfos::SystemInfos()
    : skyUpdateCounter(0)
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

    timer = lv_timer_create(onTimerUpdate, 1000, this);
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
    lv_obj_fade_out(_root, 300, 0);
}

void SystemInfos::onViewDidDisappear()
{
    lv_timer_del(timer);
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

    /* Sport */
    float trip;
    float maxSpd;
    Model.GetSportInfo(&trip, buf, sizeof(buf), &maxSpd);
    View.SetSport(trip, buf, maxSpd);

    /* GPS */
    float lat;
    float lng;
    float alt;
    float course;
    float speed;
    Model.GetGPSInfo(&lat, &lng, &alt, buf, sizeof(buf), &course, &speed);
    View.SetGPS(lat, lng, alt, buf, course, speed);

    /* IMU */
    int steps;
    Model.GetIMUInfo(&steps, buf, sizeof(buf));
    View.SetIMU(steps, buf);

    /* RTC */
    Model.GetRTCInfo(buf, sizeof(buf));
    View.SetRTC(buf);

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
    char stackBuf[64];
    char heapBuf[48];  // 显示用的缩短版——HAL_Memory.cpp 拼出来的原始诊断
                        // 信息可能很长，这里限制一下，配合 labelData 的
                        // 换行宽度（130px），避免换行行数太多撑爆容器。
    Model.GetMemoryInfo(stackBuf, sizeof(stackBuf), heapBuf, sizeof(heapBuf));

    DataProc::MakeTimeString(lv_tick_get(), buf, sizeof(buf));
    View.SetSystem(
        VERSION_FIRMWARE_NAME " " VERSION_SOFTWARE,
        VERSION_AUTHOR_NAME,
        VERSION_LVGL,
        buf,
        VERSION_COMPILER,
        VERSION_BUILD_TIME,
        stackBuf,
        heapBuf
    );

    /* Sky View —— 每 5 秒才刷新一次，跟 GSV 数据本身的节流频率对上，
     * 不需要跟着 Update() 的 1 秒周期一起跑。counter 从 0 开始，
     * 第一次调用（counter==0）就会刷新，页面刚打开就能看到数据，
     * 不用干等最多 5 秒。 */
    if (skyUpdateCounter == 0)
    {
        HAL::Sky_Info_t sky;
        Model.GetSkyInfo(&sky);
        View.SetSky(&sky);
    }
    skyUpdateCounter++;
    if (skyUpdateCounter >= 5)
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
