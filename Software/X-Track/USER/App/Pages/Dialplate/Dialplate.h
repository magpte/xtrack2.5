#ifndef __DIALPLATE_PRESENTER_H
#define __DIALPLATE_PRESENTER_H

#include "DialplateView.h"
#include "DialplateModel.h"

namespace Page
{

class Dialplate : public PageBase
{
public:
    Dialplate();
    virtual ~Dialplate();

    virtual void onCustomAttrConfig();
    virtual void onViewLoad();
    virtual void onViewDidLoad();
    virtual void onViewWillAppear();
    virtual void onViewDidAppear();
    virtual void onViewWillDisappear();
    virtual void onViewDidDisappear();
    virtual void onViewUnload();
    virtual void onViewDidUnload();

private:
    typedef enum
    {
        RECORD_STATE_READY,
        RECORD_STATE_RUN,
        RECORD_STATE_PAUSE,
        RECORD_STATE_STOP
    } RecordState_t;

private:
    void Update();
    void AttachEvent(lv_obj_t* obj);
    static void onTimerUpdate(lv_timer_t* timer);
    static void onEvent(lv_event_t* event);
    void onBtnClicked(lv_obj_t* btn);
    void onRecord(bool longPress);
    void SetBtnRecImgSrc(const char* srcName);
    void BrightnessAdjust_Enter();
    void BrightnessAdjust_Exit();
    void BrightnessAdjust_Step(int32_t dir);
    void LockMode_Enter();
    void LockMode_Exit();

private:
    DialplateView View;
    DialplateModel Model;
    lv_timer_t* timer;
    RecordState_t recState;
    lv_obj_t* lastFocus;
    bool isAdjustingBrightness;
    int32_t brightnessValue;
    bool isLocked;
    int32_t savedBrightness;   // 进锁屏前的亮度，退出时恢复，而不是写死某个值

    typedef struct
    {
        bool valid;
        char speedStr[16];
        char avgSpeedStr[32];
        char timeStr[32];
        char distStr[32];
        char courseStr[32];
        uint8_t buffer[3900]; // 4KB 专用 SRAM 缓存空间，消除无谓格式化与 LVGL 重绘开销
    } DialplateCache_t;

    DialplateCache_t cache;
};

}

#endif
