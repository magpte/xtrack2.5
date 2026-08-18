#include "DialplateView.h"
#include <stdarg.h>
#include <stdio.h>

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

using namespace Page;

void DialplateView::Create(lv_obj_t* root)
{
    BottomInfo_Create(root);
    TopInfo_Create(root);
    BtnCont_Create(root);
    Brightness_Create(root);

    ui.anim_timeline = lv_anim_timeline_create();

#define ANIM_DEF(start_time, obj, attr, start, end) \
    {start_time, obj, LV_ANIM_EXEC(attr), start, end, 500, lv_anim_path_ease_out, true}

    lv_coord_t y_tar_top = lv_obj_get_y(ui.topInfo.cont);
    lv_coord_t y_tar_bottom = lv_obj_get_y(ui.bottomInfo.cont);
    lv_coord_t h_tar_btn = lv_obj_get_height(ui.btnCont.btnRec);

    lv_anim_timeline_wrapper_t wrapper[] =
    {
        ANIM_DEF(0, ui.topInfo.cont, y, -lv_obj_get_height(ui.topInfo.cont), y_tar_top),

        ANIM_DEF(200, ui.bottomInfo.cont, y, -lv_obj_get_height(ui.bottomInfo.cont), y_tar_bottom),

        ANIM_DEF(500, ui.btnCont.btnMap, height, 0, h_tar_btn),
        ANIM_DEF(600, ui.btnCont.btnRec, height, 0, h_tar_btn),
        ANIM_DEF(700, ui.btnCont.btnMenu, height, 0, h_tar_btn),
        LV_ANIM_TIMELINE_WRAPPER_END
    };
    lv_anim_timeline_add_wrapper(ui.anim_timeline, wrapper);
}

void DialplateView::Delete()
{
    if(ui.anim_timeline)
    {
        lv_anim_timeline_del(ui.anim_timeline);
        ui.anim_timeline = nullptr;
    }
}

void DialplateView::TopInfo_Create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_HOR_RES, 165);

    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x333333), 0);

    lv_obj_set_style_radius(cont, 27, 0);
    lv_obj_set_y(cont, -36);
    ui.topInfo.cont = cont;

    lv_obj_t* label = lv_label_create(cont);
    lv_obj_set_style_text_font(label, ResourcePool::GetFont("bahnschrift_65"), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, "00");
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 77);
    ui.topInfo.labelSpeed = label;

    label = lv_label_create(cont);
    lv_obj_set_style_text_font(label, ResourcePool::GetFont("bahnschrift_17"), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, "km/h");
    lv_obj_align_to(label, ui.topInfo.labelSpeed, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    ui.topInfo.labelUint = label;
}

void DialplateView::BottomInfo_Create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_remove_style_all(cont);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_set_size(cont, LV_HOR_RES, 170);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 106);

    ui.bottomInfo.cont = cont;

    // Left container for SubInfo (AVG, Trip, Time vertical column)
    lv_obj_t* leftCont = lv_obj_create(cont);
    lv_obj_remove_style_all(leftCont);
    lv_obj_set_size(leftCont, 105, 120);
    lv_obj_align(leftCont, LV_ALIGN_LEFT_MID, 10, 12);

    lv_obj_set_flex_flow(leftCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        leftCont,
        LV_FLEX_ALIGN_SPACE_EVENLY,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    const char* unitText[3] =
    {
        "AVG",
        "Trip",
        "Time"
    };

    for (int i = 0; i < ARRAY_SIZE(ui.bottomInfo.labelInfoGrp); i++)
    {
        SubInfoGrp_Create(
            leftCont,
            &(ui.bottomInfo.labelInfoGrp[i]),
            unitText[i]
        );
    }

    // Right side SkyView Rotating Compass UI
    Compass_Create(cont);
}

void DialplateView::SubInfoGrp_Create(lv_obj_t* par, SubInfo_t* info, const char* unitText)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 95, 34);

    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        cont,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t* label = lv_label_create(cont);
    lv_obj_set_style_text_font(label, ResourcePool::GetFont("bahnschrift_17"), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    info->lableValue = label;

    label = lv_label_create(cont);
    lv_obj_set_style_text_font(label, ResourcePool::GetFont("bahnschrift_13"), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xb3b3b3), 0);
    lv_label_set_text(label, unitText);
    info->lableUnit = label;

    info->cont = cont;
}

