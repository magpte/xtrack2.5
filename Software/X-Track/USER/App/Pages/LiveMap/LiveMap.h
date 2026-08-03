#ifndef __LIVEMAP_PRESENTER_H
#define __LIVEMAP_PRESENTER_H

#include "LiveMapView.h"
#include "LiveMapModel.h"

namespace Page
{

class LiveMap : public PageBase
{
public:
    LiveMap();
    virtual ~LiveMap();

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
    LiveMapView View;
    LiveMapModel Model;

    struct
    {
        uint32_t lastMapUpdateTime;
        lv_timer_t* timer;
        TileConv::Point_t lastTileContOriPoint;
        bool isTrackAvtive;
        bool isStationary;   // 迟滞判断后的"静止"状态，见 CheckPosition()

        // 优化2：缓存上次 map.cont 的 tile 容器偏移，只有实际变化时才
        // 调 lv_obj_set_pos，避免静止时每帧都触发 LVGL 脏区标记。
        // 初始化成 INT32_MIN 保证第一帧一定会刷新。
        TileConv::Point_t lastContOffset;

        // 优化3：缓存上次箭头的位置和旋转角度（×10，同 lv_img_set_angle），
        // 只有发生变化时才调 SetImgArrowStatus，避免静止时重复触发图片
        // 旋转运算（lv_img_set_angle 内部有三角函数和像素变换）。
        lv_coord_t lastArrowX;
        lv_coord_t lastArrowY;
        int16_t    lastArrowAngle; // lv_img_set_angle 单位：0.1°
    } priv;


    static uint16_t mapLevelCurrent;

private:
    typedef  TrackLineFilter::Area_t Area_t;

private:
    void Update();
    void UpdateDelay(uint32_t ms);
    void CheckPosition();

    /* SportInfo */
    void SportInfoUpdate();

    /* MapTileCont */
    bool GetIsMapTileContChanged();
    void onMapTileContRefresh(const Area_t* area, int32_t x, int32_t y);
    void MapTileContUpdate(int32_t mapX, int32_t mapY, float course);
    void MapTileContReload();
    
    /* TrackLine */
    void TrackLineReload(const Area_t* area, int32_t x, int32_t y);
    void TrackLineAppend(int32_t x, int32_t y);
    void TrackLineAppendToEnd(int32_t x, int32_t y);
    static void onTrackLineEvent(TrackLineFilter* filter, TrackLineFilter::Event_t* event);
    
    void AttachEvent(lv_obj_t* obj);
    static void onEvent(lv_event_t* event);
};

}

#endif
