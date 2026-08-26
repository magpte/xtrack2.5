#include "DataProc.h"
#include "Utils/MapConv/MapConv.h"
#include "Utils/TrackFilter/TrackFilter.h"
#include "Utils/PointContainer/PointContainer.h"
#include "Config/Config.h"
#include <math.h>

using namespace DataProc;

typedef struct
{
    MapConv mapConv;
    TrackPointFilter pointFilter;
    PointContainer* pointContainer;
    bool isStarted;
    bool isActive;

    // IMU 航位推算 (Dead Reckoning) 状态（仅在 GPS 信号丢失时启用）
    bool hasLastValidGps;
    float lastValidSpeedKph;
    uint32_t lastGpsTick;
    bool isDeadReckoning;
    float drMapX;
    float drMapY;
    float drCourse;
    float drSpeedKph;
    uint32_t lastDrTick;
} TrackFilter_t;

static TrackFilter_t trackFilter;

static void onNotify(Account* account, TrackFilter_Info_t* info)
{
    switch (info->cmd)
    {
    case TRACK_FILTER_CMD_START:
        if (trackFilter.pointContainer != nullptr)
        {
            delete trackFilter.pointContainer;
            trackFilter.pointContainer = nullptr;
        }
        trackFilter.pointContainer = new PointContainer;
        trackFilter.pointFilter.Reset();
        trackFilter.hasLastValidGps = false;
        trackFilter.isDeadReckoning = false;
        trackFilter.isActive = true;
        trackFilter.isStarted = true;
        LV_LOG_USER("Track filter start: TrackPointFilter + IMU DR fallback enabled");
        break;
    case TRACK_FILTER_CMD_PAUSE:
        trackFilter.isActive = false;
        LV_LOG_USER("Track filter pause");
        break;
    case TRACK_FILTER_CMD_CONTINUE:
        trackFilter.isActive = true;
        LV_LOG_USER("Track filter continue");
        break;
    case TRACK_FILTER_CMD_STOP:
    {
        trackFilter.isStarted = false;
        trackFilter.isActive = false;

        if (trackFilter.pointContainer)
        {
            delete trackFilter.pointContainer;
            trackFilter.pointContainer = nullptr;
        }

        uint32_t sum = 0, output = 0;
        trackFilter.pointFilter.GetCounts(&sum, &output);
        LV_LOG_USER(
            "Track filter stop, filted(%d%%): sum = %d, output = %d",
            sum ? (100 - output * 100 / sum) : 0,
            sum,
            output
        );
        break;
    }
    default:
        break;
    }
}

