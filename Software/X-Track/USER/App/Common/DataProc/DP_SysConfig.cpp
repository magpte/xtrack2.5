#include "DataProc.h"
#include "../HAL/HAL.h"
#include "Config/Config.h"
#include "HAL/HAL_Config.h"
#include "Utils/Time/Time.h"
#include "lvgl/lvgl.h"

using namespace DataProc;

static SysConfig_Info_t sysConfig;

static float s_lastKnownSpeed = 0.0f;
static bool s_lastGpsValid = false;
static bool s_isAutoDimmed = false;
static bool s_isScreenLocked = false;
static int16_t s_lastAppliedBrightness = -1;
static uint32_t s_lastEncoderActivityTick = 0;

static int16_t SysConfig_NormalizeBrightness(int16_t val, int16_t defaultVal)
{
    if (val <= 0) return defaultVal;
    if (val <= 100) return val * 10;
    if (val > 1000) return 1000;
    return val;
}

static void SysConfig_UpdateBacklight(float currentSpeedKph, bool isGpsValid, uint16_t animTime)
{
    if (s_isScreenLocked)
    {
        if (s_lastAppliedBrightness != 0)
        {
            s_lastAppliedBrightness = 0;
            HAL::Backlight_SetGradual(0, animTime);
        }
        return;
    }

    int16_t highBrightThresh = SysConfig_NormalizeBrightness(sysConfig.autoDimBrightThresh, CONFIG_AUTO_DIM_BRIGHT_THRESH_DEFAULT);
    int16_t targetLowBright  = SysConfig_NormalizeBrightness(sysConfig.autoDimTargetBright, CONFIG_AUTO_DIM_TARGET_BRIGHT_DEFAULT);
    float speedThresh        = sysConfig.autoDimSpeedThresh;

    if (sysConfig.screenBrightness > highBrightThresh && speedThresh > 0.0f)
    {
        float exitSpeedThresh = (speedThresh >= 1.0f) ? (speedThresh - 0.5f) : (speedThresh * 0.8f);

        if (!s_isAutoDimmed)
        {
            if (isGpsValid && currentSpeedKph > speedThresh)
            {
                s_isAutoDimmed = true;
            }
        }
        else
        {
            if (!isGpsValid || currentSpeedKph < exitSpeedThresh)
            {
                s_isAutoDimmed = false;
            }
        }
    }
    else
    {
        s_isAutoDimmed = false;
    }

    int16_t targetHardwareBrightness = sysConfig.screenBrightness;

    if (s_isAutoDimmed)
    {
        // 自动变暗生效期间：若 5 秒内有编码器操作，临时恢复原本设定的亮度，满 5 秒后回到变暗状态
        if (s_lastEncoderActivityTick != 0 && DataProc::GetTickElaps(s_lastEncoderActivityTick) < 5000)
        {
            targetHardwareBrightness = sysConfig.screenBrightness;
        }
        else
        {
            targetHardwareBrightness = targetLowBright;
        }
    }

    if (targetHardwareBrightness < 0) targetHardwareBrightness = 0;
    else if (targetHardwareBrightness > 1000) targetHardwareBrightness = 1000;

    if (targetHardwareBrightness != s_lastAppliedBrightness)
    {
        s_lastAppliedBrightness = targetHardwareBrightness;
        HAL::Backlight_SetGradual(targetHardwareBrightness, animTime);
    }
}

