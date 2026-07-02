#include "Dialplate.h"
#include "../HAL/HAL.h"

using namespace Page;

Dialplate::Dialplate()
    : recState(RECORD_STATE_READY)
    , lastFocus(nullptr)
    , isAdjustingBrightness(false)
    , brightnessValue(0)
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

void Dialplate::onEvent(lv_event_t* event)
{
    Dialplate* instance = (Dialplate*)lv_event_get_user_data(event);
    LV_ASSERT_NULL(instance);

    lv_obj_t* obj = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);

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
            HAL::Backlight_SetGradual(1000, 1000);
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
