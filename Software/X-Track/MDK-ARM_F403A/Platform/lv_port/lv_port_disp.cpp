#include "lv_port/lv_port.h"
#include "lvgl/lvgl.h"
#include "HAL/HAL.h"

#define SCR_BUFF_SIZE (CONFIG_SCREEN_BUFFER_SIZE - CONFIG_SCREEN_HOR_RES * 29)

static lv_disp_drv_t* disp_drv_p;

#ifndef __REV16
#  define __REV16(x) (((x) & 0x00FF00FF) << 8 | ((x) & 0xFF00FF00) >> 8)
#endif

static void disp_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    disp_drv_p = disp;

    const lv_coord_t w = (area->x2 - area->x1 + 1);
    const lv_coord_t h = (area->y2 - area->y1 + 1);
    const uint32_t size = w * h;

    uint32_t* p32 = (uint32_t*)color_p;
    uint32_t count32 = size / 2;
    uint32_t i = 0;
    // 4x 循环展开，利用 Cortex-M4 __REV16 汇编指令单周期并行处理双像素字节序转换
    for (; i + 3 < count32; i += 4) {
        p32[i + 0] = __REV16(p32[i + 0]);
        p32[i + 1] = __REV16(p32[i + 1]);
        p32[i + 2] = __REV16(p32[i + 2]);
        p32[i + 3] = __REV16(p32[i + 3]);
    }
    for (; i < count32; i++) {
        p32[i] = __REV16(p32[i]);
    }
    if (size & 1) {
        uint16_t* p16 = (uint16_t*)color_p;
        uint32_t val = p16[size - 1];
        p16[size - 1] = (uint16_t)((val >> 8) | (val << 8));
    }

    HAL::Display_SetAddrWindow(area->x1, area->y1, area->x2, area->y2);

    HAL::Display_SendPixels((uint16_t*)color_p, size);
}

static void disp_send_finish_callback()
{
    lv_disp_flush_ready(disp_drv_p);
}

void lv_port_disp_init()
{
    HAL::Display_SetSendFinishCallback(disp_send_finish_callback);

    static lv_color_t lv_disp_buf[SCR_BUFF_SIZE];

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, lv_disp_buf, NULL, SCR_BUFF_SIZE);

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
