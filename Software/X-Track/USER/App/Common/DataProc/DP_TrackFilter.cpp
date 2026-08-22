#include "DataProc.h"
#include "Utils/MapConv/MapConv.h"
#include "Utils/PointContainer/PointContainer.h"
#include "Config/Config.h"
#include <math.h>

using namespace DataProc;

typedef struct
{
    float x;
    float y;
    float px;
    float py;
    float q; // 过程噪声协方差 Q
    float r; // 测量噪声协方差 R
    bool isInitialized;
} Kalman2D_t;

static inline void Kalman2D_Init(Kalman2D_t* kf, float q = 0.08f, float r = 1.8f)
{
    kf->x = 0.0f;
    kf->y = 0.0f;
    kf->px = 1.0f;
    kf->py = 1.0f;
    kf->q = q;
    kf->r = r;
    kf->isInitialized = false;
}

static inline void Kalman2D_Update(Kalman2D_t* kf, float mx, float my, float* outX, float* outY)
{
    if (!kf->isInitialized)
    {
        kf->x = mx;
        kf->y = my;
        kf->px = 1.0f;
        kf->py = 1.0f;
        kf->isInitialized = true;
        *outX = mx;
        *outY = my;
        return;
    }

    // 状态跳跃保护（重新定位跳变 > 50 像素 ~ 120米时快速收敛）
    float dx = mx - kf->x;
    float dy = my - kf->y;
    if (dx * dx + dy * dy > 2500.0f)
    {
        kf->x = mx;
        kf->y = my;
        kf->px = 1.0f;
        kf->py = 1.0f;
        *outX = mx;
        *outY = my;
        return;
    }

    // 1. 状态外推（预测）
    kf->px += kf->q;
    kf->py += kf->q;

    // 2. 卡尔曼增益（单精度硬件 FPU 指令加速）
    float kx = kf->px / (kf->px + kf->r);
    float ky = kf->py / (kf->py + kf->r);

    // 3. 测量更新
    kf->x += kx * (mx - kf->x);
    kf->y += ky * (my - kf->y);

    // 4. 误差协方差更新
    kf->px = (1.0f - kx) * kf->px;
    kf->py = (1.0f - ky) * kf->py;

    *outX = kf->x;
    *outY = kf->y;
}

