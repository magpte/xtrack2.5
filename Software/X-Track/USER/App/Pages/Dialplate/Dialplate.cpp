#include "Dialplate.h"
#include "../HAL/HAL.h"

using namespace Page;

Dialplate::Dialplate()
    : recState(RECORD_STATE_READY)
    , lastFocus(nullptr)
    , isAdjustingBrightness(false)
    , brightnessValue(0)
    , isLocked(false)
    , savedBrightness(0)
{
}

Dialplate::~Dialplate()
{
}

void Dialplate::onCustomAttrConfig()
{
    SetCustomLoadAnimType(PageManager::LOAD_ANIM_NONE);
}

void Dialplate::onViewLoad()
{
    Model.Init();
    View.Create(_root);

    AttachEvent(View.ui.btnCont.btnMap);
    AttachEvent(View.ui.btnCont.btnRec);
    AttachEvent(View.ui.btnCont.btnMenu);
}

void Dialplate::onViewDidLoad()
{

}

void Dialplate::onViewWillAppear()
{
    lv_indev_wait_release(lv_indev_get_act());
    lv_group_t* group = lv_group_get_default();
    LV_ASSERT_NULL(group);

    lv_group_set_wrap(group, false);

    lv_group_add_obj(group, View.ui.btnCont.btnMap);
    lv_group_add_obj(group, View.ui.btnCont.btnRec);
    lv_group_add_obj(group, View.ui.btnCont.btnMenu);

    if (lastFocus)
    {
        lv_group_focus_obj(lastFocus);
    }
    else
    {
        lv_group_focus_obj(View.ui.btnCont.btnRec);
    }

    Model.SetStatusBarStyle(DataProc::STATUS_BAR_STYLE_TRANSP);

    Update();

    View.AppearAnimStart();
}

void Dialplate::onViewDidAppear()
{
    timer = lv_timer_create(onTimerUpdate, 1000, this);
}

void Dialplate::onViewWillDisappear()
{
    if (isAdjustingBrightness)
    {
        BrightnessAdjust_Exit();
    }

    // 正常情况下锁屏时其他按键都被屏蔽了，不应该会走到"离开页面"这一步；
    // 这里只是防御性兜底，万一因为某种意外情况触发了页面切换，确保
    // 背光/IMU 不会被遗留在锁屏时关闭的状态。
    if (isLocked)
    {
        LockMode_Exit();
    }

    lv_group_t* group = lv_group_get_default();
    LV_ASSERT_NULL(group);
    lastFocus = lv_group_get_focused(group);
    lv_group_remove_all_objs(group);
    lv_timer_del(timer);
    //View.AppearAnimStart(true);
}

void Dialplate::onViewDidDisappear()
{
}

void Dialplate::onViewUnload()
{
    Model.Deinit();
    View.Delete();
}

void Dialplate::onViewDidUnload()
{

}

void Dialplate::AttachEvent(lv_obj_t* obj)
{
    lv_obj_add_event_cb(obj, onEvent, LV_EVENT_ALL, this);
}

void Dialplate::Update()
{
    char buf[16];
    lv_label_set_text_fmt(View.ui.topInfo.labelSpeed, "%02d", (int)Model.GetSpeed());

    lv_label_set_text_fmt(View.ui.bottomInfo.labelInfoGrp[0].lableValue, "%0.1f km/h", Model.GetAvgSpeed());
    lv_label_set_text(
        View.ui.bottomInfo.labelInfoGrp[1].lableValue,
        DataProc::MakeTimeString(Model.sportStatusInfo.singleTime, buf, sizeof(buf))
    );
    lv_label_set_text_fmt(
        View.ui.bottomInfo.labelInfoGrp[2].lableValue,
        "%0.1f km",
        Model.sportStatusInfo.singleDistance / 1000
    );
    lv_label_set_text_fmt(
        View.ui.bottomInfo.labelInfoGrp[3].lableValue,
        "%0.1f %s",
        Model.GetCourse(),  
        Model.GetCourseDirection()  
    );
}

void Dialplate::onTimerUpdate(lv_timer_t* timer)
{
    Dialplate* instance = (Dialplate*)timer->user_data;

    instance->Update();
}

