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
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
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
   UI Font & Character Set Configuration & Guidelines
 *=========================
 * 固件内置的 Bahnschrift 字体系列 (13px, 17px, 32px, 65px)
 * 为节省 Flash 空间，仅收录了标准 ASCII 字符集 (Unicode 0x0020 ~ 0x007E，共 95
 个字符)。
 *
 * 【系统支持且可正常显示的特殊符号/标点 (标准 ASCII 32~126)】
 *   !  "  #  $  %  &  '  (  )  *  +  ,  -  .  /
 *   :  ;  <  =  >  ?  @
 *   [  \  ]  ^  _  `
 *   {  |  }  ~
 *
 * 【系统不支持的非 ASCII 特殊字符（严禁在 UI Label 中直接使用）】
 *   - 度数符号 '°' (U+00B0)  --> 无法识别，请直接显示数字（如 "356" 或 "0"）
 *   - 摄氏度 '℃' (U+2103)   --> 无法识别，请使用 "C" 或 "degC"
 *   - 箭头符号 '↑' '↓' '←' '→' --> 无法识别，请使用 LVGL 图像或 Polygon
 矢量绘制
 *   - 中文字符 / 日韩字符 / 扩展拉丁字符 --> 无法识别
 *=========================*/

/*=========================
   Application configuration
 *=========================*/

#define CONFIG_SYSTEM_SAVE_FILE_PATH "/SystemSave.json"
#define CONFIG_SYSTEM_SAVE_FILE_BACKUP_PATH "/.SystemSaveBackup.json"
#define CONFIG_SYSTEM_LANGUAGE_DEFAULT "en-GB"
#define CONFIG_SYSTEM_TIME_ZONE_DEFAULT 8 // GMT+ 8
#define CONFIG_SYSTEM_SOUND_ENABLE_DEFAULT true
#define CONFIG_SCREEN_BRIGHTNESS_DEFAULT 800 // Range [0, 1000]

// 自动降光配置：当设定亮度大于该阈值且速度大于速度阈值时，自动降光至目标亮度
#define CONFIG_AUTO_DIM_BRIGHT_THRESH_DEFAULT                                  \
  700 // 高亮度触发阈值，70% (0..1000)
#define CONFIG_AUTO_DIM_SPEED_THRESH_DEFAULT 5.0f // 速度触发阈值，5 km/h
#define CONFIG_AUTO_DIM_TARGET_BRIGHT_DEFAULT 200 // 目标降低亮度，20% (0..1000)

// 静止无操作节能配置 (ms)：
// 当处于静止状态（速度 < 1.0 km/h 或 GPS 无效）且无编码器操作时：
// 1. 超过 IDLE_TIMEOUT 自动将背光降至目标低亮度 (20%)；
// 2. 超过 SCREEN_OFF_TIMEOUT 自动进入息屏模式 (背光关闭 + ST7789 深度睡眠 SLPIN)；
// 3. 一旦检测到编码器操作或起步速度 > 1.5 km/h，立刻瞬时唤醒并恢复原设定亮度。
#define CONFIG_AUTO_DIM_IDLE_TIMEOUT_DEFAULT        30000  // 30s 静止无操作自动降光至 20%
#define CONFIG_AUTO_SCREEN_OFF_TIMEOUT_DEFAULT     120000  // 120s 静止无操作自动息屏 (0: 关闭自动息屏)

#define CONFIG_WEIGHT_DEFAULT 70 // kg

#ifdef ARDUINO
#define CONFIG_GPS_REFR_PERIOD 500 // ms -- 配合模块 PCAS02 提到的 2Hz 定位频率
#else
#define CONFIG_GPS_REFR_PERIOD                                                 \
  500 // ms -- 对齐 2Hz 定位刷新，消除 10ms (100Hz) 高频空转
#endif

// 静止时把 LiveMap 的 GPS/瓦片检查间隔从 CONFIG_GPS_REFR_PERIOD 拉长到这个值，
// 减少 CheckPosition() 的调用频率（瓦片坐标换算、SportInfo 文本格式化、
// 以及最容易被 GPS 噪声在瓦片边界附近来回触发的 MapTileContReload()）。
// 一旦速度重新超过 EXIT 阈值，下一次检查就会立刻恢复到正常刷新间隔。
#define CONFIG_GPS_REFR_PERIOD_STATIONARY 3000 // ms

// 双阈值迟滞：进入"静止"用低阈值，退出用高阈值，避免速度刚好卡在
// 临界值附近时，每次判断都在两种刷新间隔之间来回抖动。
#define CONFIG_LIVE_MAP_STATIONARY_ENTER_KPH 1.0f
#define CONFIG_LIVE_MAP_STATIONARY_EXIT_KPH 2.5f

// 像素死区过滤阈值（单位：像素）。设置为 1 保持地图背景与箭头图标无迟滞同步
#define CONFIG_LIVE_MAP_DEADBAND_THRESHOLD 1

// 全局统一最高精度轨迹存储基准层级（Level 18，地面分辨率 0.52m/px）
#define CONFIG_TRACK_BASE_LEVEL 18

// 轨迹特征拐点提取阈值（Level 18 坐标系下，1 像素 ≈ 0.52 米）
#define CONFIG_TRACK_FILTER_OFFSET_THRESHOLD 1

// 远景/大范围视图（Level 1~16）屏幕像素空间自适应抽稀间距平方阈值（2px ^ 2 =
// 4）
#define CONFIG_TRACK_SIMPLIFY_MIN_DIST_SQ 4

#define CONFIG_GPS_LONGITUDE_DEFAULT 113.055735
#define CONFIG_GPS_LATITUDE_DEFAULT 23.011105

#define CONFIG_TRACK_RECORD_FILE_DIR_NAME "Track"

// 原始 NMEA 语句日志目录（采用短目录 "N"，配合 8.3 SFN 格式极速读写）。
#define CONFIG_NMEA_LOG_FILE_DIR_NAME "N"

#define CONFIG_MAP_USE_WGS84_DEFAULT true
#define CONFIG_MAP_DIR_PATH_DEFAULT "/MAPRB"

#ifndef CONFIG_MAP_EXT_NAME_DEFAULT
#define CONFIG_MAP_EXT_NAME_DEFAULT "rle"
#endif

#define CONFIG_MAP_IMG_PNG_ENABLE 0

#define CONFIG_MAP_IMG_RLE_ENABLE 1

#ifndef CONFIG_MAP_CACHE_SIZE_KB
#define CONFIG_MAP_CACHE_SIZE_KB 64
#endif

#define CONFIG_ARROW_THEME_DEFAULT "default"

#define CONFIG_LIVE_MAP_LEVEL_DEFAULT 16

#define CONFIG_LIVE_MAP_DEBUG_ENABLE 0
#if CONFIG_LIVE_MAP_DEBUG_ENABLE
#define CONFIG_LIVE_MAP_VIEW_WIDTH 240
#define CONFIG_LIVE_MAP_VIEW_HEIGHT 240
#else
#define CONFIG_LIVE_MAP_VIEW_WIDTH LV_HOR_RES
#define CONFIG_LIVE_MAP_VIEW_HEIGHT LV_VER_RES
#endif

#define CONFIG_MONKEY_TEST_ENABLE 0
#if CONFIG_MONKEY_TEST_ENABLE
#define CONFIG_MONKEY_INDEV_TYPE LV_INDEV_TYPE_ENCODER
#define CONFIG_MONKEY_PERIOD_MIN 10
#define CONFIG_MONKEY_PERIOD_MAX 100
#define CONFIG_MONKEY_INPUT_RANGE_MIN -5
#define CONFIG_MONKEY_INPUT_RANGE_MAX 5
#endif

#define CONFIG_PAGE_TEMPLATE_ENABLE 0

// ---------------------------------------------------------------------
// 系统全量日志落盘配置（/system.log）
// 开启后，系统所有的串口输出（Serial.print/printf、printf、LV_LOG_USER、地图加载日志等）
// 均会自动镜像缓存并持久化写入 SD 卡
// /system.log，方便拔卡直接在电脑上查看完整运行日志。
// ---------------------------------------------------------------------
#define CONFIG_SD_SYS_LOG_ENABLE 1

#endif