static int onEvent(Account* account, Account::EventParam_t* param)
{
    if (param->event == Account::EVENT_TIMER)
    {
        SysConfig_UpdateBacklight(s_lastKnownSpeed, s_lastGpsValid, 500);
        return Account::RES_OK;
    }

    if (param->event == Account::EVENT_PUB_PUBLISH)
    {
        if (param->size == sizeof(HAL::GPS_Info_t))
        {
            HAL::GPS_Info_t* gpsInfo = (HAL::GPS_Info_t*)param->data_p;
            s_lastKnownSpeed = (float)gpsInfo->speed;
            s_lastGpsValid = gpsInfo->isVaild;
            SysConfig_UpdateBacklight(s_lastKnownSpeed, s_lastGpsValid, 500);

#if CONFIG_GPS_ALMANAC_AID_ENABLE
            // 连续稳定定位满 15 分钟（15 * 60 * 1000 ms）后，在后台触发一次全量历书轮询提取
            static uint32_t s_gpsFixStartTick = 0;
            static bool s_almanacHarvestTriggered = false;

            if (gpsInfo->isVaild)
            {
                if (s_gpsFixStartTick == 0)
                {
                    s_gpsFixStartTick = DataProc::GetTick();
                }
                else if (!s_almanacHarvestTriggered &&
                         DataProc::GetTickElaps(s_gpsFixStartTick) >= 15 * 60 * 1000)
                {
                    s_almanacHarvestTriggered = true;
                    HAL::GPS_PollAlmanac();
                }
            }
            else
            {
                s_gpsFixStartTick = 0;
            }
#endif
        }
        return Account::RES_OK;
    }

    if (param->size != sizeof(SysConfig_Info_t))
    {
        return Account::RES_SIZE_MISMATCH;
    }

    SysConfig_Info_t* info = (SysConfig_Info_t*)param->data_p;

    switch (param->event)
    {
    case Account::EVENT_NOTIFY:
    {
        if (info->cmd == SYSCONFIG_CMD_LOAD)
        {
            HAL::Buzz_SetEnable(sysConfig.soundEnable);
            s_lastAppliedBrightness = -1;
            SysConfig_UpdateBacklight(s_lastKnownSpeed, s_lastGpsValid, 1000);
#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
            HAL::Power_Info_t powerInfo;
            HAL::Power_GetInfo(&powerInfo);
            if (powerInfo.design_capacity != sysConfig.designCap || powerInfo.fullcharge_capacity != sysConfig.fullChgCap){
                HAL::Audio_PlayMusic("GaugeSetting"); // 在设置Gauge时，播放声音，以提醒用户设备已开机
                HAL::Power_RevertCapacity(sysConfig.designCap, sysConfig.fullChgCap);
            }
#endif
            // 开机时把 RTC 时间 + 上次保存的定位点告诉 GPS 模块（AID-INI
            // 辅助定位），帮它缩小搜星范围、加快这次开机的首次定位
            // （TTFF）。放在这里（sysConfig 刚加载完这一刻）触发，是因为
            // 这是 App 层第一次能同时拿到"上次已知位置"（sysConfig.
            // longitude/latitude，刚从 SD 卡加载出来）和"当前 RTC 时间"
            // 的时机，而且这时模块大概率还没完成冷启动搜星（冷启动 TTFF
            // 规格 ≤32 秒，这里的启动流程远用不到那么久），赶得上帮上忙。
            //
            // 注意："Clock" 账户 Pull 出来的是本地时间（DP_TzConv.cpp
            // 里用 GPS UTC 时间 + sysConfig.timeZone 小时换算存进 RTC
            // 的），不是 UTC，而 AID-INI 需要的是真 UTC（GPS 周/周内秒
            // 是相对 UTC 算的）。这里先用 setTime/adjustTime 把时区
            // 偏移减回去，还原成 UTC，再传给 GPS_SendAidingData()——
            // 早前有一版忘了做这一步，直接把本地时间当 UTC 发给模块，
            // 靠实测 NMEA log 里模块开机时回显的时间对不上真实 UTC（早了
            // 整整一个时区）才发现，那次实测下来 TTFF 反而比不加辅助
            // 定位还慢，应该就是喂了个偏差 8 小时的时间帮了倒忙。
            HAL::Clock_Info_t clock;
            if (account->Pull("Clock", &clock, sizeof(clock)) == Account::RES_OK)
            {
                setTime(
                    clock.hour,
                    clock.minute,
                    clock.second,
                    clock.day,
                    clock.month,
                    clock.year
                );
                adjustTime(-(int32_t)sysConfig.timeZone * SECS_PER_HOUR);

                HAL::Clock_Info_t utcClock;
                utcClock.year   = year();
                utcClock.month  = month();
                utcClock.day    = day();
                utcClock.hour   = hour();
                utcClock.minute = minute();
                utcClock.second = second();

                // 当前 UTC unix 时间戳，与 lastFixUnix 的差值供
                // GPS_SendAidingData() 动态计算 posAcc/timeAcc。
                uint32_t nowUnix = (uint32_t)now();
                HAL::GPS_SendAidingData(
                    sysConfig.latitude,
                    sysConfig.longitude,
                    utcClock,
                    sysConfig.lastFixUnix,
                    nowUnix
                );

#if CONFIG_GPS_ALMANAC_AID_ENABLE
                // 尝试从 SD 卡根目录加载历书缓存文件 (/gpsalm.bin)
                // 若文件存在且通过校验/未过期，流式下发给 GPS 模块进一步加速搜星
                lv_fs_file_t almFile;
                if (lv_fs_open(&almFile, CONFIG_GPS_ALMANAC_FILE_PATH, LV_FS_MODE_RD) == LV_FS_RES_OK)
                {
                    uint32_t bufSize = 4096 + sizeof(HAL::GPS_Almanac_Header_t);
                    uint8_t* almBuffer = (uint8_t*)lv_mem_alloc(bufSize);
                    if (almBuffer != NULL)
                    {
                        uint32_t bytesRead = 0;
                        lv_fs_read(&almFile, almBuffer, bufSize, &bytesRead);

                        if (bytesRead >= sizeof(HAL::GPS_Almanac_Header_t))
                        {
                            HAL::GPS_SendAlmanacData(almBuffer, bytesRead, nowUnix);
                        }
                        else
                        {
                            LV_LOG_WARN("GPS: /gpsalm.bin corrupted or incomplete, skipping.");
                        }
                        lv_mem_free(almBuffer);
                    }
                    lv_fs_close(&almFile);
                }
                else
                {
                    LV_LOG_USER("GPS: /gpsalm.bin not found, skipping almanac aiding.");
                }
#endif
            }
        }
        else if (info->cmd == SYSCONFIG_CMD_SET_BRIGHTNESS)
        {
            // 由旋转编码器等实时调节场景调用：只更新内存中的 sysConfig，
            // 不在这里写 SD 卡；真正落盘发生在下一次 SYSCONFIG_CMD_SAVE
            // （见 App.cpp，通常是关机/断电时触发）。
            // App 层不引用 HAL/CommonMacro.h，这里手写钳位，
            // 避免跨层依赖（CM_VALUE_LIMIT 定义在 USER/HAL/CommonMacro.h，
            // 而这里 include 的是 App/Common/HAL/HAL.h，两者不是同一个文件）。
            if (info->screenBrightness < 0)
            {
                info->screenBrightness = 0;
            }
            else if (info->screenBrightness > 1000)
            {
                info->screenBrightness = 1000;
            }
            sysConfig.screenBrightness = info->screenBrightness;
            SysConfig_UpdateBacklight(s_lastKnownSpeed, s_lastGpsValid, 100);
        }
        else if (info->cmd == SYSCONFIG_CMD_ENCODER_ACTIVITY)
        {
            s_lastEncoderActivityTick = DataProc::GetTick();
            SysConfig_UpdateBacklight(s_lastKnownSpeed, s_lastGpsValid, 300);
        }
        else if (info->cmd == SYSCONFIG_CMD_SET_LOCK_STATE)
        {
            s_isScreenLocked = info->isLocked;
            if (s_isScreenLocked)
            {
                s_lastAppliedBrightness = 0;
                HAL::Backlight_SetGradual(0, 500);
            }
            else
            {
                s_lastAppliedBrightness = -1; // 强制重新计算并平滑恢复原本背光
                SysConfig_UpdateBacklight(s_lastKnownSpeed, s_lastGpsValid, 500);
            }
        }
        else if (info->cmd == SYSCONFIG_CMD_SAVE)
        {
            HAL::GPS_Info_t gpsInfo;
            if(account->Pull("GPS", &gpsInfo, sizeof(gpsInfo)) != Account::RES_OK)
            {
                return Account::RES_UNKNOW;
            }

            if(gpsInfo.isVaild)
            {
                sysConfig.longitude = gpsInfo.longitude;
                sysConfig.latitude  = gpsInfo.latitude;

                // GPS 有效时同时记录当前 UTC unix 时间戳，供下次开机时
                // 动态计算 posAcc/timeAcc（距上次定位越近，精度估计越紧，
                // 模块搜星范围越小，TTFF 越短）。
                // 这里用 RTC 转回 UTC：Clock 账户存的是本地时间，先
                // 减时区偏移再算 unix 时间戳，和 LOAD 分支里做法一致。
                HAL::Clock_Info_t clock;
                if (account->Pull("Clock", &clock, sizeof(clock)) == Account::RES_OK)
                {
                    setTime(
                        clock.hour, clock.minute, clock.second,
                        clock.day,  clock.month,  clock.year
                    );
                    adjustTime(-(int32_t)sysConfig.timeZone * SECS_PER_HOUR);
                    sysConfig.lastFixUnix = (uint32_t)now();
                }
            }
#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
            HAL::Power_Info_t powerInfo;
            account->Pull("Power", &powerInfo, sizeof(powerInfo));
            // 存储电量计缓存数据，掉电或电量计POR可以恢复缓存
            if (sysConfig.fullChgCap > powerInfo.fullcharge_capacity){
                sysConfig.fullChgCap = powerInfo.fullcharge_capacity;
            }
#endif

#if CONFIG_GPS_ALMANAC_AID_ENABLE
            // 关机落盘：若运行期间在后台成功捕获到了完整历书，极速写入 SD 卡根目录 /gpsalm.bin
            if (HAL::GPS_IsAlmanacHarvestReady())
            {
                uint32_t bufSize = 4096 + sizeof(HAL::GPS_Almanac_Header_t);
                uint8_t* almSaveBuffer = (uint8_t*)lv_mem_alloc(bufSize);
                if (almSaveBuffer != NULL)
                {
                    uint32_t nowUnix = (uint32_t)now();
                    uint32_t totalSize = HAL::GPS_GetHarvestedAlmanac(almSaveBuffer, bufSize, nowUnix);

                    if (totalSize > 0)
                    {
                        lv_fs_file_t saveFile;
                        if (lv_fs_open(&saveFile, CONFIG_GPS_ALMANAC_FILE_PATH, LV_FS_MODE_WR) == LV_FS_RES_OK)
                        {
                            uint32_t bytesWritten = 0;
                            lv_fs_write(&saveFile, almSaveBuffer, totalSize, &bytesWritten);
                            lv_fs_close(&saveFile);
                            LV_LOG_USER("GPS: Saved harvested almanac to %s (%d bytes)",
                                        CONFIG_GPS_ALMANAC_FILE_PATH, (int)bytesWritten);
                        }
                        else
                        {
                            LV_LOG_ERROR("GPS: Failed to open /gpsalm.bin for writing!");
                        }
                    }
                    lv_mem_free(almSaveBuffer);
                }
            }
#endif
        }
    }
    break;
    case Account::EVENT_SUB_PULL:
    {
        memcpy(info, &sysConfig, sizeof(sysConfig));
    }
    break;
    default:
        return Account::RES_UNSUPPORTED_REQUEST;
    }

    return Account::RES_OK;
}

