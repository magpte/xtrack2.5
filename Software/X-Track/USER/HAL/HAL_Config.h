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

// 实测已确认 PCAS04 Mode=7（GPS+BDS+GLONASS 三星座联合定位）有效——
// GSA 系统ID 里出现了 1/2/4，卫星总数从双星座 7 颗涨到三星座 12 颗。
// 这个开关同时控制两条指令（都在 GPS_Init() 里）：
//   1) $PCAS04,7*1E   打开三星座联合定位
//   2) $PCAS03,...    只保留 GGA+RMC，关掉 TinyGPS++ 根本不解析、纯粹
//      浪费带宽的 GSA/GSV/GLL/VTG/ZDA，把 9600 波特率下的带宽占用从
//      83% 压到 15%，见 HAL_GPS.cpp 里的详细说明。
// AT6558R 是 ROM 版，不支持 $PCAS00 保存配置，所以这两条指令必须每次
// 开机都重新发送，不能只发一次——已经实现为开机时在 GPS_Init() 里发送。
// 如果想退回出厂默认的 GPS+BDS 双星座、且不限制语句类型，把这个改回 0
// 即可，不需要发任何指令。
#define CONFIG_GPS_TRY_MODE7_ENABLE  1

// 解析 GSV 语句，给 SystemInfos 页面的天球图提供卫星方位角/仰角/信噪比
// 数据。见 HAL_GPS.cpp 里的 Sky_ParseLine()。关掉这个开关的话，
// HAL::GPS_GetSkyInfo() 会返回一个空的（count=0）结构体，天球图那块
// 区域会没有卫星点可画，但不会报错/崩溃。
#define CONFIG_GPS_SKY_ENABLE        1

// 把 GPS 模块吐出来的原始 NMEA 语句（未经 TinyGPS++ 解析、字节原样）
// 落盘到 SD 卡 CONFIG_NMEA_LOG_FILE_DIR_NAME 目录下，文件名按本次开机
// 时刻命名，一次开机一个文件，供之后拖进 u-center 回放/分析用。
// 具体收录哪些语句、按什么频率收录，见 HAL_GPS.cpp 里 NMEA_Log_Feed()
// 和 GPS_Init() 里 PCAS03 那条指令的注释。关掉这个开关的话完全不建
// 文件、不写 SD，跟这个功能之前不存在时行为一致。
#define CONFIG_GPS_NMEA_LOG_ENABLE   1

// 开机时用 RTC 时间 + 上次保存的定位点（DP_SysConfig.cpp 里的
// sysConfig.longitude/latitude）给 GPS 模块发一条 AID-INI 辅助定位
// 信息（CASIC 二进制协议 Class 0x0B, ID 0x01），帮它缩小搜星范围、
// 加快这次开机的首次定位。具体实现见 HAL_GPS.cpp 的
// HAL::GPS_SendAidingData()，触发点在 DP_SysConfig.cpp。这个开关只
// 控制"要不要发这条辅助信息"，跟 NMEA log/GSA/GSV 那些配置无关。
#define CONFIG_GPS_AID_ENABLE        1

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
