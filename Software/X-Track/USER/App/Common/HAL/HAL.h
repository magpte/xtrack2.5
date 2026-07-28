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
#ifndef __HAL_H
#define __HAL_H

#include <stdint.h>
#include "HAL_Def.h"

namespace HAL {
    
typedef bool (*CommitFunc_t)(void* info, void* userData);
    
void HAL_Init();
void HAL_Update();

/* Backlight */
void Backlight_Init();
int32_t Backlight_GetValue();
void Backlight_SetValue(int32_t val);
void Backlight_SetGradual(int32_t target, uint16_t time = 500);
void Backlight_ForceLit(bool en);

/* Display */
void Display_Init();
void Display_DumpCrashInfo(const char* info);
void Display_SetAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void Display_SendPixels(const uint16_t* pixels, uint32_t len);
    
typedef void(*Display_CallbackFunc_t)(void);
void Display_SetSendFinishCallback(Display_CallbackFunc_t func);
    
/* FaultHandle */
void FaultHandle_Init();
bool SD_WriteCrashLog(const char* data); 

// 主循环"心跳"——见 main.cpp 的 loop()，每跑完一圈（HAL_Update() +
// lv_task_handler() 都执行完）就调用一次。HAL.cpp 里喂狗的定时器中断
// 会检查这个心跳最近有没有更新，只有心跳没有停摆太久才真正喂狗——
// 这样主循环真死循环/死锁的时候，看门狗还是能抓到并硬复位，不会被
// 定时器中断无条件喂饱。详细原因见 HAL.cpp 里的注释。
void WatchDog_Feed();

/* I2C */
int I2C_Scan();

/* IMU */
bool IMU_Init();
void IMU_SetCommitCallback(CommitFunc_t func, void* userData);
void IMU_Update();
// 暂停/恢复 IMU 轮询任务（不是断电关闭传感器，只是不再调 IMU_Update）。
// 用于锁屏轨迹记录模式下省电，见 Dialplate.cpp 的 LockMode_Enter/Exit。
void IMU_SetEnable(bool enable);

/* SD */
bool SD_Init();
void SD_Update();
bool SD_GetReady();
float SD_GetCardSizeMB();
const char* SD_GetTypeName();
typedef void(*SD_CallbackFunction_t)(bool insert);
void SD_SetEventCallback(SD_CallbackFunction_t callback);

// 原始 NMEA 语句落盘（供 u-center 回放用），实现见 HAL_SD_CARD.cpp。
// line 不需要以 '\0' 结尾，按 len 写入即可（调用方传的是 GPS_Update()
// 里按 '\n' 分好行的原始字节，末尾自带 \r\n）。文件懒加载：第一次调用
// 且 SD 卡就绪时才会真正建目录、开文件；不会阻塞在 GPS_Init() 阶段。
void NMEA_Log_Write(const char* line, uint32_t len);

// 把当前还没落盘的缓冲区内容写文件并 sync()，然后关闭文件——SD 卡被
// 拔出、或者设备准备关机（见 HAL_Power.cpp 的 Power_EventMonitor()）时
// 调用，避免最后一小段数据留在内存缓冲区里没写进去。SD 卡没打开日志
// 文件时调用这个函数是安全的空操作。
void NMEA_Log_Close();

/* Power */
void Power_Init();
void Power_HandleTimeUpdate();
void Power_SetAutoLowPowerTimeout(uint16_t sec);
uint16_t Power_GetAutoLowPowerTimeout();
void Power_SetAutoLowPowerEnable(bool en);
void Power_Shutdown();
void Power_Update();
void Power_RevertCapacity(uint16_t designCapacity, uint16_t fullChargeCapacity);
void Power_EventMonitor();
void Power_GetInfo(Power_Info_t* info);
typedef void(*Power_CallbackFunction_t)(void);
void Power_SetEventCallback(Power_CallbackFunction_t callback);

/* Clock */
void Clock_Init();
void Clock_GetInfo(Clock_Info_t* info);
void Clock_SetInfo(const Clock_Info_t* info);
const char* Clock_GetWeekString(uint8_t week);

/* GPS */
void GPS_Init();
void GPS_Update();
bool GPS_GetInfo(GPS_Info_t* info);
bool GPS_LocationIsValid();
double GPS_GetDistanceOffset(GPS_Info_t* info, double preLong, double preLat);

// 拿最近一次解析完整的 GSV 天球数据（方位角/仰角/信噪比/星座）。
// 独立于 GPS_Info_t 之外，不随 2Hz 定位一起刷新——GSV 本身被节流成
// 5 秒一次（见 HAL_GPS.cpp 里的 PCAS03 配置），跟星星在天上移动的
// 速度比起来完全够用，没必要跟着定位频率走。
void GPS_GetSkyInfo(Sky_Info_t* info);

/* Buzzer */
void Buzz_init();
void Buzz_SetEnable(bool en);
void Buzz_Tone(uint32_t freq, int32_t duration = -1);

/* Encoder */
void Encoder_Init();
void Encoder_Update();
int32_t Encoder_GetDiff();
bool Encoder_GetIsPush();
void Encoder_SetEnable(bool en);

/* Audio */
void Audio_Init();
void Audio_Update();
bool Audio_PlayMusic(const char* name);

/* Memory */
void Memory_DumpInfo();

}

#endif
