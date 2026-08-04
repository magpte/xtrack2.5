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

    // 天球图（Sky View）的刷新节流计数器：GSV 数据本身更新很慢
    // （模块 0.5Hz 发送，HAL 侧还要集齐三颗星座才切换快照），没必要
    // 每 200ms 都刷新，这个计数器把天球图的刷新频率降到每 25 次
    // Update()（即 5 秒）才问一次。计数器无条件递增，不管当前焦点
    // 是不是 Sky View，避免切换回来时出现等待延迟。
    uint8_t skyUpdateCounter;

    typedef struct
    {
        bool valid;
        lv_obj_t* lastFocused;
        char itemStr[128];
        uint8_t buffer[3900]; // 4KB SRAM 脏缓存空间，消除 5Hz 定时器盲目刷新与重绘
    } SystemInfosCache_t;

    SystemInfosCache_t cache;
};

}

#endif