DATA_PROC_INIT_DEF(SysConfig)
{
    account->Subscribe("Storage");
    account->Subscribe("GPS");
    account->Subscribe("Clock"); // AID-INI 辅助定位需要 RTC 时间，见 SYSCONFIG_CMD_LOAD 分支
#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
    account->Subscribe("Power");
#endif
    account->SetEventCallback(onEvent);
    account->SetTimerPeriod(500);

    memset(&sysConfig, 0, sizeof(sysConfig));

#   define SYSCGF_STRCPY(dest, src) \
do{ \
    strncpy(dest, src, sizeof(dest)); \
    dest[sizeof(dest) - 1] = '\0'; \
}while(0)

    sysConfig.cmd         = SYSCONFIG_CMD_LOAD;
    sysConfig.longitude   = CONFIG_GPS_LONGITUDE_DEFAULT;
    sysConfig.latitude    = CONFIG_GPS_LATITUDE_DEFAULT;
    sysConfig.timeZone    = CONFIG_SYSTEM_TIME_ZONE_DEFAULT;
    sysConfig.soundEnable = CONFIG_SYSTEM_SOUND_ENABLE_DEFAULT;
    sysConfig.screenBrightness = CONFIG_SCREEN_BRIGHTNESS_DEFAULT;
    sysConfig.autoDimBrightThresh = CONFIG_AUTO_DIM_BRIGHT_THRESH_DEFAULT;
    sysConfig.autoDimSpeedThresh  = CONFIG_AUTO_DIM_SPEED_THRESH_DEFAULT;
    sysConfig.autoDimTargetBright = CONFIG_AUTO_DIM_TARGET_BRIGHT_DEFAULT;
    SYSCGF_STRCPY(sysConfig.language, CONFIG_SYSTEM_LANGUAGE_DEFAULT);
    SYSCGF_STRCPY(sysConfig.arrowTheme, CONFIG_ARROW_THEME_DEFAULT);
    SYSCGF_STRCPY(sysConfig.mapDirPath, CONFIG_MAP_DIR_PATH_DEFAULT);
    SYSCGF_STRCPY(sysConfig.mapExtName, CONFIG_MAP_EXT_NAME_DEFAULT);
    sysConfig.mapWGS84    = CONFIG_MAP_USE_WGS84_DEFAULT;

#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
    sysConfig.designCap   = CONFIG_GAUGE_DESIGN_CAP_DEFAULT;
    sysConfig.fullChgCap  = CONFIG_GAUGE_FULL_CHG_CAP_DEFAULT;
#endif

    STORAGE_VALUE_REG(account, sysConfig.longitude,    STORAGE_TYPE_DOUBLE);
    STORAGE_VALUE_REG(account, sysConfig.latitude,     STORAGE_TYPE_DOUBLE);
    STORAGE_VALUE_REG(account, sysConfig.lastFixUnix,  STORAGE_TYPE_INT);

    STORAGE_VALUE_REG(account, sysConfig.soundEnable, STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sysConfig.screenBrightness, STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sysConfig.autoDimBrightThresh, STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sysConfig.autoDimSpeedThresh, STORAGE_TYPE_FLOAT);
    STORAGE_VALUE_REG(account, sysConfig.autoDimTargetBright, STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sysConfig.timeZone, STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sysConfig.language, STORAGE_TYPE_STRING);
    STORAGE_VALUE_REG(account, sysConfig.arrowTheme, STORAGE_TYPE_STRING);
    STORAGE_VALUE_REG(account, sysConfig.mapDirPath, STORAGE_TYPE_STRING);
    STORAGE_VALUE_REG(account, sysConfig.mapExtName, STORAGE_TYPE_STRING);
    STORAGE_VALUE_REG(account, sysConfig.mapWGS84, STORAGE_TYPE_INT);

#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
    STORAGE_VALUE_REG(account, sysConfig.designCap, STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sysConfig.fullChgCap, STORAGE_TYPE_INT);
#endif  
}