void DialplateView::Compass_Create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 100, 100);
    lv_obj_align(cont, LV_ALIGN_RIGHT_MID, -20, 10);
    ui.compass.cont = cont;

    // Dial Plot Background (100x100px)
    lv_obj_t* dial = lv_obj_create(cont);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, 100, 100);
    lv_obj_center(dial);
    lv_obj_clear_flag(dial, LV_OBJ_FLAG_SCROLLABLE);
    ui.compass.dialBg = dial;

    // Static Outer Ring (100x100px)
    lv_obj_t* ring = lv_obj_create(dial);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 100, 100);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    ui.compass.ring = ring;

    // Line 1: Course Angle Number (bahnschrift_17)
    lv_obj_t* labelAngle = lv_label_create(cont);
    lv_obj_set_style_text_font(labelAngle, ResourcePool::GetFont("bahnschrift_17"), 0);
    lv_obj_set_style_text_color(labelAngle, lv_color_white(), 0);
    lv_label_set_text(labelAngle, "0");
    lv_obj_align(labelAngle, LV_ALIGN_CENTER, 0, 5);
    ui.compass.labelAngle = labelAngle;

    // Line 2: Direction Text (bahnschrift_13)
    lv_obj_t* labelDir = lv_label_create(cont);
    lv_obj_set_style_text_font(labelDir, ResourcePool::GetFont("bahnschrift_13"), 0);
    lv_obj_set_style_text_color(labelDir, lv_color_hex(0xaaaaaa), 0);
    lv_label_set_text(labelDir, "N");
    lv_obj_align(labelDir, LV_ALIGN_CENTER, 0, 24);
    ui.compass.labelDir = labelDir;

    // Register Draw Event Callback for Compass Center Red Pointer & N/S/E/W Markers
    compassCourse = -1.0f;
    lv_obj_add_event_cb(dial, onCompassDraw, LV_EVENT_DRAW_POST_END, this);
}

void DialplateView::SetCompassCourse(float course)
{
    while (course < 0) course += 360.0f;
    while (course >= 360.0f) course -= 360.0f;

    int16_t newAngle = (int16_t)course;
    int16_t oldAngle = (int16_t)compassCourse;
    if (newAngle != oldAngle)
    {
        compassCourse = course;
        if (ui.compass.dialBg)
        {
            lv_obj_invalidate(ui.compass.dialBg);
        }
    }
}

void DialplateView::onCompassDraw(lv_event_t* event)
{
    DialplateView* view = (DialplateView*)lv_event_get_user_data(event);
    lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(event);

    lv_area_t plotArea;
    lv_obj_get_coords(view->ui.compass.dialBg, &plotArea);

    lv_coord_t cx = plotArea.x1 + 50;
    lv_coord_t cy = plotArea.y1 + 50;
    lv_coord_t R = 42; // Outer Ring Radius

    float course = view->compassCourse;
    if (course < 0) course = 0.0f;

    int16_t rel_angle_N = (int16_t)(-course);
    while (rel_angle_N < 0) rel_angle_N += 360;
    while (rel_angle_N >= 360) rel_angle_N -= 360;

    // 1. Draw N Red Circle Badge
    int32_t sin_N = (int32_t)lv_trigo_sin(rel_angle_N);
    int32_t cos_N = (int32_t)lv_trigo_cos(rel_angle_N);
    lv_coord_t nx = cx + (lv_coord_t)(((int32_t)R * sin_N) >> 15);
    lv_coord_t ny = cy - (lv_coord_t)(((int32_t)R * cos_N) >> 15);

    lv_draw_rect_dsc_t n_bg_dsc;
    lv_draw_rect_dsc_init(&n_bg_dsc);
    n_bg_dsc.bg_color = lv_color_hex(0xE74C3C);
    n_bg_dsc.bg_opa = LV_OPA_COVER;
    n_bg_dsc.radius = LV_RADIUS_CIRCLE;

    lv_area_t n_area;
    n_area.x1 = nx - 8;
    n_area.y1 = ny - 8;
    n_area.x2 = n_area.x1 + 15;
    n_area.y2 = n_area.y1 + 15;
    lv_draw_rect(draw_ctx, &n_bg_dsc, &n_area);

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.font = ResourcePool::GetFont("bahnschrift_13");

    lv_area_t txt_area = n_area;
    txt_area.x1 += 3;
    txt_area.y1 += 1;
    lv_draw_label(draw_ctx, &label_dsc, &txt_area, "N", nullptr);

    // 2. Draw S, W, E Markers
    label_dsc.color = lv_color_hex(0xDDDDDD);
    const struct { int16_t add_angle; const char* txt; } markers[] = {
        { 180, "S" },
        { 270, "W" },
        {  90, "E" }
    };

    for (int i = 0; i < 3; i++)
    {
        int16_t angle = rel_angle_N + markers[i].add_angle;
        while (angle < 0) angle += 360;
        while (angle >= 360) angle -= 360;

        int32_t sin_m = (int32_t)lv_trigo_sin(angle);
        int32_t cos_m = (int32_t)lv_trigo_cos(angle);
        lv_coord_t mx = cx + (lv_coord_t)(((int32_t)R * sin_m) >> 15);
        lv_coord_t my = cy - (lv_coord_t)(((int32_t)R * cos_m) >> 15);

        lv_area_t m_area;
        m_area.x1 = mx - 5;
        m_area.y1 = my - 7;
        m_area.x2 = m_area.x1 + 12;
        m_area.y2 = m_area.y1 + 14;
        lv_draw_label(draw_ctx, &label_dsc, &m_area, markers[i].txt, nullptr);
    }

    // 3. Draw Center Red Pointer Triangle (3 points, zero dynamic buffer allocations)
    lv_point_t arrow_pts[3];
    arrow_pts[0].x = cx;       arrow_pts[0].y = cy - 24; // Tip
    arrow_pts[1].x = cx - 6;   arrow_pts[1].y = cy - 10; // Left wing
    arrow_pts[2].x = cx + 6;   arrow_pts[2].y = cy - 10; // Right wing

    lv_draw_rect_dsc_t arrow_dsc;
    lv_draw_rect_dsc_init(&arrow_dsc);
    arrow_dsc.bg_color = lv_color_hex(0xE74C3C);
    arrow_dsc.bg_opa = LV_OPA_COVER;
    lv_draw_polygon(draw_ctx, &arrow_dsc, arrow_pts, 3);
}

