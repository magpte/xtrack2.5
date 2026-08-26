#include "LiveMapModel.h"
#include "Config/Config.h"
#include "Utils/PointContainer/PointContainer.h"

using namespace Page;

LiveMapModel::LiveMapModel()
{

}

void LiveMapModel::Init()
{
    account = new Account("LiveMapModel", DataProc::Center(), 0, this);
    account->Subscribe("GPS");
    accountSportStatus = account->Subscribe("SportStatus");
    account->Subscribe("TrackFilter");
    account->Subscribe("SysConfig");
    account->Subscribe("StatusBar");
    account->SetEventCallback(onEvent);
}

void LiveMapModel::Deinit()
{
    if (account)
    {
        delete account;
        account = nullptr;
        accountSportStatus = nullptr;
    }
}

void LiveMapModel::GetGPS_Info(HAL::GPS_Info_t* info)
{
    memset(info, 0, sizeof(HAL::GPS_Info_t));
    account->Pull("GPS", info, sizeof(HAL::GPS_Info_t));
}

void LiveMapModel::GetDefaultCoord(double* longitude, double* latitude)
{
    *longitude = CONFIG_GPS_LONGITUDE_DEFAULT;
    *latitude = CONFIG_GPS_LATITUDE_DEFAULT;

    DataProc::SysConfig_Info_t sysConfig;
    if(account->Pull("SysConfig", &sysConfig, sizeof(sysConfig)) == Account::RES_OK)
    {
        if (sysConfig.longitude != 0.0 || sysConfig.latitude != 0.0)
        {
            *longitude = sysConfig.longitude;
            *latitude = sysConfig.latitude;
        }
    }
}

void LiveMapModel::GetArrowTheme(char* buf, uint32_t size)
{
    DataProc::SysConfig_Info_t sysConfig;
    if(account->Pull("SysConfig", &sysConfig, sizeof(sysConfig)) != Account::RES_OK)
    {
        buf[0] = '\0';
        return;
    }
    strncpy(buf, sysConfig.arrowTheme, size);
    buf[size - 1] = '\0';
}

bool LiveMapModel::GetTrackFilterActive()
{
    DataProc::TrackFilter_Info_t info;
    if(account->Pull("TrackFilter", &info, sizeof(info)) != Account::RES_OK)
    {
        return false;
    }

    return info.isActive;
}

int LiveMapModel::onEvent(Account* account, Account::EventParam_t* param)
{
    if (param->event != Account::EVENT_PUB_PUBLISH)
    {
        return Account::RES_UNSUPPORTED_REQUEST;
    }

    LiveMapModel* instance = (LiveMapModel*)account->UserData;
    if (param->tran != instance->accountSportStatus || param->size != sizeof(HAL::SportStatus_Info_t))
    {
        return Account::RES_PARAM_ERROR;
    }

    memcpy(&(instance->sportStatusInfo), param->data_p, param->size);

    return Account::RES_OK;
}

void LiveMapModel::TrackReload(TrackPointFilter::Callback_t callback, void* userData, const TrackLineFilter::Area_t* area)
{
    DataProc::TrackFilter_Info_t info;
    if(account->Pull("TrackFilter", &info, sizeof(info)) != Account::RES_OK)
    {
        return;
    }

    if (!info.isActive || info.pointCont == nullptr)
    {
        return;
    }

    PointContainer* pointContainer = (PointContainer*)info.pointCont;

    // 采用全量连续点流遍历，确保跨分块线段 100% 连贯，彻底消除小视口粗筛截断引起的悬空断线
    pointContainer->PopStart();

    TrackPointFilter dummyFilter;
    dummyFilter.userData = userData;

    int32_t pointX, pointY;
    while (pointContainer->PopPoint(&pointX, &pointY))
    {
        int32_t mapX, mapY;
        mapConv.ConvertMapLevelPos(
            &mapX, &mapY,
            pointX, pointY,
            info.level
        );

        if (callback)
        {
            TrackPointFilter::Point_t pt = { mapX, mapY };
            callback(&dummyFilter, &pt);
        }
    }
}

void LiveMapModel::SetStatusBarStyle(DataProc::StatusBar_Style_t style)
{
    DataProc::StatusBar_Info_t info;
    DATA_PROC_INIT_STRUCT(info);

    info.cmd = DataProc::STATUS_BAR_CMD_SET_STYLE;
    info.param.style = style;

    account->Notify("StatusBar", &info, sizeof(info));
}
