#include "SystemInfosView.h"
#include <math.h>

#if defined(__ARM_ARCH) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
#  include "arm_math.h"
#endif

using namespace Page;

#define ITEM_HEIGHT_MIN   100
#define ITEM_PAD          ((LV_VER_RES - ITEM_HEIGHT_MIN) / 2)

void SystemInfosView::Create(lv_obj_t* root)
{
    lv_obj_set_style_pad_ver(root, ITEM_PAD, 0);

    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        root,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER
    );

    Style_Init();

    /* Item Sport */
    Item_Create(
        &ui.sport,
        root,
        "Sport",
        "bicycle",

        "Total trip\n"
        "Total time\n"
        "Max speed"
    );

    /* Item GPS */
    Item_Create(
        &ui.gps,
        root,
        "GPS",
        "map_location",

        "Latitude\n"
        "Longitude\n"
        "Altitude\n"
        "UTC Time\n\n"
        "Course\n"
        "Speed\n"
        "PDOP"
    );

    /* Sky View */
    SkyPlot_Create(root);

    /* Item IMU */
    Item_Create(
        &ui.imu,
        root,
        "IMU",
        "gyroscope",

        "Step\n"
        "Ax\n"
        "Ay\n"
        "Az\n"
        "Gx\n"
        "Gy\n"
        "Gz"
    );

    /* Item RTC */
    Item_Create(
        &ui.rtc,
        root,
        "RTC",
        "time_info",

        "Date\n"
        "Time"
    );

    /* Item Battery */
    Item_Create(
        &ui.battery,
        root,
        "Battery",
        "battery_info",

        "Usage\n"
        "Voltage\n"
        "Status"
    );

    /* Item Storage */
    Item_Create(
        &ui.storage,
        root,
        "Storage",
        "storage",

        "Status\n"
        "Size\n"
        "Type\n"
        "Version"
    );

    /* Item System */
    Item_Create(
        &ui.system,
        root,
        "System",
        "system_info",

        "Firmware\n"
        "Author\n"
        "LVGL\n"
        "SysTick\n"
        "Compiler\n\n"
        "Build\n"
    );
}

void SystemInfosView::Group_Init()
{
    lv_group_t* group = lv_group_get_default();
    lv_group_set_wrap(group, true);
    lv_group_set_focus_cb(group, onFocus);

    // 逆序加入分组，编码器操作顺序更顺手（沿用原来的写法）。
    // sky 不在 ui 结构体里（结构跟 item_t 对不上，见头文件里的注释），
    // 不能再用"把整个结构体当数组"那个技巧了，手动按视觉顺序插进
    // imu 和 gps 之间，让它在导航顺序上排在 GPS 后面、IMU 前面。
    lv_group_add_obj(group, ui.system.icon);
    lv_group_add_obj(group, ui.storage.icon);
    lv_group_add_obj(group, ui.battery.icon);
    lv_group_add_obj(group, ui.rtc.icon);
    lv_group_add_obj(group, ui.imu.icon);
    lv_group_add_obj(group, sky.icon);
    lv_group_add_obj(group, ui.gps.icon);
    lv_group_add_obj(group, ui.sport.icon);

    lv_group_focus_obj(ui.sport.icon);
}

void SystemInfosView::Group_Deinit()
{
    // 必须在本页消失前（onViewWillDisappear）做，而不是等到页面卸载。
    // PageManager 先调旧页的 onViewWillDisappear、再调新页的
    // onViewWillAppear，页面真正被删除（onViewUnload）则是切换动画
    // 结束之后。如果到那时候才清，Dialplate 重新把自己的按钮加进
    // 分组并 lv_group_focus_obj() 时，焦点回调还是本页的 onFocus，
    // 会拿 Dialplate 的按钮去算滚动位置，把 Dialplate 的 root 滚走。
    lv_group_t* group = lv_group_get_default();
    lv_group_set_focus_cb(group, nullptr);
    lv_group_remove_all_objs(group);
}

void SystemInfosView::Delete()
{
    Style_Reset();
}

void SystemInfosView::SetScrollToY(lv_obj_t* obj, lv_coord_t y, lv_anim_enable_t en)
{
    lv_coord_t scroll_y = lv_obj_get_scroll_y(obj);
    lv_coord_t diff = -y + scroll_y;

    lv_obj_scroll_by(obj, 0, diff, en);
}