static void onPublish(Account* account, HAL::GPS_Info_t* gps)
{
    if (!trackFilter.isStarted || !trackFilter.isActive || trackFilter.pointContainer == nullptr)
    {
        return;
    }

    uint32_t now = DataProc::GetTick();

    // 1. GPS 信号有效：采用原版 TrackPointFilter 纯几何法线转折与基线偏距提取特征点
    if (gps->isVaild && (gps->longitude != 0.0 || gps->latitude != 0.0) && gps->satellites >= 3)
    {
        int32_t mapX, mapY;
        trackFilter.mapConv.ConvertMapCoordinate(
            gps->longitude,
            gps->latitude,
            &mapX,
            &mapY
        );

        if (trackFilter.pointFilter.PushPoint(mapX, mapY))
        {
            trackFilter.pointContainer->PushPoint(mapX, mapY);
        }

        // 平滑重置惯导状态基准
        trackFilter.isDeadReckoning = false;
        trackFilter.hasLastValidGps = true;
        trackFilter.lastValidSpeedKph = gps->speed;
        trackFilter.lastGpsTick = now;
        trackFilter.drMapX = (float)mapX;
        trackFilter.drMapY = (float)mapY;
        trackFilter.drCourse = gps->course;
        trackFilter.drSpeedKph = gps->speed;
        trackFilter.lastDrTick = now;
    }
    // 2. GPS 信号丢失 (例如进入隧道/林荫路段)：启用 IMU 航位推算 (Dead Reckoning)
    else if (trackFilter.hasLastValidGps && trackFilter.lastValidSpeedKph >= 1.5f)
    {
        uint32_t lossDuration = now - trackFilter.lastGpsTick;
        // 信号丢失在 120 秒内且仍在移动时，进行惯导推演拟合
        if (lossDuration < 120000)
        {
            HAL::IMU_Info_t imuInfo;
            if (account->Pull("IMU", &imuInfo, sizeof(imuInfo)) == Account::RES_OK)
            {
                float dt = (now - trackFilter.lastDrTick) / 1000.0f;
                if (dt >= 0.4f) // 约 2Hz 推演步长
                {
                    // LSM6DSM 陀螺仪 Z 轴角速度 (FS 500dps: ~0.01526 deg/s/LSB)
                    float gzDps = (float)imuInfo.gz * 0.01526f;
                    if (fabsf(gzDps) < 1.0f)
                    {
                        gzDps = 0.0f; // 消除静止陀螺零偏噪声
                    }

                    trackFilter.drCourse += gzDps * dt;
                    while (trackFilter.drCourse < 0.0f) trackFilter.drCourse += 360.0f;
                    while (trackFilter.drCourse >= 360.0f) trackFilter.drCourse -= 360.0f;

                    // 惯性平滑阻力轻微衰减
                    trackFilter.drSpeedKph *= 0.995f;

                    // 距离推算 (米 -> 瓦片像素，根据当前 MapConv 缩放层级自适应地面分辨率，Level 18 约 0.52m/px)
                    float distMeters = (trackFilter.drSpeedKph * (1000.0f / 3600.0f)) * dt;
                    float groundRes = (float)Microsoft_MapPoint::TileSystem::GroundResolution(30.0, trackFilter.mapConv.GetLevel());
                    if (groundRes < 0.05f) groundRes = 0.52f;
                    float distPixels = distMeters / groundRes;
                    float rad = trackFilter.drCourse * (3.14159265f / 180.0f);

                    trackFilter.drMapX += distPixels * sinf(rad);
                    trackFilter.drMapY -= distPixels * cosf(rad);

                    int32_t curDrX = (int32_t)roundf(trackFilter.drMapX);
                    int32_t curDrY = (int32_t)roundf(trackFilter.drMapY);

                    if (trackFilter.pointFilter.PushPoint(curDrX, curDrY))
                    {
                        trackFilter.pointContainer->PushPoint(curDrX, curDrY);
                    }

                    trackFilter.lastDrTick = now;
                    trackFilter.isDeadReckoning = true;
                }
            }
        }
    }
}

static int onEvent(Account* account, Account::EventParam_t* param)
{
    if (param->event == Account::EVENT_PUB_PUBLISH
            && param->size == sizeof(HAL::GPS_Info_t))
    {
        if (trackFilter.isActive)
        {
            onPublish(account, (HAL::GPS_Info_t*)param->data_p);
        }

        return Account::RES_OK;
    }

    if (param->size != sizeof(TrackFilter_Info_t))
    {
        return Account::RES_SIZE_MISMATCH;
    }

    switch (param->event)
    {
    case Account::EVENT_SUB_PULL:
    {
        TrackFilter_Info_t* info = (TrackFilter_Info_t*)param->data_p;
        info->pointCont = trackFilter.pointContainer;
        info->level = (uint8_t)trackFilter.mapConv.GetLevel();
        info->isActive = trackFilter.isStarted;
        break;
    }
    case Account::EVENT_NOTIFY:
        onNotify(account, (TrackFilter_Info_t*)param->data_p);
        break;

    default:
        break;
    }

    return Account::RES_OK;
}

DATA_PROC_INIT_DEF(TrackFilter)
{
    account->Subscribe("GPS");
    account->Subscribe("IMU");
    account->SetEventCallback(onEvent);

    trackFilter.pointContainer = nullptr;
    trackFilter.isActive = false;
    trackFilter.isStarted = false;
    trackFilter.hasLastValidGps = false;
    trackFilter.isDeadReckoning = false;

    trackFilter.mapConv.SetLevel(CONFIG_TRACK_BASE_LEVEL);
    trackFilter.pointFilter.SetOffsetThreshold(CONFIG_TRACK_FILTER_OFFSET_THRESHOLD);
}
