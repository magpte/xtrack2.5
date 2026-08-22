#include "lv_port/lv_port.h"
#include "lvgl/lvgl.h"
#include "HAL/HAL.h"

/* 全屏单缓冲：320 行（240x320）占 150KB RAM。
 * 相比原 160 行双缓冲（2 x 75KB = 150KB），RAM 总占用完全一致（150KB），
 * 但彻底消除了全屏重绘/地图移动及载入时 LVGL 拆分为上下半屏两次刷新的割裂感与画面撕裂现象。
 */
#define SCREEN_BUFFER_LINES CONFIG_SCREEN_VER_RES
#define SCREEN_BUFFER_SIZE (CONFIG_SCREEN_HOR_RES * SCREEN_BUFFER_LINES)

static lv_disp_drv_t* disp_drv_p = NULL;

#ifndef __REV16
#  define __REV16(x) (((x) & 0x00FF00FF) << 8 | ((x) & 0xFF00FF00) >> 8)
#endif

/* 硬件/驱动层字节序翻转配置开关：
 * 0: 使用 CPU Cortex-M4 __REV16 汇编指令进行单周期双像素端序转换
 * 1: 开启 LCD IC (如 ST7789 RAM Control 0xB0) 或 SPI DMA 硬件 Byte-Swap 模式，
 *    彻底绕过 CPU __REV16 循环，将送显 CPU 消耗降低至 0。
 */
#ifndef DISP_HW_BYTE_SWAP_ENABLE
#  define DISP_HW_BYTE_SWAP_ENABLE  1
#endif

static void disp_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p)
{
    disp_drv_p = disp;

    const lv_coord_t w = (area->x2 - area->x1 + 1);
    const lv_coord_t h = (area->y2 - area->y1 + 1);
    const uint32_t len = w * h;

#if (DISP_HW_BYTE_SWAP_ENABLE == 0)
    uint32_t* p32 = (uint32_t*)color_p;
    uint32_t count32 = len / 2;
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
    if (len & 1) {
        uint16_t* p16 = (uint16_t*)color_p;
        uint32_t val = p16[len - 1];
        p16[len - 1] = (uint16_t)((val >> 8) | (val << 8));
    }
#endif

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

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, lv_disp_buf1, NULL, SCREEN_BUFFER_SIZE);

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = CONFIG_SCREEN_HOR_RES;
    disp_drv.ver_res = CONFIG_SCREEN_VER_RES;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.wait_cb = NULL;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);
}