void SystemInfosView::onFocus(lv_group_t* g)
{
    lv_obj_t* icon = lv_group_get_focused(g);
    if (icon == nullptr)
    {
        return;
    }

    lv_obj_t* cont = lv_obj_get_parent(icon);
    lv_coord_t y = lv_obj_get_y(cont);
    lv_obj_scroll_to_y(lv_obj_get_parent(cont), y, LV_ANIM_ON);
}

void SystemInfosView::Style_Init()
{
    lv_style_init(&style.icon);
    lv_style_set_width(&style.icon, 220);
    lv_style_set_bg_color(&style.icon, lv_color_black());
    lv_style_set_bg_opa(&style.icon, LV_OPA_COVER);
    lv_style_set_text_font(&style.icon, ResourcePool::GetFont("bahnschrift_17"));
    lv_style_set_text_color(&style.icon, lv_color_white());

    lv_style_init(&style.focus);
    lv_style_set_width(&style.focus, 70);
    lv_style_set_border_side(&style.focus, LV_BORDER_SIDE_RIGHT);
    lv_style_set_border_width(&style.focus, 2);
    lv_style_set_border_color(&style.focus, lv_color_hex(0xff931e));

    static const lv_style_prop_t style_prop[] =
    {
        LV_STYLE_WIDTH,
        LV_STYLE_PROP_INV
    };

    static lv_style_transition_dsc_t trans;
    lv_style_transition_dsc_init(
        &trans,
        style_prop,
        lv_anim_path_overshoot,
        200,
        0,
        nullptr
    );
    lv_style_set_transition(&style.focus, &trans);
    lv_style_set_transition(&style.icon, &trans);

    lv_style_init(&style.info);
    lv_style_set_text_font(&style.info, ResourcePool::GetFont("bahnschrift_13"));
    lv_style_set_text_color(&style.info, lv_color_hex(0x999999));

    lv_style_init(&style.data);
    lv_style_set_text_font(&style.data, ResourcePool::GetFont("bahnschrift_13"));
    lv_style_set_text_color(&style.data, lv_color_white());
}

void SystemInfosView::Style_Reset()
{
    lv_style_reset(&style.icon);
    lv_style_reset(&style.info);
    lv_style_reset(&style.data);
    lv_style_reset(&style.focus);
}

