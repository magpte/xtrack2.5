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
#ifndef __HAL_CONFIG_H
#define __HAL_CONFIG_H

/*=========================
   Hardware Configuration
 *=========================*/

/* Sensors */
#define CONFIG_SENSOR_ENABLE        1

#if CONFIG_SENSOR_ENABLE
#  define CONFIG_SENSOR_IMU_ENABLE  1
#  define CONFIG_SENSOR_MAG_ENABLE  0
#endif

#define NULL_PIN                    PD0

/* Screen */
#define CONFIG_SCREEN_CS_PIN        PB0
#define CONFIG_SCREEN_DC_PIN        PA4
#define CONFIG_SCREEN_RST_PIN       PA6
#define CONFIG_SCREEN_SCK_PIN       PA5
#define CONFIG_SCREEN_MOSI_PIN      PA7
#define CONFIG_SCREEN_BLK_PIN       PB1  // TIM3
#define CONFIG_SCREEN_SPI           SPI

#define CONFIG_SCREEN_HOR_RES       240
#define CONFIG_SCREEN_VER_RES       320

/* Battery */
#define CONFIG_BAT_DET_PIN          PA1
#define CONFIG_BAT_CHG_DET_PIN      PA11

/* Buzzer */
#define CONFIG_BUZZ_PIN             PA0  // TIM2

/* GPS */
#define CONFIG_GPS_SERIAL           Serial2
#define CONFIG_GPS_USE_TRANSPARENT  1
#define CONFIG_GPS_BUF_OVERLOAD_CHK 0

// 把 GPS 模块吐出来的原始 NMEA 语句实时写到 SD 卡 /nmea.log。
// 用于在不接 USB 转串口工具的情况下抓取 NMEA 数据（比如确认星座配置）。
// 确认完之后建议改回 0 关闭，避免长期运行时持续写卡。
#define CONFIG_GPS_NMEA_LOG_ENABLE  0

// 实验性：尝试用 PCAS04 的 Mode=7 打开更多星座（官方 AT300 手册只文档化
// 了 1=GPS、2=BDS、3=GPS+BDS 三个值，7 不在文档范围内，是否有效、加的
// 是哪个星座、甚至会不会被模块直接忽略都不确定）。
// AT6558R 是 ROM 版，不支持 $PCAS00 保存配置，所以这条指令必须每次开机
//都重新发送，不能只发一次——因此实现为开机时在 GPS_Init() 里发送。
// 验证方法：改完烧录，配合 CONFIG_GPS_NMEA_LOG_ENABLE 抓一份新的
// /nmea.log，看 GSA 语句里的系统 ID 有没有变化。
// 如果发现没有效果或者定位变得不稳定，把这个改回 0 就能退回出厂默认的
// GPS+BDS（mode=3 效果），不需要发任何指令。
#define CONFIG_GPS_TRY_MODE7_ENABLE  1
#define CONFIG_GPS_TX_PIN           PA3
#define CONFIG_GPS_RX_PIN           PA2

/* IMU */
#define CONFIG_IMU_INT1_PIN         PB10
#define CONFIG_IMU_INT2_PIN         PB11

/* I2C */
#define CONFIG_MCU_SDA_PIN          PB7
#define CONFIG_MCU_SDL_PIN          PB6

/* Encoder */
#define CONFIG_ENCODER_B_PIN        PB5
#define CONFIG_ENCODER_A_PIN        PB4
#define CONFIG_ENCODER_PUSH_PIN     PB3

/* Power */
#define CONFIG_POWER_EN_PIN         PA12
#define CONFIG_POWER_WAIT_TIME      500
#define CONFIG_POWER_SHUTDOWM_DELAY 2000
#define CONFIG_POWER_BATT_CHG_DET_PULLUP    true

/* Debug USART */
#define CONFIG_DEBUG_SERIAL         Serial
#define CONFIG_DEBUG_RX_PIN         PA10
#define CONFIG_DEBUG_TX_PIN         PA9

/* SD CARD */
#define CONFIG_SD_SPI               SPI_2
#define CONFIG_SD_CD_PIN            PA8
#define CONFIG_SD_MOSI_PIN          PB15
#define CONFIG_SD_MISO_PIN          PB14
#define CONFIG_SD_SCK_PIN           PB13
#define CONFIG_SD_CS_PIN            PB12

/* HAL Interrupt Update Timer */
#define CONFIG_HAL_UPDATE_TIM       TIM4

/* Show Stack & Heap Info */
#define CONFIG_SHOW_STACK_INFO      0
#define CONFIG_SHOW_HEAP_INFO       0

/* Backlight Config */
#define CONFIG_BACKLIGHT_MIN        200  // Range [0, 1000]
#define CONFIG_BACKLIGHT_MAX        1000 // Range [0, 1000]
#define CONFIG_BACKLIGHT_CTRL_RANGE 60   // minute Range[1, 120]

/* Use Watch Dog */
#define CONFIG_WATCH_DOG_ENABLE     1
#if CONFIG_WATCH_DOG_ENABLE
#  define CONFIG_WATCH_DOG_TIMEOUT (10 * 1000) // [ms]
#endif

#endif
