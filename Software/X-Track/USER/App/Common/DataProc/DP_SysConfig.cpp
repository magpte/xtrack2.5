#include "DataProc.h"
#include "../HAL/HAL.h"
#include "Config/Config.h"
#include "HAL/HAL_Config.h"
#include "Utils/Time/Time.h"

using namespace DataProc;

static SysConfig_Info_t sysConfig;

static int onEvent(Account* account, Account::EventParam_t* param)
{
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
            HAL::Backlight_SetGradual(sysConfig.screenBrightness, 1000);
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

                HAL::GPS_SendAidingData(sysConfig.latitude, sysConfig.longitude, utcClock);
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
            HAL::Backlight_SetGradual(sysConfig.screenBrightness, 100);
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
                sysConfig.longitude = (float)gpsInfo.longitude;
                sysConfig.latitude = (float)gpsInfo.latitude;
            }
#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
            HAL::Power_Info_t powerInfo;
            account->Pull("Power", &powerInfo, sizeof(powerInfo));
            // 存储电量计缓存数据，掉电或电量计POR可以恢复缓存
            if (sysConfig.fullChgCap > powerInfo.fullcharge_capacity){
                sysConfig.fullChgCap = powerInfo.fullcharge_capacity;
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
    SYSCGF_STRCPY(sysConfig.language, CONFIG_SYSTEM_LANGUAGE_DEFAULT);
    SYSCGF_STRCPY(sysConfig.arrowTheme, CONFIG_ARROW_THEME_DEFAULT);
    SYSCGF_STRCPY(sysConfig.mapDirPath, CONFIG_MAP_DIR_PATH_DEFAULT);
    SYSCGF_STRCPY(sysConfig.mapExtName, CONFIG_MAP_EXT_NAME_DEFAULT);
    sysConfig.mapWGS84    = CONFIG_MAP_USE_WGS84_DEFAULT;

#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
    sysConfig.designCap   = CONFIG_GAUGE_DESIGN_CAP_DEFAULT;
    sysConfig.fullChgCap  = CONFIG_GAUGE_FULL_CHG_CAP_DEFAULT;
#endif

    STORAGE_VALUE_REG(account, sysConfig.longitude, STORAGE_TYPE_FLOAT);
    STORAGE_VALUE_REG(account, sysConfig.latitude, STORAGE_TYPE_FLOAT);

    STORAGE_VALUE_REG(account, sysConfig.soundEnable, STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sysConfig.screenBrightness, STORAGE_TYPE_INT);
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