void Dialplate::onBtnClicked(lv_obj_t* btn)
{
    if (btn == View.ui.btnCont.btnMap)
    {
        _Manager->Push("Pages/LiveMap");
    }
    else if (btn == View.ui.btnCont.btnMenu)
    {
        _Manager->Push("Pages/SystemInfos");
    }
}

void Dialplate::onRecord(bool longPress)
{
    switch (recState)
    {
    case RECORD_STATE_READY:
        if (longPress)
        {
            Model.PlayMusic("Connect");
            Model.RecorderCommand(Model.REC_START);
            SetBtnRecImgSrc("pause");
            recState = RECORD_STATE_RUN;
        }
        break;
    case RECORD_STATE_RUN:
        if (!longPress)
        {
            Model.PlayMusic("UnstableConnect");
            Model.RecorderCommand(Model.REC_PAUSE);
            SetBtnRecImgSrc("start");
            recState = RECORD_STATE_PAUSE;
        }
        break;
    case RECORD_STATE_PAUSE:
        if (longPress)
        {
            Model.PlayMusic("NoOperationWarning");
            SetBtnRecImgSrc("stop");
            Model.RecorderCommand(Model.REC_READY_STOP);
            recState = RECORD_STATE_STOP;
        }
        else
        {
            Model.PlayMusic("Connect");
            Model.RecorderCommand(Model.REC_CONTINUE);
            SetBtnRecImgSrc("pause");
            recState = RECORD_STATE_RUN;
        }
        break;
    case RECORD_STATE_STOP:
        if (longPress)
        {
            Model.PlayMusic("Disconnect");
            Model.RecorderCommand(Model.REC_STOP);
            SetBtnRecImgSrc("start");
            recState = RECORD_STATE_READY;
        }
        else
        {
            Model.PlayMusic("Connect");
            Model.RecorderCommand(Model.REC_CONTINUE);
            SetBtnRecImgSrc("pause");
            recState = RECORD_STATE_RUN;
        }
        break;
    default:
        break;
    }
}

void Dialplate::SetBtnRecImgSrc(const char* srcName)
{
    lv_obj_set_style_bg_img_src(View.ui.btnCont.btnRec, ResourcePool::GetImage(srcName), 0);
}

#define BRIGHTNESS_STEP  50
#define BRIGHTNESS_MIN   0
#define BRIGHTNESS_MAX   1000

void Dialplate::BrightnessAdjust_Enter()
{
    lv_group_t* group = lv_group_get_default();
    LV_ASSERT_NULL(group);

    isAdjustingBrightness = true;
    brightnessValue = Model.GetScreenBrightness();

    // 编码器转动期间锁定焦点在 btnMenu 上，不再在按钮间切换焦点，
    // 而是把 LV_KEY_LEFT/LV_KEY_RIGHT 直接发给 btnMenu (见 lv_indev.c
    // 里 enc_diff 在 lv_group_get_editing() 为真时的分支)。
    lv_group_set_editing(group, true);

    View.SetBrightnessValue(brightnessValue);
    View.ShowBrightnessOverlay(true);

    HAL::Buzz_Tone(600, 20);
}

void Dialplate::BrightnessAdjust_Exit()
{
    lv_group_t* group = lv_group_get_default();
    LV_ASSERT_NULL(group);

    lv_group_set_editing(group, false);
    View.ShowBrightnessOverlay(false);
    isAdjustingBrightness = false;

    HAL::Buzz_Tone(400, 20);
}

void Dialplate::BrightnessAdjust_Step(int32_t dir)
{
    brightnessValue += dir * BRIGHTNESS_STEP;

    if (brightnessValue < BRIGHTNESS_MIN)
    {
        brightnessValue = BRIGHTNESS_MIN;
    }
    else if (brightnessValue > BRIGHTNESS_MAX)
    {
        brightnessValue = BRIGHTNESS_MAX;
    }

    // 立即应用到硬件 + 更新内存中的 sysConfig，下次 SYSCONFIG_CMD_SAVE
    // （关机/断电时触发，见 App.cpp）会把这个值写进 SystemSave.json。
    Model.SetScreenBrightness(brightnessValue);
    View.SetBrightnessValue(brightnessValue);
}

