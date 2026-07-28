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
    HAL::HAL_Update();
    lv_task_handler();

    // 主循环这一圈（HAL_Update() + lv_task_handler()）都跑完了，标记
    // 心跳——喂狗的定时器中断（HAL.cpp 的 HAL_TimerInterrputUpdate()）
    // 会检查这个心跳有没有在看门狗超时时间内更新过，主循环真死锁/死
    // 循环卡在这两个调用之一里出不来的话，心跳就会停摆，看门狗最终
    // 会把系统硬复位，而不是被定时器中断无条件喂饱。
    HAL::WatchDog_Feed();

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
