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
            // 骑行记录用 double 做累加，避免长时间骑行下 float 累加误差
            // 逐渐放大（Cortex-M4 只有单精度硬件 FPU，double 运算会由
            // 编译器插入软件浮点库完成——这里只让这两行关键累加走软件
            // 浮点，换取记录数值的正确性；HAL::SportStatus_Info_t 里
            // 对外/存盘用的 singleDistance/totalDistance 字段本身仍然
            // 是 float，不改动存储格式和其它读取它们的 UI 代码）。
            //
            // s_totalDistanceAccum 用 -1.0 当"尚未从存档/默认值接管"
            // 的哨兵：totalDistance 在 DATA_PROC_INIT_DEF 里注册为
            // STORAGE_VALUE_REG(..., STORAGE_TYPE_FLOAT)，开机时可能
            // 被存储子系统直接写回一个非零的历史值，这里第一次真正开始
            // 累加时才把它接过来，而不是从 0 起步覆盖掉已保存的里程。
            static double s_singleDistanceAccum = 0.0;
            static double s_totalDistanceAccum = -1.0;

            if (s_totalDistanceAccum < 0.0)
            {
                s_totalDistanceAccum = (double)sportStatus.totalDistance;
            }

            s_singleDistanceAccum += distOffset;
            s_totalDistanceAccum += distOffset;

            sportStatus.singleDistance = (float)s_singleDistanceAccum;
            sportStatus.totalDistance = (float)s_totalDistanceAccum;

            if (sportStatus.singleTime > 0)
            {
                float meterPerSec = sportStatus.singleDistance * 1000.0f / sportStatus.singleTime;
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
