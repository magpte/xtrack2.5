/*
 * MIT License
 * Copyright (c) 2021 _VIFEXTech
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "Arduino.h"
#include "App/App.h"
#include "HAL/HAL.h"
#include "lvgl/lvgl.h"
#include "lv_port/lv_port.h"

#if defined(AT32F435xx)
#include "DebugLog.h"
#endif

#if LV_USE_DEMO_BENCHMARK

#include "benchmark.inc"

#else

static void setup()
{
    lv_init();
    lv_port_init();
    HAL::HAL_Init();

    App_Init();

    HAL::Power_SetEventCallback(App_Uninit);
    HAL::Memory_DumpInfo();
}

static void loop()
{
#if defined(AT32F435xx)
    // 主循环打点：卡死的时候看 g_DebugLog 里最后一条是 'Lhal'（说明
    // 卡在 HAL::HAL_Update() 里，SD/GPS 相关的读写大多在这一步）还是
    // 'Llvg'（卡在 LVGL 渲染里）——这一条比 SD 卡自己的日志更先看，
    // 能先把"卡在哪个大模块"缩小范围，再去看该模块自己打的详细日志。
    DEBUG_LOG(DEBUG_TAG('L','h','a','l'), 0, 0);
#endif
    HAL::HAL_Update();

#if defined(AT32F435xx)
    DEBUG_LOG(DEBUG_TAG('L','l','v','g'), 0, 0);
#endif
    lv_task_handler();

    __wfi();
}

#endif

/**
  * @brief  Main Function
  * @param  None
  * @retval None
  */
int main(void)
{
    Core_Init();
    setup();
    for(;;)loop();
}
