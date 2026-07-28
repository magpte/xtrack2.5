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
#ifndef __CONFIG_H
#define __CONFIG_H

/*=========================
   Application configuration
 *=========================*/

#define CONFIG_SYSTEM_SAVE_FILE_PATH          "/SystemSave.json"
#define CONFIG_SYSTEM_SAVE_FILE_BACKUP_PATH   "/.SystemSaveBackup.json"
#define CONFIG_SYSTEM_LANGUAGE_DEFAULT        "en-GB"
#define CONFIG_SYSTEM_TIME_ZONE_DEFAULT       8    // GMT+ 8
#define CONFIG_SYSTEM_SOUND_ENABLE_DEFAULT    true
#define CONFIG_SCREEN_BRIGHTNESS_DEFAULT       800  // Range [0, 1000]

#define CONFIG_WEIGHT_DEFAULT                 70   // kg

#ifdef ARDUINO
#  define CONFIG_GPS_REFR_PERIOD              500  // ms -- 配合模块 PCAS02 提到的 2Hz 定位频率
#else
#  define CONFIG_GPS_REFR_PERIOD              10 // ms
#endif

// 静止时把 LiveMap 的 GPS/瓦片检查间隔从 CONFIG_GPS_REFR_PERIOD 拉长到这个值，
// 减少 CheckPosition() 的调用频率（瓦片坐标换算、SportInfo 文本格式化、
// 以及最容易被 GPS 噪声在瓦片边界附近来回触发的 MapTileContReload()）。
// 一旦速度重新超过 EXIT 阈值，下一次检查就会立刻恢复到正常刷新间隔。
#define CONFIG_GPS_REFR_PERIOD_STATIONARY     3000 // ms

// 双阈值迟滞：进入"静止"用低阈值，退出用高阈值，避免速度刚好卡在
// 临界值附近时，每次判断都在两种刷新间隔之间来回抖动。
#define CONFIG_LIVE_MAP_STATIONARY_ENTER_KPH  1.0f
#define CONFIG_LIVE_MAP_STATIONARY_EXIT_KPH   2.5f

#define CONFIG_GPS_LONGITUDE_DEFAULT          113.055735f
#define CONFIG_GPS_LATITUDE_DEFAULT           23.011105f

#define CONFIG_TRACK_FILTER_OFFSET_THRESHOLD  2 // pixel
#define CONFIG_TRACK_RECORD_FILE_DIR_NAME     "Track"

// 原始 NMEA 语句日志目录，用来给 u-center 回放/分析用。文件名按开机
// 时刻命名（见 HAL_SD_CARD.cpp 的 NMEA_Log_Open()），跟 Track 目录下
// 按"开始录制轨迹"这个用户动作命名的 GPX 文件是两回事——NMEA 日志是
// 只要 SD 卡在、GPS 在跑就记录，不需要用户手动开始/停止。
#define CONFIG_NMEA_LOG_FILE_DIR_NAME         "NMEA"

#define CONFIG_MAP_USE_WGS84_DEFAULT          false
#define CONFIG_MAP_DIR_PATH_DEFAULT           "/MAPRB"

#ifndef CONFIG_MAP_EXT_NAME_DEFAULT
#define CONFIG_MAP_EXT_NAME_DEFAULT           "rle"
#endif

#define CONFIG_MAP_IMG_PNG_ENABLE             0

#define CONFIG_MAP_IMG_RLE_ENABLE             1

#define CONFIG_ARROW_THEME_DEFAULT            "default"

#define CONFIG_LIVE_MAP_LEVEL_DEFAULT         16

#define CONFIG_LIVE_MAP_DEBUG_ENABLE          0
#if CONFIG_LIVE_MAP_DEBUG_ENABLE
#  define CONFIG_LIVE_MAP_VIEW_WIDTH          240
#  define CONFIG_LIVE_MAP_VIEW_HEIGHT         240
#else
#  define CONFIG_LIVE_MAP_VIEW_WIDTH          LV_HOR_RES
#  define CONFIG_LIVE_MAP_VIEW_HEIGHT         LV_VER_RES
#endif

#define CONFIG_MONKEY_TEST_ENABLE             0
#if CONFIG_MONKEY_TEST_ENABLE
#  define CONFIG_MONKEY_INDEV_TYPE            LV_INDEV_TYPE_ENCODER
#  define CONFIG_MONKEY_PERIOD_MIN            10
#  define CONFIG_MONKEY_PERIOD_MAX            100
#  define CONFIG_MONKEY_INPUT_RANGE_MIN       -5
#  define CONFIG_MONKEY_INPUT_RANGE_MAX       5
#endif

#endif