void DialplateView::BtnCont_Create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_HOR_RES, 40);
    lv_obj_align_to(cont, ui.bottomInfo.cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    /*lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_place(
        cont,
        LV_FLEX_PLACE_SPACE_AROUND,
        LV_FLEX_PLACE_CENTER,
        LV_FLEX_PLACE_CENTER
    );*/

    ui.btnCont.cont = cont;

    ui.btnCont.btnMap = Btn_Create(cont, ResourcePool::GetImage("locate"), -80);
    ui.btnCont.btnRec = Btn_Create(cont, ResourcePool::GetImage("start"), 0);
    ui.btnCont.btnMenu = Btn_Create(cont, ResourcePool::GetImage("menu"), 80);
}

lv_obj_t* DialplateView::Btn_Create(lv_obj_t* par, const void* img_src, lv_coord_t x_ofs)
{
    lv_obj_t* obj = lv_obj_create(par);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 40, 31);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_align(obj, LV_ALIGN_CENTER, x_ofs, 0);
    lv_obj_set_style_bg_img_src(obj, img_src, 0);

    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_width(obj, 45, LV_STATE_PRESSED);
    lv_obj_set_style_height(obj, 25, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xbbbbbb), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff931e), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(obj, 9, 0);

    static lv_style_transition_dsc_t tran;
    static const lv_style_prop_t prop[] = { LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_PROP_INV};
    lv_style_transition_dsc_init(
        &tran,
        prop,
        lv_anim_path_ease_out,
        200,
        0,
        nullptr
    );
    lv_obj_set_style_transition(obj, &tran, LV_STATE_PRESSED);
    lv_obj_set_style_transition(obj, &tran, LV_STATE_FOCUSED);

    lv_obj_update_layout(obj);

    return obj;
}

void DialplateView::Brightness_Create(lv_obj_t* par)
{
    lv_obj_t* cont = lv_obj_create(par);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 120, 50);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_70, 0);
    lv_obj_set_style_radius(cont, 10, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cont);
    ui.brightness.cont = cont;

    lv_obj_t* label = lv_label_create(cont);
    lv_obj_set_style_text_font(label, ResourcePool::GetFont("bahnschrift_17"), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, "");
    lv_obj_center(label);
    ui.brightness.labelValue = label;
}

void DialplateView::ShowBrightnessOverlay(bool show)
{
    if (show)
    {
        lv_obj_move_foreground(ui.brightness.cont);
        lv_obj_clear_flag(ui.brightness.cont, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ui.brightness.cont, LV_OBJ_FLAG_HIDDEN);
    }
}

void DialplateView::SetBrightnessValue(int32_t value)
{
    lv_label_set_text_fmt(ui.brightness.labelValue, "%d%%", (int)(value * 100 / 1000));
}

void DialplateView::AppearAnimStart(bool reverse)
{
    lv_anim_timeline_set_reverse(ui.anim_timeline, reverse);
    lv_anim_timeline_start(ui.anim_timeline);
}
