#include "DataProc.h"
#include "../HAL/HAL.h"
#include "Config/Config.h"

typedef enum
{
    GPS_STATUS_DISCONNECT,
    GPS_STATUS_UNSTABLE,
    GPS_STATUS_CONNECT,
} GPS_Status_t;

static void onTimer(Account* account)
{
    HAL::GPS_Info_t gpsInfo;
    HAL::GPS_GetInfo(&gpsInfo);

    int satellites = gpsInfo.satellites;

    static GPS_Status_t nowStatus = GPS_STATUS_DISCONNECT;
    static GPS_Status_t lastStatus = GPS_STATUS_DISCONNECT;

    // Fix: 原版状态机在 satellites = 1/2/5/6/7 时无分支命中，nowStatus 保持旧值不更新。
    // 修正为连续完整覆盖：>7 星优质定位 → CONNECT，3~7 星勉强定位 → UNSTABLE，<3 → DISCONNECT
    if (satellites > 7)
    {
        nowStatus = GPS_STATUS_CONNECT;
    }
    else if (satellites >= 3)
    {
        nowStatus = GPS_STATUS_UNSTABLE;
    }
    else
    {
        nowStatus = GPS_STATUS_DISCONNECT;
    }

    if (nowStatus != lastStatus)
    {
        const char* music[] =
        {
            "Disconnect",
            "UnstableConnect",
            "Connect"
        };

        const char* statusStr[] =
        {
            "DISCONNECT",
            "UNSTABLE",
            "CONNECT"
        };

        HAL::SysLog_Write("[DP_GPS] Status changed: %s -> %s (Sats: %d, Valid: %d)",
            statusStr[lastStatus], statusStr[nowStatus], satellites, gpsInfo.isVaild ? 1 : 0);

        DataProc::MusicPlayer_Info_t info;
        DATA_PROC_INIT_STRUCT(info);
        info.music = music[nowStatus];
        account->Notify("MusicPlayer", &info, sizeof(info));
        lastStatus = nowStatus;
    }

    // Fix: 原版用 satellites >= 3 判断，但卫星数在 FIX LOST 后有最长 2.5s 延迟才归零，
    // 期间可能将过期坐标推送给上层 Pages。改用 isVaild（含 2.5s 时效超时判断）作为门控。
    if (gpsInfo.isVaild)
    {
        account->Commit(&gpsInfo, sizeof(gpsInfo));
        account->Publish();
    }
}

static int onEvent(Account* account, Account::EventParam_t* param)
{
    if (param->event == Account::EVENT_TIMER)
    {
        onTimer(account);
        return Account::RES_OK;
    }

    if (param->event != Account::EVENT_SUB_PULL)
    {
        return Account::RES_UNSUPPORTED_REQUEST;
    }

    if (param->size != sizeof(HAL::GPS_Info_t))
    {
        return Account::RES_SIZE_MISMATCH;
    }

    HAL::GPS_GetInfo((HAL::GPS_Info_t*)param->data_p);

    return Account::RES_OK;
}

DATA_PROC_INIT_DEF(GPS)
{
    account->Subscribe("MusicPlayer");

    account->SetEventCallback(onEvent);
    account->SetTimerPeriod(CONFIG_GPS_REFR_PERIOD);
}