void SystemInfosView::Item_Create(
    item_t* item,
    lv_obj_t* par,
    const char* name,
    const char* img_src,
    const char* infos
)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_enable_style_refresh(false);
    lv_obj_remove_style_all(cont);
    lv_obj_set_width(cont, 220);

    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    item->cont = cont;

    /* icon */
    lv_obj_t* icon = lv_obj_create(cont);
    lv_obj_enable_style_refresh(false);
    lv_obj_remove_style_all(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_style(icon, &style.icon, 0);
    lv_obj_add_style(icon, &style.focus, LV_STATE_FOCUSED);
    lv_obj_set_style_align(icon, LV_ALIGN_LEFT_MID, 0);

    lv_obj_set_flex_flow(icon, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        icon,
        LV_FLEX_ALIGN_SPACE_AROUND,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t* img = lv_img_create(icon);
    lv_obj_enable_style_refresh(false);
    lv_img_set_src(img, ResourcePool::GetImage(img_src));

    lv_obj_t* label = lv_label_create(icon);
    lv_obj_enable_style_refresh(false);
    lv_label_set_text(label, name);
    item->icon = icon;

    /* infos */
    label = lv_label_create(cont);
    lv_obj_enable_style_refresh(false);
    lv_label_set_text(label, infos);
    lv_obj_add_style(label, &style.info, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 75, 0);
    item->labelInfo = label;

    /* datas */
    label = lv_label_create(cont);
    lv_obj_enable_style_refresh(false);
    lv_label_set_text(label, "-");
    lv_obj_add_style(label, &style.data, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 60, 0);
    item->labelData = label;

    lv_obj_move_foreground(icon);
    lv_obj_enable_style_refresh(true);

    /* get real max height */
    lv_obj_update_layout(item->labelInfo);
    lv_coord_t height = lv_obj_get_height(item->labelInfo);
    height = LV_MAX(height, ITEM_HEIGHT_MIN);
    lv_obj_set_height(cont, height);
    lv_obj_set_height(icon, height);
}

#define SKY_PLOT_SIZE  130
#define SKY_DOT_MIN    4
#define SKY_DOT_MAX    10

void SystemInfosView::SkyPlot_Create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_enable_style_refresh(false);
    lv_obj_remove_style_all(cont);
    lv_obj_set_width(cont, 220);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    sky.cont = cont;

    /* icon —— 跟其他条目左侧一样的图标+标题，保持视觉风格统一 */
    lv_obj_t* icon = lv_obj_create(cont);
    lv_obj_enable_style_refresh(false);
    lv_obj_remove_style_all(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_style(icon, &style.icon, 0);
    lv_obj_add_style(icon, &style.focus, LV_STATE_FOCUSED);
    lv_obj_set_style_align(icon, LV_ALIGN_LEFT_MID, 0);

    lv_obj_set_flex_flow(icon, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        icon,
        LV_FLEX_ALIGN_SPACE_AROUND,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t* img = lv_img_create(icon);
    lv_obj_enable_style_refresh(false);
    // "satellite" 素材原始只有 13x13，放大后糊得厉害；换成 "compass"，
    // 原始尺寸 36x36，跟其他图标（gyroscope/map_location/system_info）
    // 大小一致，不需要缩放，而且语义上也更贴切（方向/天球图）。
    lv_img_set_src(img, ResourcePool::GetImage("compass"));

    lv_obj_t* label = lv_label_create(icon);
    lv_obj_enable_style_refresh(false);
    lv_label_set_text(label, "Sky View");
    sky.icon = icon;

    /* plot —— 圆形底盘，卫星点画在上面 */
    lv_obj_t* plot = lv_obj_create(cont);
    lv_obj_enable_style_refresh(false);
    lv_obj_remove_style_all(plot);
    lv_obj_set_size(plot, SKY_PLOT_SIZE, SKY_PLOT_SIZE);
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(plot, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_radius(plot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(plot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_20, 0);
    lv_obj_set_style_border_width(plot, 1, 0);
    lv_obj_set_style_border_color(plot, lv_palette_main(LV_PALETTE_GREY), 0);
    sky.plot = plot;

    /* 30°/60° 仰角参考圈，纯装饰，不需要交互 */
    for (int r = 1; r <= 2; r++)
    {
        lv_obj_t* ring = lv_obj_create(plot);
        lv_obj_enable_style_refresh(false);
        lv_obj_remove_style_all(ring);
        lv_coord_t d = SKY_PLOT_SIZE * r / 3;
        lv_obj_set_size(ring, d, d);
        lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_40, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    }

    /* 卫星点在 plot 的绘制事件里直接画出来，不占用对象/堆内存。
     * DRAW_POST_END 是子对象（参考圈）画完之后才触发的，
     * 保证十字线、正北红色箭头和卫星点都在最上层绘制。 */
    memset(&sky.info, 0, sizeof(sky.info));
    sky.course = 0.0f;
    lv_obj_add_event_cb(plot, onSkyPlotDraw, LV_EVENT_DRAW_POST_END, this);

    lv_obj_move_foreground(icon);
    lv_obj_enable_style_refresh(true);

    lv_coord_t height = LV_MAX(SKY_PLOT_SIZE, ITEM_HEIGHT_MIN);
    lv_obj_set_height(cont, height);
    lv_obj_set_height(icon, height);
}

void SystemInfosView::SetSky(HAL::Sky_Info_t* info)
{
    sky.info = *info;

    if (sky.info.count > SKY_MAX_SATELLITES)
    {
        sky.info.count = SKY_MAX_SATELLITES;
    }

    lv_obj_invalidate(sky.plot);
}

void SystemInfosView::SetSkyCourse(float course)
{
    if (sky.course != course)
    {
        sky.course = course;
        lv_obj_invalidate(sky.plot);
    }
}

void SystemInfosView::onSkyPlotDraw(lv_event_t* event)
{
    SystemInfosView* view = (SystemInfosView*)lv_event_get_user_data(event);
    lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(event);

    lv_area_t plotArea;
    lv_obj_get_coords(view->sky.plot, &plotArea);

    lv_coord_t c = SKY_PLOT_SIZE / 2;   // 圆心 = 仰角 90°（正头顶）
    lv_coord_t cx = plotArea.x1 + c;
    lv_coord_t cy = plotArea.y1 + c;

    // 正北的相对航向角度为 -course
    int16_t rel_angle_N = (int16_t)(-view->sky.course);
    while (rel_angle_N < 0) rel_angle_N += 360;
    while (rel_angle_N >= 360) rel_angle_N -= 360;

    int32_t sin_N = (int32_t)lv_trigo_sin(rel_angle_N);
    int32_t cos_N = (int32_t)lv_trigo_cos(rel_angle_N);

    // 1. 绘制随 Course 旋转的 8 等分分割线 (每 45° 一条线，共 4 条穿过圆心的轴线)
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x666666);
    line_dsc.width = 1;

    for (int i = 0; i < 4; i++)
    {
        int16_t angle = rel_angle_N + i * 45;
        while (angle < 0) angle += 360;
        while (angle >= 360) angle -= 360;

        int32_t sin_a = (int32_t)lv_trigo_sin(angle);
        int32_t cos_a = (int32_t)lv_trigo_cos(angle);

        line_dsc.opa = (i % 2 == 0) ? LV_OPA_60 : LV_OPA_30;

        lv_point_t p1 = { (lv_coord_t)(cx + (((int32_t)(c - 4) * sin_a) >> 15)), (lv_coord_t)(cy - (((int32_t)(c - 4) * cos_a) >> 15)) };
        lv_point_t p2 = { (lv_coord_t)(cx - (((int32_t)(c - 4) * sin_a) >> 15)), (lv_coord_t)(cy + (((int32_t)(c - 4) * cos_a) >> 15)) };

        lv_draw_line(draw_ctx, &line_dsc, &p1, &p2);
    }

    // 2. 绘制中心天顶十字准星 (Zenith Cross at Elevation 90°)
    {
        lv_draw_line_dsc_t zen_dsc;
        lv_draw_line_dsc_init(&zen_dsc);
        zen_dsc.color = lv_color_hex(0x888888);
        zen_dsc.width = 1;
        zen_dsc.opa = LV_OPA_70;

        lv_point_t zh1 = { (lv_coord_t)(cx - 4), cy };
        lv_point_t zh2 = { (lv_coord_t)(cx + 4), cy };
        lv_draw_line(draw_ctx, &zen_dsc, &zh1, &zh2);

        lv_point_t zv1 = { cx, (lv_coord_t)(cy - 4) };
        lv_point_t zv2 = { cx, (lv_coord_t)(cy + 4) };
        lv_draw_line(draw_ctx, &zen_dsc, &zv1, &zv2);
    }

    // 3. 绘制正北红色向外箭头标识
    lv_point_t arrow_pts[3];
    arrow_pts[0].x = (lv_coord_t)(cx + (((int32_t)(c - 2) * sin_N) >> 15));
    arrow_pts[0].y = (lv_coord_t)(cy - (((int32_t)(c - 2) * cos_N) >> 15));

    int32_t bx = cx + (((int32_t)(c - 10) * sin_N) >> 15);
    int32_t by = cy - (((int32_t)(c - 10) * cos_N) >> 15);

    arrow_pts[1].x = (lv_coord_t)(bx - (((int32_t)4 * cos_N) >> 15));
    arrow_pts[1].y = (lv_coord_t)(by - (((int32_t)4 * sin_N) >> 15));

    arrow_pts[2].x = (lv_coord_t)(bx + (((int32_t)4 * cos_N) >> 15));
    arrow_pts[2].y = (lv_coord_t)(by + (((int32_t)4 * sin_N) >> 15));

    lv_draw_rect_dsc_t arrow_dsc;
    lv_draw_rect_dsc_init(&arrow_dsc);
    arrow_dsc.bg_color = lv_color_hex(0xE74C3C);
    arrow_dsc.bg_opa = LV_OPA_COVER;
    lv_draw_polygon(draw_ctx, &arrow_dsc, arrow_pts, 3);

    // 4. 绘制卫星点（根据 Course 方向旋转，区分已锁定高饱和实心 vs 跟踪中半透明 vs 未锁定/仅在视野虚化镂空）
    for (int i = 0; i < view->sky.info.count; i++)
    {
        HAL::Sky_Satellite_t* sat = &view->sky.info.satellites[i];

        // 仰角 90°（正头顶）在圆心，仰角 0°（地平线）在圆周边缘。
        int32_t r = (int32_t)(c - 6) * (90 - (int32_t)sat->elevation) / 90;

        // 相对角度 = 卫星绝对方位角 - Course 航向角
        int16_t rel_angle = (int16_t)(sat->azimuth - view->sky.course);
        while (rel_angle < 0) rel_angle += 360;
        while (rel_angle >= 360) rel_angle -= 360;

        int32_t sin_val = (int32_t)lv_trigo_sin(rel_angle);
        int32_t cos_val = (int32_t)lv_trigo_cos(rel_angle);
        lv_coord_t x = c + (lv_coord_t)(((int32_t)r * sin_val) >> 15);
        lv_coord_t y = c - (lv_coord_t)(((int32_t)r * cos_val) >> 15);

        // 星座专属鲜明色彩
        lv_color_t satColor;
        switch (sat->constellation)
        {
        case HAL::SKY_CONSTELLATION_GPS:     satColor = lv_color_hex(0x2ECC71); break; // 鲜绿 (GPS)
        case HAL::SKY_CONSTELLATION_BDS:     satColor = lv_color_hex(0xE74C3C); break; // 艳红 (北斗)
        case HAL::SKY_CONSTELLATION_GLONASS: satColor = lv_color_hex(0x3498DB); break; // 天蓝 (GLONASS)
        default:                             satColor = lv_color_hex(0xF39C12); break; // 琥珀黄 (Galileo/其他)
        }

        uint8_t snr = sat->snr;
        lv_draw_rect_dsc_t sat_dsc;
        lv_draw_rect_dsc_init(&sat_dsc);
        sat_dsc.radius = LV_RADIUS_CIRCLE;

        lv_coord_t dotSize;

        if (snr > 0)
        {
            // --- 已捕获信号/已锁定卫星 (Solid Circle) ---
            if (snr > 40) snr = 40;
            dotSize = SKY_DOT_MIN + (SKY_DOT_MAX - SKY_DOT_MIN) * snr / 40;

            sat_dsc.bg_color = satColor;
            sat_dsc.bg_opa   = (snr >= 20) ? LV_OPA_COVER : LV_OPA_70;
            sat_dsc.border_width = 1;
            sat_dsc.border_color = lv_color_white();
            sat_dsc.border_opa   = (snr >= 25) ? LV_OPA_80 : LV_OPA_40;
        }
        else
        {
            // --- 仅在视野内未捕获/未锁定虚星 (Hollow / Outlined Ring) ---
            dotSize = 5;
            sat_dsc.bg_opa       = LV_OPA_TRANSP;
            sat_dsc.border_width = 1;
            sat_dsc.border_color = satColor;
            sat_dsc.border_opa   = LV_OPA_50;
        }

        lv_area_t dotArea;
        dotArea.x1 = plotArea.x1 + x - dotSize / 2;
        dotArea.y1 = plotArea.y1 + y - dotSize / 2;
        dotArea.x2 = dotArea.x1 + dotSize - 1;
        dotArea.y2 = dotArea.y1 + dotSize - 1;

        lv_draw_rect(draw_ctx, &sat_dsc, &dotArea);
    }
}

void SystemInfosView::SetSport(
    float trip,
    const char* time,
    float maxSpd
)
{
    lv_label_set_text_fmt(
        ui.sport.labelData,
        "%0.2fkm\n"
        "%s\n"
        "%0.1fkm/h",
        trip,
        time,
        maxSpd
    );
}

void SystemInfosView::SetGPS(
    double lat,
    double lng,
    float alt,
    const char* utc,
    float course,
    float speed,
    float pdop
)
{
    lv_label_set_text_fmt(
        ui.gps.labelData,
        "%0.6f\n"
        "%0.6f\n"
        "%0.2fm\n"
        "%s\n"
        "%0.1f deg\n"
        "%0.1fkm/h\n"
        "%0.1f",
        lat,
        lng,
        alt,
        utc,
        course,
        speed,
        pdop
    );
}

void SystemInfosView::SetIMU(
    int step,
    const char* info
)
{
    lv_label_set_text_fmt(
        ui.imu.labelData,
        "%d\n"
        "%s",
        step,
        info
    );
}

void SystemInfosView::SetRTC(
    const char* dateTime
)
{
    lv_label_set_text(
        ui.rtc.labelData,
        dateTime
    );
}

void SystemInfosView::SetBattery(
    int usage,
    float voltage,
    const char* state
)
{
    lv_label_set_text_fmt(
        ui.battery.labelData,
        "%d%%\n"
        "%0.2fV\n"
        "%s",
        usage,
        voltage,
        state
    );
}

void SystemInfosView::SetStorage(
    const char* detect,
    const char* size,
    const char* type,
    const char* version
)
{
    lv_label_set_text_fmt(
        ui.storage.labelData,
        "%s\n"
        "%s\n"
        "%s\n"
        "%s",
        detect,
        size,
        type,
        version
    );
}

void SystemInfosView::SetSystem(
    const char* firmVer,
    const char* authorName,
    const char* lvglVer,
    const char* bootTime,
    const char* compilerName,
    const char* bulidTime
)
{
    lv_label_set_text_fmt(
        ui.system.labelData,
        "%s\n"
        "%s\n"
        "%s\n"
        "%s\n"
        "%s\n"
        "%s",
        firmVer,
        authorName,
        lvglVer,
        bootTime,
        compilerName,
        bulidTime
    );
}