void Dialplate::LockMode_Enter()
{
    // 只在录制中才允许进入锁屏——不在录制的时候长按 Map 没有效果。
    if (recState != RECORD_STATE_RUN)
    {
        return;
    }

    isLocked = true;

    // 记下当前亮度，退出的时候恢复这个值，而不是写死恢复到某个固定
    // 亮度（不然会覆盖掉用户在设置里调好的亮度）。
    savedBrightness = HAL::Backlight_GetValue();
    HAL::Backlight_SetGradual(0, 500);

    // 暂停这个页面自己的 1 秒刷新定时器——速度/时长/距离这些标签既然
    // 看不见了，没必要还每秒重新计算+触发重绘。
    // GPS 解析、轨迹写入、看门狗完全不受影响：它们各自走的是 DataProc
    // 自己的 LVGL 定时器和硬件定时器中断，不依赖这个页面级的定时器，
    // 也不会因为这里暂停就跟着停。
    lv_timer_pause(timer);

    // IMU 只用来计步，录轨迹本身用不上，锁屏期间顺手关掉。
    HAL::IMU_SetEnable(false);

    HAL::Buzz_Tone(400, 30);

    LV_LOG_USER("Entered lock-screen recording mode");
}

void Dialplate::LockMode_Exit()
{
    isLocked = false;

    HAL::Backlight_SetGradual(savedBrightness, 500);
    lv_timer_resume(timer);
    HAL::IMU_SetEnable(true);

    HAL::Buzz_Tone(600, 30);

    LV_LOG_USER("Exited lock-screen recording mode");
}

void Dialplate::onEvent(lv_event_t* event)
{
    Dialplate* instance = (Dialplate*)lv_event_get_user_data(event);
    LV_ASSERT_NULL(instance);

    lv_obj_t* obj = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);

    // 锁屏期间屏幕是黑的，除了长按 Map 退出锁屏之外，其他任何按键都
    // 不应该有反应——不然揣在口袋里被意外触碰，可能会跳转页面、
    // 暂停录制、进入调光模式这些看不见屏幕的情况下很容易误操作的动作。
    if (instance->isLocked)
    {
        // 不管这时候焦点具体在哪个按钮上（屏幕是黑的，编码器意外转动
        // 会让焦点在 Rec/Map/Menu 之间跳，用户根本看不见跳到哪了）——
        // 只要是长按，就退出锁屏。之前写死判断"必须是 btnMap 的长按"，
        // 一旦焦点被意外转走，长按会落在别的按钮上被无声忽略掉，用户
        // 会误以为设备卡死/关机了。
        if (code == LV_EVENT_LONG_PRESSED)
        {
            instance->LockMode_Exit();
        }
        return;
    }

    if (obj == instance->View.ui.btnCont.btnRec)
    {
        if (code == LV_EVENT_SHORT_CLICKED)
        {
            instance->onRecord(false);
        }
        else if (code == LV_EVENT_LONG_PRESSED)
        {
            instance->onRecord(true);
        }
		}
		
    if (obj == instance->View.ui.btnCont.btnMap)
    {
        if (code == LV_EVENT_SHORT_CLICKED)
        {
            instance->onBtnClicked(obj);
        }
        else if (code == LV_EVENT_LONG_PRESSED)
        {
            instance->LockMode_Enter();
        }
		}
		
    if (obj == instance->View.ui.btnCont.btnMenu)
    {
        if (code == LV_EVENT_SHORT_CLICKED)
        {
            if (instance->isAdjustingBrightness)
            {
                // 短按 = 确认并退出调光模式，而不是跳转到设置页
                instance->BrightnessAdjust_Exit();
            }
            else
            {
                instance->onBtnClicked(obj);
            }
        }
        else if (code == LV_EVENT_LONG_PRESSED)
        {
            // 长按 Menu：进入/退出亮度调节模式。
            // 进入后旋转编码器每格 ±50 调整亮度（见下面 LV_EVENT_KEY 分支）。
            if (instance->isAdjustingBrightness)
            {
                instance->BrightnessAdjust_Exit();
            }
            else
            {
                instance->BrightnessAdjust_Enter();
            }
        }
        else if (code == LV_EVENT_KEY)
        {
            if (instance->isAdjustingBrightness)
            {
                uint32_t key = lv_event_get_key(event);
                if (key == LV_KEY_RIGHT)
                {
                    instance->BrightnessAdjust_Step(1);
                }
                else if (key == LV_KEY_LEFT)
                {
                    instance->BrightnessAdjust_Step(-1);
                }
            }
        }
		}
}
