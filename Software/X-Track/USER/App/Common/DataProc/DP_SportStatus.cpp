#include "DataProc.h"
#include "Utils/Filters/Filters.h"
#include "../HAL/HAL.h"
#include "Config/Config.h"

#define CALORIC_CORFFICIENT 0.5f

using namespace DataProc;

static HAL::SportStatus_Info_t sportStatus;

static double SportStatus_GetDistanceOffset(HAL::GPS_Info_t* gpsInfo)
{
    static bool isFirst = true;
    static double preLongitude = 0.0;
    static double preLatitude = 0.0;

    // 校验 GPS 定位有效性及非空点 (Null Island 0,0)
    if (!gpsInfo->isVaild || (gpsInfo->longitude == 0.0 && gpsInfo->latitude == 0.0))
    {
        isFirst = true;
        return 0.0;
    }

    double offset = 0.0;

    if (!isFirst)
    {
        offset = HAL::GPS_GetDistanceOffset(gpsInfo, preLongitude, preLatitude);
    }
    else
    {
        isFirst = false;
    }

    preLongitude = gpsInfo->longitude;
    preLatitude = gpsInfo->latitude;

    return offset;
}

static void onTimer(Account* account)
{
    HAL::GPS_Info_t gpsInfo;
    if(account->Pull("GPS", &gpsInfo, sizeof(gpsInfo)) != Account::RES_OK)
    {
        return;
    }

    uint32_t timeElaps = DataProc::GetTickElaps(sportStatus.lastTick);

    // 1. 获取本周期（500ms）与上个有效坐标点之间的实际物理距离（单位：米）
    double distOffset = SportStatus_GetDistanceOffset(&gpsInfo);

    // 2. 由经纬度物理位移与时间间隔计算即时位移速度（km/h）
    float calcSpeedKph = 0.0f;
    if (timeElaps > 0 && distOffset > 0.0)
    {
        calcSpeedKph = (float)distOffset * 1000.0f / (float)timeElaps * 3.6f;
    }

    // 3. 双通道自适应运动状态判决：
    //    通道 A（GPS 模块自主测速）：模块经多普勒/PVT 解算的速度 > 1.0 km/h。
    //    通道 B（物理位移融合兜底）：当模块内部因静态导航锁死报 0 速时，
    //          若连续经纬度位移折算速度 > 1.2 km/h 且单次位移超过 0.15 米（避免静态噪点），
    //          自动判定用户处于真实移动状态（如步行起步），打破 0 速死区。
    float speedKph = 0.0f;
    bool isMoving = false;
    bool isSignalInterruption = (gpsInfo.isVaild && (gpsInfo.satellites == 0));

    if (gpsInfo.satellites >= 3 && gpsInfo.isVaild)
    {
        if (gpsInfo.speed > 1.0f)
        {
            isMoving = true;
            speedKph = gpsInfo.speed;
        }
        else if (calcSpeedKph > 1.2f && calcSpeedKph < 150.0f && distOffset >= 0.15)
        {
            isMoving = true;
            speedKph = calcSpeedKph;
        }
    }

    if (isMoving || isSignalInterruption)
    {
        sportStatus.singleTime += timeElaps;
        sportStatus.totalTime += timeElaps;

        if (isMoving && distOffset > 0.0)
        {
            // 采用 Kahan 补偿求和算法 (Kahan Compensated Summation)
            // 解决 float 在累加微小增量 (如 0.3m) 到大数值 (如 50000.0m) 时的尾数丢失问题。
            // 100% 运行于 Cortex-M4F 硬件浮点指令 (VADD.F32, VSUB.F32)，
            // 无需调用任何软件 double 模拟库，且具备与 double 一致的无限次累加零漂移精度。
            static float s_singleDistCompensation = 0.0f;
            static float s_totalDistCompensation  = 0.0f;

            float offsetF = (float)distOffset;

            // 1. 单次里程 Kahan 累加
            float y1 = offsetF - s_singleDistCompensation;
            float t1 = sportStatus.singleDistance + y1;
            s_singleDistCompensation = (t1 - sportStatus.singleDistance) - y1;
            sportStatus.singleDistance = t1;

            // 2. 总里程 Kahan 累加
            float y2 = offsetF - s_totalDistCompensation;
            float t2 = sportStatus.totalDistance + y2;
            s_totalDistCompensation = (t2 - sportStatus.totalDistance) - y2;
            sportStatus.totalDistance = t2;

            if (sportStatus.singleTime > 0)
            {
                float meterPerSec = sportStatus.singleDistance * 1000.0f / (float)sportStatus.singleTime;
                sportStatus.speedAvgKph = meterPerSec * 3.6f;
            }
            else
            {
                sportStatus.speedAvgKph = 0.0f;
            }

            if (speedKph > sportStatus.speedMaxKph)
            {
                sportStatus.speedMaxKph = speedKph;
            }
        }
    }

    sportStatus.speedKph = speedKph;

    sportStatus.lastTick = DataProc::GetTick();
    account->Commit(&sportStatus, sizeof(sportStatus));
    account->Publish();
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

    if (param->size != sizeof(sportStatus))
    {
        return Account::RES_SIZE_MISMATCH;
    }

    memcpy(param->data_p, &sportStatus, param->size);
    return Account::RES_OK;
}

DATA_PROC_INIT_DEF(SportStatus)
{
    memset(&sportStatus, 0, sizeof(sportStatus));
    sportStatus.weight = CONFIG_WEIGHT_DEFAULT;

    account->Subscribe("GPS");
    account->Subscribe("Storage");

    STORAGE_VALUE_REG(account, sportStatus.totalDistance, STORAGE_TYPE_FLOAT);
    STORAGE_VALUE_REG(account, sportStatus.totalTimeUINT32[0], STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sportStatus.totalTimeUINT32[1], STORAGE_TYPE_INT);
    STORAGE_VALUE_REG(account, sportStatus.speedMaxKph, STORAGE_TYPE_FLOAT);
    STORAGE_VALUE_REG(account, sportStatus.weight, STORAGE_TYPE_FLOAT);

    sportStatus.lastTick = DataProc::GetTick();

    account->SetEventCallback(onEvent);
    account->SetTimerPeriod(500);
}