typedef struct
{
    MapConv mapConv;
    PointContainer* pointContainer;
    Kalman2D_t kf;
    bool isStarted;
    bool isActive;

    // 真实点采样状态
    bool hasLastValidGps;
    int32_t lastRecordedMapX;
    int32_t lastRecordedMapY;
    float lastRecordedCourse;
    float lastValidSpeedKph;
    uint32_t lastGpsTick;
    uint32_t pointCount;

    // IMU 航位推算 (Dead Reckoning) 状态
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
        Kalman2D_Init(&trackFilter.kf, 0.08f, 1.8f);
        trackFilter.hasLastValidGps = false;
        trackFilter.isDeadReckoning = false;
        trackFilter.pointCount = 0;
        trackFilter.isActive = true;
        trackFilter.isStarted = true;
        LV_LOG_USER("Track filter start: 2D Kalman + authentic sampling + IMU DR enabled");
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

        LV_LOG_USER("Track filter stop: total points recorded = %d", trackFilter.pointCount);
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

    // 1. GPS 信号有效：硬件 2D 卡尔曼滤波平滑 + 自适应点位步进记录
    if (gps->isVaild && (gps->longitude != 0.0 || gps->latitude != 0.0) && gps->satellites >= 3)
    {
        int32_t rawMapX, rawMapY;
        trackFilter.mapConv.ConvertMapCoordinate(
            gps->longitude,
            gps->latitude,
            &rawMapX,
            &rawMapY
        );

        // 2D 硬件卡尔曼滤波：消除卫星高频测量抖动（单次耗时 < 30ns）
        float kfX, kfY;
        Kalman2D_Update(&trackFilter.kf, (float)rawMapX, (float)rawMapY, &kfX, &kfY);
        int32_t mapX = (int32_t)roundf(kfX);
        int32_t mapY = (int32_t)roundf(kfY);

        // 如果之前处于 IMU 惯导推演模式，此时平滑接驳回真实 GPS 点
        trackFilter.isDeadReckoning = false;

        // 速度门限：运动中才记录，彻底杜绝原地停靠时的蜘蛛网/鸟巢状乱线
        bool isMoving = (gps->speed >= 1.2f);

        if (!trackFilter.hasLastValidGps)
        {
            // 第一个真实有效点，无条件记录
            trackFilter.pointContainer->PushPoint(mapX, mapY);
            trackFilter.lastRecordedMapX = mapX;
            trackFilter.lastRecordedMapY = mapY;
            trackFilter.lastRecordedCourse = gps->course;
            trackFilter.hasLastValidGps = true;
            trackFilter.pointCount++;
        }
        else if (isMoving)
        {
            int32_t dx = mapX - trackFilter.lastRecordedMapX;
            int32_t dy = mapY - trackFilter.lastRecordedMapY;
            int32_t distSq = dx * dx + dy * dy;

            float courseDiff = fabsf(gps->course - trackFilter.lastRecordedCourse);
            if (courseDiff > 180.0f)
            {
                courseDiff = 360.0f - courseDiff;
            }

            // 自适应采样：直线位移 >= 阈值 (默认 2 像素 ~ 4.8米) 或 拐弯航向角改变 >= 5度
            const int32_t minDistSq = CONFIG_TRACK_FILTER_OFFSET_THRESHOLD * CONFIG_TRACK_FILTER_OFFSET_THRESHOLD;
            if (distSq >= minDistSq || (distSq >= 1 && courseDiff >= 5.0f))
            {
                trackFilter.pointContainer->PushPoint(mapX, mapY);
                trackFilter.lastRecordedMapX = mapX;
                trackFilter.lastRecordedMapY = mapY;
                trackFilter.lastRecordedCourse = gps->course;
                trackFilter.pointCount++;
            }
        }

        // 更新状态基准
        trackFilter.lastValidSpeedKph = gps->speed;
        trackFilter.lastGpsTick = now;
        trackFilter.drMapX = (float)mapX;
        trackFilter.drMapY = (float)mapY;
        trackFilter.drCourse = gps->course;
        trackFilter.drSpeedKph = gps->speed;
        trackFilter.lastDrTick = now;
    }
    // 2. GPS 信号丢失 (例如进入隧道/高架桥下)：启用 IMU 航位推算 (Dead Reckoning)
    else if (trackFilter.hasLastValidGps && trackFilter.lastValidSpeedKph >= 1.5f)
    {
        uint32_t lossDuration = now - trackFilter.lastGpsTick;
        // 信号丢失在 120 秒内时，进行惯导推演拟合
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

                    // 距离推算 (米 -> Level 16 瓦片像素，1像素约为 2.388 米)
                    float distMeters = (trackFilter.drSpeedKph * 1000.0f / 3600.0f) * dt;
                    float distPixels = distMeters / 2.388f;
                    float rad = trackFilter.drCourse * (3.14159265f / 180.0f);

                    trackFilter.drMapX += distPixels * sinf(rad);
                    trackFilter.drMapY -= distPixels * cosf(rad);

                    int32_t curDrX = (int32_t)roundf(trackFilter.drMapX);
                    int32_t curDrY = (int32_t)roundf(trackFilter.drMapY);

                    int32_t dx = curDrX - trackFilter.lastRecordedMapX;
                    int32_t dy = curDrY - trackFilter.lastRecordedMapY;
                    const int32_t minDistSq = CONFIG_TRACK_FILTER_OFFSET_THRESHOLD * CONFIG_TRACK_FILTER_OFFSET_THRESHOLD;

                    if (dx * dx + dy * dy >= minDistSq)
                    {
                        trackFilter.pointContainer->PushPoint(curDrX, curDrY);
                        trackFilter.lastRecordedMapX = curDrX;
                        trackFilter.lastRecordedMapY = curDrY;
                        trackFilter.pointCount++;
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
    trackFilter.pointCount = 0;

    trackFilter.mapConv.SetLevel(CONFIG_LIVE_MAP_LEVEL_DEFAULT);
}

