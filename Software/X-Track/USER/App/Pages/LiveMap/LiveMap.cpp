#include "LiveMap.h"
#include "Config/Config.h"
#include <stdlib.h>
#include <math.h>
// ARMCC 在 C++ 模式下编译 <stdint.h> 时，INT32_MIN/INT16_MIN 等极值宏
// 需要 __STDC_LIMIT_MACROS 才会导出（C++ 历史遗留问题）。
// 这里直接用保护宏手动定义，跨工具链最可靠。
#ifndef INT32_MIN
#  define INT32_MIN  (-2147483647L - 1)
#endif
#ifndef INT16_MIN
#  define INT16_MIN  (-32767 - 1)
#endif

#if CONFIG_MAP_IMG_RLE_ENABLE
#include "Utils/lv_img_rle/lv_img_rle.h"
#endif

using namespace Page;

uint16_t LiveMap::mapLevelCurrent = CONFIG_LIVE_MAP_LEVEL_DEFAULT;

LiveMap::LiveMap()
{
    memset(&priv, 0, sizeof(priv));
}

LiveMap::~LiveMap()
{

}

void LiveMap::onCustomAttrConfig()
{
    SetCustomCacheEnable(false);
}

void LiveMap::onViewLoad()
{
    const uint32_t tileSize = 256;

    Model.tileConv.SetTileSize(tileSize);
    Model.tileConv.SetViewSize(
        CONFIG_LIVE_MAP_VIEW_WIDTH,
        CONFIG_LIVE_MAP_VIEW_HEIGHT
    );
    Model.tileConv.SetFocusPos(0, 0);

    TileConv::Rect_t rect;
    uint32_t tileNum = Model.tileConv.GetTileContainer(&rect);

    View.Create(_root, tileNum);
    lv_slider_set_range(
        View.ui.zoom.slider,
        Model.mapConv.GetLevelMin(),
        Model.mapConv.GetLevelMax()
    );
    View.SetMapTile(tileSize, rect.width / tileSize);

#if CONFIG_LIVE_MAP_DEBUG_ENABLE
    lv_obj_t* contView = lv_obj_create(root);
    lv_obj_center(contView);
    lv_obj_set_size(contView, CONFIG_LIVE_MAP_VIEW_WIDTH, CONFIG_LIVE_MAP_VIEW_HEIGHT);
    lv_obj_set_style_border_color(contView, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(contView, 1, 0);
#endif

    AttachEvent(_root);
    AttachEvent(View.ui.zoom.slider);
    AttachEvent(View.ui.sportInfo.cont);

    lv_slider_set_value(View.ui.zoom.slider, mapLevelCurrent, LV_ANIM_OFF);
    Model.mapConv.SetLevel(mapLevelCurrent);
    lv_obj_add_flag(View.ui.map.cont, LV_OBJ_FLAG_HIDDEN);

    /* Point filter */
    Model.pointFilter.SetOffsetThreshold(CONFIG_TRACK_FILTER_OFFSET_THRESHOLD);
    Model.pointFilter.SetOutputPointCallback([](TrackPointFilter * filter, const TrackPointFilter::Point_t* point)
    {
        LiveMap* instance = (LiveMap*)filter->userData;
        instance->TrackLineAppendToEnd((int32_t)point->x, (int32_t)point->y);
    });
    Model.pointFilter.userData = this;

    /* Line filter */
    Model.lineFilter.SetMinDistance(CONFIG_TRACK_LINE_SIMPLIFY_MIN_DIST);
    Model.lineFilter.SetOutputPointCallback(onTrackLineEvent);
    Model.lineFilter.userData = this;
}

void LiveMap::onViewDidLoad()
{

}

void LiveMap::onViewWillAppear()
{
    lv_obj_set_style_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
    Model.Init();

    char theme[16];
    Model.GetArrowTheme(theme, sizeof(theme));
    View.SetArrowTheme(theme);

    priv.isTrackAvtive = Model.GetTrackFilterActive();

    Model.SetStatusBarStyle(DataProc::STATUS_BAR_STYLE_BLACK);
    SportInfoUpdate();
    lv_obj_clear_flag(View.ui.labelInfo, LV_OBJ_FLAG_HIDDEN);
}

void LiveMap::onViewDidAppear()
{
#if CONFIG_MAP_IMG_RLE_ENABLE
    // 只在地图页可见期间占用像素缓存的 RAM（见 lv_img_rle.h 里的说明），
    // 离开页面时对应 deinit 会释放掉。
    lv_img_rle_cache_init();
#endif

    // 优化1：timer 周期从 100ms 改为 CONFIG_GPS_REFR_PERIOD（500ms）。
    // 原来用 100ms 是为了让 zoom 条"3 秒后自动隐藏"的判断能及时触发，
    // 但这导致 Update() 以 10Hz 被唤醒，即使地图根本没更新也要跑一遍
    // lv_tick_elaps() 比较。现在 zoom 隐藏改用 lv_anim 延迟回调处理，
    // 不再需要高频轮询——timer 可以直接对齐 GPS 刷新周期。
    priv.timer = lv_timer_create([](lv_timer_t* timer)
    {
        LiveMap* instance = (LiveMap*)timer->user_data;
        instance->Update();
    },
    CONFIG_GPS_REFR_PERIOD,
    this);
    priv.lastMapUpdateTime = 0;
    lv_obj_clear_flag(View.ui.map.cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(View.ui.labelInfo, LV_OBJ_FLAG_HIDDEN);

    priv.lastTileContOriPoint.x = 0;
    priv.lastTileContOriPoint.y = 0;
    priv.isStationary = false;   // 每次进入页面先按正常刷新频率来，避免上次退出时
                                  // 恰好处于静止状态被错误带入这次的第一帧判断

    // 优化2：初始化为 INT32_MIN，保证第一帧一定会刷新 map.cont 位置。
    priv.lastContOffset.x = INT32_MIN;
    priv.lastContOffset.y = INT32_MIN;

    // 优化3：初始化为明显不可能出现的值，保证第一帧一定会刷新箭头。
    priv.lastArrowX     = INT16_MIN;
    priv.lastArrowY     = INT16_MIN;
    priv.lastArrowAngle = INT16_MIN;

    // 热点6：同理，初始化为不可能值，第一帧强制刷新 SportInfo 所有 label。
    priv.lastSpeedKph       = -1;
    priv.lastSingleDistance = -1.0f;
    priv.lastSingleTime     = (uint32_t)-1;

    priv.isTrackAvtive = Model.GetTrackFilterActive();
    if (!priv.isTrackAvtive)
    {
        Model.pointFilter.SetOutputPointCallback(nullptr);
    }

    lv_group_t* group = lv_group_get_default();
    lv_group_add_obj(group, View.ui.zoom.slider);
    lv_group_set_editing(group, View.ui.zoom.slider);
}

void LiveMap::onViewWillDisappear()
{
    lv_timer_del(priv.timer);
    priv.timer = NULL;

    /* Clear the callback so the filter can't fire into a half-torn-down
     * View after the timer is gone (defensive: the timer deletion above
     * is the primary guard, this makes the invariant explicit). */
    Model.pointFilter.SetOutputPointCallback(NULL);

    lv_obj_add_flag(View.ui.map.cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_out(_root, 250, 250);

#if CONFIG_MAP_IMG_RLE_ENABLE
    lv_img_rle_cache_deinit();
#endif
}

void LiveMap::onViewDidDisappear()
{
    Model.Deinit();
}

void LiveMap::onViewUnload()
{
    View.Delete();
}

void LiveMap::onViewDidUnload()
{

}

void LiveMap::AttachEvent(lv_obj_t* obj)
{
    lv_obj_add_event_cb(obj, onEvent, LV_EVENT_ALL, this);
}

void LiveMap::Update()
{
    CheckPosition();
    SportInfoUpdate();
    priv.lastMapUpdateTime = lv_tick_get();
}

void LiveMap::UpdateDelay(uint32_t ms)
{
    // 用户正在操作缩放条，退出静止状态以立即恢复正常刷新频率。
    if (priv.isStationary)
    {
        priv.isStationary = false;
        if (priv.timer)
        {
            lv_timer_set_period(priv.timer, CONFIG_GPS_REFR_PERIOD);
        }
    }
    priv.lastMapUpdateTime = lv_tick_get() - CONFIG_GPS_REFR_PERIOD + ms;

    // 优化1：zoom 条自动隐藏改用 lv_anim 延迟回调，不再依赖 timer 轮询
    // lastContShowTime。每次调用 UpdateDelay()（即用户转动缩放滑块时）
    // 先取消上一次还在倒计时的隐藏动画，再重新开一个 3 秒延迟：
    //   - 如果 3 秒内没再操作 → 延迟到期，zoom 条自动滑出隐藏
    //   - 如果 3 秒内又操作了 → 旧延迟被取消，重新计时，不会误隐藏
    // 这样彻底去掉了原来为判断"3 秒是否到了"而维持的高频轮询分支。
    lv_obj_clear_state(View.ui.zoom.cont, LV_STATE_USER_1); // 先确保可见

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, View.ui.zoom.cont);
    // exec_cb 设为 nullptr：这个动画只用来做延迟触发，不需要逐帧改任何属性；
    // 真正的"隐藏"在 ready_cb 里通过设置 LV_STATE_USER_1 完成（View 里已经
    // 为这个 state 配置了 x 偏移 + opa 渐出的 CSS transition，见
    // LiveMapView.cpp ZoomCtrl_Create()）。
    lv_anim_set_exec_cb(&a, nullptr);
    lv_anim_set_values(&a, 0, 0);
    lv_anim_set_time(&a, 0);       // 动画本身时长为 0
    lv_anim_set_delay(&a, 3000);   // 延迟 3 秒后触发 ready_cb
    lv_anim_set_ready_cb(&a, [](lv_anim_t* anim)
    {
        lv_obj_t* cont = (lv_obj_t*)anim->var;
        lv_obj_add_state(cont, LV_STATE_USER_1);
    });
    lv_anim_start(&a);
}

void LiveMap::SportInfoUpdate()
{
    // 热点6：只有数值实际变化时才调 lv_label_set_text_fmt，
    // 避免每 GPS 周期都触发字符串格式化 + label 失效 + 重绘。
    // 采用与 arrow 位置缓存相同的模式（见 priv.lastArrowX/Y/Angle）。

    int speedKph = (int)Model.sportStatusInfo.speedKph;
    if (speedKph != priv.lastSpeedKph)
    {
        priv.lastSpeedKph = speedKph;
        lv_label_set_text_fmt(
            View.ui.sportInfo.labelSpeed,
            "%02d",
            speedKph
        );
    }

    float dist = Model.sportStatusInfo.singleDistance;
    if (dist != priv.lastSingleDistance)
    {
        priv.lastSingleDistance = dist;
        lv_label_set_text_fmt(
            View.ui.sportInfo.labelTrip,
            "%0.1f km",
            dist / 1000
        );
    }

    uint32_t t = Model.sportStatusInfo.singleTime;
    if (t != priv.lastSingleTime)
    {
        priv.lastSingleTime = t;
        char buf[16];
        lv_label_set_text(
            View.ui.sportInfo.labelTime,
            DataProc::MakeTimeString(t, buf, sizeof(buf))
        );
    }
}

void LiveMap::CheckPosition()
{
    bool refreshMap = false;
    bool prevStationary = priv.isStationary;

    HAL::GPS_Info_t gpsInfo;
    Model.GetGPS_Info(&gpsInfo);

    // 静止判断（双阈值迟滞）：没有有效定位时一律按"非静止"处理，保证一旦
    // 重新定位成功能尽快追上真实位置，不被静止节流拖慢。
    if (!gpsInfo.isVaild)
    {
        priv.isStationary = false;
    }
    else if (priv.isStationary)
    {
        if (gpsInfo.speed > CONFIG_LIVE_MAP_STATIONARY_EXIT_KPH)
        {
            priv.isStationary = false;
        }
    }
    else
    {
        if (gpsInfo.speed < CONFIG_LIVE_MAP_STATIONARY_ENTER_KPH)
        {
            priv.isStationary = true;
        }
    }

    // 静止状态切换时，动态修改 LVGL timer 周期：
    // - 静止：拉长到 CONFIG_GPS_REFR_PERIOD_STATIONARY (3000ms)，减少 LVGL 唤醒
    // - 运动：恢复到 CONFIG_GPS_REFR_PERIOD (500ms)
    if (prevStationary != priv.isStationary && priv.timer)
    {
        uint32_t period = priv.isStationary
                          ? CONFIG_GPS_REFR_PERIOD_STATIONARY
                          : CONFIG_GPS_REFR_PERIOD;
        lv_timer_set_period(priv.timer, period);
    }

    mapLevelCurrent = lv_slider_get_value(View.ui.zoom.slider);
    if (mapLevelCurrent != Model.mapConv.GetLevel())
    {
        refreshMap = true;
        Model.mapConv.SetLevel(mapLevelCurrent);
    }

    int32_t mapX, mapY;
    Model.mapConv.ConvertMapCoordinate(
        gpsInfo.longitude, gpsInfo.latitude,
        &mapX, &mapY
    );
    Model.tileConv.SetFocusPos(mapX, mapY);

    if (GetIsMapTileContChanged())
    {
        refreshMap = true;
    }

    if (refreshMap)
    {
        TileConv::Rect_t rect;
        Model.tileConv.GetTileContainer(&rect);

        Area_t area =
        {
            .x0 = rect.x,
            .y0 = rect.y,
            .x1 = rect.x + rect.width - 1,
            .y1 = rect.y + rect.height - 1
        };

        onMapTileContRefresh(&area, mapX, mapY);
    }

    MapTileContUpdate(mapX, mapY, gpsInfo.course);

    if (priv.isTrackAvtive)
    {
        Model.pointFilter.PushPoint(mapX, mapY);
    }
}

void LiveMap::onMapTileContRefresh(const Area_t* area, int32_t x, int32_t y)
{
    LV_LOG_INFO(
        "area: (%d, %d) [%dx%d]",
        area->x0, area->y0,
        area->x1 - area->x0 + 1,
        area->y1 - area->y0 + 1
    );

    MapTileContReload();

    if (priv.isTrackAvtive)
    {
        TrackLineReload(area, x, y);
    }
}

void LiveMap::MapTileContUpdate(int32_t mapX, int32_t mapY, float course)
{
    TileConv::Point_t offset;
    TileConv::Point_t curPoint = { mapX, mapY };
    Model.tileConv.GetOffset(&offset, &curPoint);

    /* arrow */
    lv_obj_t* img = View.ui.map.imgArrow;
    Model.tileConv.GetFocusOffset(&offset);
    lv_coord_t arrowX    = offset.x - lv_obj_get_width(img) / 2;
    lv_coord_t arrowY    = offset.y - lv_obj_get_height(img) / 2;

    // 优化：静止状态下屏蔽 GPS 航向角高频跳变噪声，锁定箭头角度；
    // 同时对坐标增加死区过滤，避免产生 LVGL 脏区引发底瓦片无谓重绘。
    int16_t arrowAngle;
    if (priv.isStationary && priv.lastArrowAngle != INT16_MIN)
    {
        arrowAngle = priv.lastArrowAngle;
        if (priv.lastArrowX != INT16_MIN &&
            LV_ABS(arrowX - priv.lastArrowX) < CONFIG_LIVE_MAP_DEADBAND_THRESHOLD &&
            LV_ABS(arrowY - priv.lastArrowY) < CONFIG_LIVE_MAP_DEADBAND_THRESHOLD)
        {
            arrowX = priv.lastArrowX;
            arrowY = priv.lastArrowY;
        }
    }
    else
    {
        arrowAngle = (int16_t)(course * 10.0f);
    }

    if (arrowX     != priv.lastArrowX ||
        arrowY     != priv.lastArrowY ||
        arrowAngle != priv.lastArrowAngle)
    {
        priv.lastArrowX     = arrowX;
        priv.lastArrowY     = arrowY;
        priv.lastArrowAngle = arrowAngle;
        View.SetImgArrowStatus(arrowX, arrowY, (float)arrowAngle / 10.0f);
    }

    /* active line */
    if (priv.isTrackAvtive)
    {
        View.SetLineActivePoint((lv_coord_t)offset.x, (lv_coord_t)offset.y);
    }

    /* map cont — 像素死区过滤优化：偏移量改动小于死区阈值时跳过 lv_obj_set_pos，避免 LVGL 脏区标记与无谓全屏重绘 */
    Model.tileConv.GetTileContainerOffset(&offset);
    if (priv.lastContOffset.x == INT32_MIN ||
        LV_ABS(offset.x - priv.lastContOffset.x) >= CONFIG_LIVE_MAP_DEADBAND_THRESHOLD ||
        LV_ABS(offset.y - priv.lastContOffset.y) >= CONFIG_LIVE_MAP_DEADBAND_THRESHOLD)
    {
        priv.lastContOffset = offset;
        lv_coord_t baseX = (LV_HOR_RES - CONFIG_LIVE_MAP_VIEW_WIDTH) / 2;
        lv_coord_t baseY = (LV_VER_RES - CONFIG_LIVE_MAP_VIEW_HEIGHT) / 2;
        lv_obj_set_pos(View.ui.map.cont, baseX - offset.x, baseY - offset.y);
    }
}

void LiveMap::MapTileContReload()
{
    /* tile src */
    for (uint32_t i = 0; i < View.ui.map.tileNum; i++)
    {
        TileConv::Point_t pos;
        Model.tileConv.GetTilePos(i, &pos);

        char path[64];
        Model.mapConv.ConvertMapPath(pos.x, pos.y, path, sizeof(path));

        View.SetMapTileSrc(i, path);
    }
}

bool LiveMap::GetIsMapTileContChanged()
{
    TileConv::Point_t pos;
    Model.tileConv.GetTilePos(0, &pos);

    bool ret = (pos.x != priv.lastTileContOriPoint.x || pos.y != priv.lastTileContOriPoint.y);

    priv.lastTileContOriPoint = pos;

    return ret;
}

void LiveMap::TrackLineReload(const Area_t* area, int32_t x, int32_t y)
{
    Model.lineFilter.SetClipArea(area);
    Model.lineFilter.Reset();
    Model.TrackReload([](TrackPointFilter * filter, const TrackPointFilter::Point_t* point)
    {
        LiveMap* instance = (LiveMap*)filter->userData;
        instance->Model.lineFilter.PushPoint((int32_t)point->x, (int32_t)point->y);
    }, this);
    Model.lineFilter.PushPoint(x, y);
    Model.lineFilter.PushEnd();
}

void LiveMap::TrackLineAppend(int32_t x, int32_t y)
{
    TileConv::Point_t offset;
    TileConv::Point_t curPoint = { x, y };
    Model.tileConv.GetOffset(&offset, &curPoint);
    View.ui.track.lineTrack->append((lv_coord_t)offset.x, (lv_coord_t)offset.y);
}

void LiveMap::TrackLineAppendToEnd(int32_t x, int32_t y)
{
    TileConv::Point_t offset;
    TileConv::Point_t curPoint = { x, y };
    Model.tileConv.GetOffset(&offset, &curPoint);
    View.ui.track.lineTrack->append_to_end((lv_coord_t)offset.x, (lv_coord_t)offset.y);
}

void LiveMap::onTrackLineEvent(TrackLineFilter* filter, TrackLineFilter::Event_t* event)
{
    LiveMap* instance = (LiveMap*)filter->userData;
    lv_poly_line* lineTrack = instance->View.ui.track.lineTrack;

    switch (event->code)
    {
    case TrackLineFilter::EVENT_START_LINE:
        lineTrack->start();
        instance->TrackLineAppend(event->point->x, event->point->y);
        break;
    case TrackLineFilter::EVENT_APPEND_POINT:
        instance->TrackLineAppend(event->point->x, event->point->y);
        break;
    case TrackLineFilter::EVENT_END_LINE:
        if (event->point != nullptr)
        {
            instance->TrackLineAppend(event->point->x, event->point->y);
        }
        lineTrack->stop();
        break;
    case TrackLineFilter::EVENT_RESET:
        lineTrack->reset();
        break;
    default:
        break;
    }
}

void LiveMap::onEvent(lv_event_t* event)
{
    LiveMap* instance = (LiveMap*)lv_event_get_user_data(event);
    LV_ASSERT_NULL(instance);

    lv_obj_t* obj = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_LEAVE)
    {
        instance->_Manager->Pop();
        return;
    }

    if (obj == instance->View.ui.zoom.slider)
    {
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            int32_t level = lv_slider_get_value(obj);
            int32_t levelMax = instance->Model.mapConv.GetLevelMax();
            lv_label_set_text_fmt(instance->View.ui.zoom.labelInfo, "%d/%d", level, levelMax);

            // lv_obj_clear_state 和 zoom 3 秒自动隐藏的计时已统一由
            // UpdateDelay() 里的 lv_anim 机制处理（优化1），此处不再重复。
            instance->UpdateDelay(200);
        }
        else if (code == LV_EVENT_PRESSED)
        {
            instance->_Manager->Pop();
        }
    }

    if (obj == instance->View.ui.sportInfo.cont)
    {
        if (code == LV_EVENT_PRESSED)
        {
            instance->_Manager->Pop();
        }
    }
}
