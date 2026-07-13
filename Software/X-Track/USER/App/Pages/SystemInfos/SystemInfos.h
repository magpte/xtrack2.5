#ifndef __SYSTEM_INFOS_PRESENTER_H
#define __SYSTEM_INFOS_PRESENTER_H

#include "SystemInfosView.h"
#include "SystemInfosModel.h"

namespace Page
{

class SystemInfos : public PageBase
{
public:

public:
    SystemInfos();
    virtual ~SystemInfos();

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
    void Update();
    void AttachEvent(lv_obj_t* obj);
    static void onTimerUpdate(lv_timer_t* timer);
    static void onEvent(lv_event_t* event);

private:
    SystemInfosView View;
    SystemInfosModel Model;
    lv_timer_t* timer;

    // Update() 本身按 1 秒一次跑（timer 的周期），天球图数据本来就被
    // GPS 那边节流成 5 秒才刷新一次（见 HAL_GPS.cpp 的 PCAS03 配置），
    // 没必要跟着 1 秒一起问，这个计数器用来把天球图的刷新频率降到
    // 每 5 次 Update() 才问一次。
    uint8_t skyUpdateCounter;
};

}

#endif
