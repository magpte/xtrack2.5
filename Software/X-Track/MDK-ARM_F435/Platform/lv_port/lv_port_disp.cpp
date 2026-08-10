#include "lv_port/lv_port.h"
#include "lvgl/lvgl.h"
#include "HAL/HAL.h"

/* 双缓冲，每块 200 行（不是整屏 320 行）。整屏两块要占 300KB，384KB
 * SRAM 里只剩 44KB 给 LVGL 堆和其他所有东西，SystemInfos 这种页面很容易
 * 把 LVGL 堆用满。刷新是按脏区域走的（disp_flush_cb 每次都会重设窗口），
 * 缓冲区小于整屏只是让一次大范围重绘分成两批发送，总像素量不变。 */
#define SCREEN_BUFFER_LINES 200
#define SCREEN_BUFFER_SIZE (CONFIG_SCREEN_HOR_RES * SCREEN_BUFFER_LINES)

static lv_disp_drv_t* disp_drv_p = NULL;

static void disp_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p)
{
    disp_drv_p = disp;

    const lv_coord_t w = (area->x2 - area->x1 + 1);
    const lv_coord_t h = (area->y2 - area->y1 + 1);
    const uint32_t len = w * h;

    uint32_t* p32 = (uint32_t*)color_p;
    uint32_t count32 = len / 2;
    for (uint32_t i = 0; i < count32; i++) {
        p32[i] = __REV16(p32[i]);
    }
    if (len & 1) {
        uint16_t* p16 = (uint16_t*)color_p;
        p16[len - 1] = (p16[len - 1] >> 8) | (p16[len - 1] << 8);
    }

    HAL::Display_SetAddrWindow(area->x1, area->y1, area->x2, area->y2);

    HAL::Display_SendPixels((uint16_t*)color_p, len);
}

static void disp_send_finish_callback()
{
    lv_disp_flush_ready(disp_drv_p);
}

void lv_port_disp_init()
{
    HAL::Display_SetSendFinishCallback(disp_send_finish_callback);

    static lv_color_t lv_disp_buf1[SCREEN_BUFFER_SIZE];
    static lv_color_t lv_disp_buf2[SCREEN_BUFFER_SIZE];

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, lv_disp_buf1, lv_disp_buf2, SCREEN_BUFFER_SIZE);

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = CONFIG_SCREEN_HOR_RES;
    disp_drv.ver_res = CONFIG_SCREEN_VER_RES;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.wait_cb = NULL;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);
}
