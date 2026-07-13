#include "SystemInfosView.h"
#include <math.h>

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
        "Speed"
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

    Group_Init();
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

void SystemInfosView::Delete()
{
    lv_group_set_focus_cb(lv_group_get_default(), nullptr);
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

    /* 十字线（正北-正南、正东-正西），点坐标要一直有效，用 static */
    static lv_point_t crossV[] = { {SKY_PLOT_SIZE / 2, 0}, {SKY_PLOT_SIZE / 2, SKY_PLOT_SIZE} };
    static lv_point_t crossH[] = { {0, SKY_PLOT_SIZE / 2}, {SKY_PLOT_SIZE, SKY_PLOT_SIZE / 2} };

    lv_obj_t* lineV = lv_line_create(plot);
    lv_obj_enable_style_refresh(false);
    lv_line_set_points(lineV, crossV, 2);
    lv_obj_set_style_line_width(lineV, 1, 0);
    lv_obj_set_style_line_color(lineV, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_line_opa(lineV, LV_OPA_40, 0);
    lv_obj_clear_flag(lineV, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(lineV, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lineH = lv_line_create(plot);
    lv_obj_enable_style_refresh(false);
    lv_line_set_points(lineH, crossH, 2);
    lv_obj_set_style_line_width(lineH, 1, 0);
    lv_obj_set_style_line_color(lineH, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_line_opa(lineH, LV_OPA_40, 0);
    lv_obj_clear_flag(lineH, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(lineH, LV_OBJ_FLAG_CLICKABLE);

    /* 正北标注 */
    lv_obj_t* nLabel = lv_label_create(plot);
    lv_obj_enable_style_refresh(false);
    lv_label_set_text(nLabel, "N");
    lv_obj_set_style_text_color(nLabel, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(nLabel, LV_ALIGN_TOP_MID, 0, -2);

    /* 卫星点对象池：预先建好、默认全部隐藏，SetSky() 里按需定位/显示，
     * 避免每次刷新都创建/删除对象（更省 RAM、也更快）。 */
    for (int i = 0; i < SKY_MAX_SATELLITES; i++)
    {
        lv_obj_t* dot = lv_obj_create(plot);
        lv_obj_enable_style_refresh(false);
        lv_obj_remove_style_all(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        sky.dots[i] = dot;
    }

    lv_obj_move_foreground(icon);
    lv_obj_enable_style_refresh(true);

    lv_coord_t height = LV_MAX(SKY_PLOT_SIZE, ITEM_HEIGHT_MIN);
    lv_obj_set_height(cont, height);
    lv_obj_set_height(icon, height);
}

void SystemInfosView::SetSky(HAL::Sky_Info_t* info)
{
    lv_coord_t c = SKY_PLOT_SIZE / 2;   // 圆心 = 仰角 90°（正头顶）

    uint8_t shown = info->count;
    if (shown > SKY_MAX_SATELLITES)
    {
        shown = SKY_MAX_SATELLITES;
    }

    for (int i = 0; i < SKY_MAX_SATELLITES; i++)
    {
        if (i >= shown)
        {
            lv_obj_add_flag(sky.dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        HAL::Sky_Satellite_t* sat = &info->satellites[i];

        // 仰角 90°（正头顶）在圆心，仰角 0°（地平线）在圆周边缘。
        float r = (float)c * (90 - sat->elevation) / 90.0f;
        float rad = sat->azimuth * 0.017453292f;  // 角度转弧度
        lv_coord_t x = c + (lv_coord_t)(r * sinf(rad));
        lv_coord_t y = c - (lv_coord_t)(r * cosf(rad));

        // 信噪比映射成点的直径：0 dB-Hz 最小点，40+ dB-Hz 最大点。
        uint8_t snr = sat->snr;
        if (snr > 40) snr = 40;
        lv_coord_t dotSize = SKY_DOT_MIN + (SKY_DOT_MAX - SKY_DOT_MIN) * snr / 40;

        lv_color_t color;
        switch (sat->constellation)
        {
        case HAL::SKY_CONSTELLATION_GPS:     color = lv_palette_main(LV_PALETTE_GREEN); break;
        case HAL::SKY_CONSTELLATION_BDS:     color = lv_palette_main(LV_PALETTE_RED); break;
        case HAL::SKY_CONSTELLATION_GLONASS: color = lv_palette_main(LV_PALETTE_BLUE); break;
        default:                             color = lv_palette_main(LV_PALETTE_GREY); break;
        }

        lv_obj_t* dot = sky.dots[i];
        lv_obj_set_size(dot, dotSize, dotSize);
        lv_obj_set_pos(dot, x - dotSize / 2, y - dotSize / 2);
        lv_obj_set_style_bg_color(dot, color, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
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
    float lat,
    float lng,
    float alt,
    const char* utc,
    float course,
    float speed
)
{
    lv_label_set_text_fmt(
        ui.gps.labelData,
        "%0.6f\n"
        "%0.6f\n"
        "%0.2fm\n"
        "%s\n"
        "%0.1f deg\n"
        "%0.1fkm/h",
        lat,
        lng,
        alt,
        utc,
        course,
        speed
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
