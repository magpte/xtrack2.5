#ifndef __SYSTEM_INFOS_MODEL_H
#define __SYSTEM_INFOS_MODEL_H

#include "Common/DataProc/DataProc.h"

namespace Page
{

class SystemInfosModel
{
public:
    void Init();
    void Deinit();

    void GetSportInfo(
        float* trip,
        char* time, uint32_t len,
        float* maxSpd
    );

    void GetGPSInfo(
        double* lat,
        double* lng,
        float* alt,
        char* utc, uint32_t len,
        float* course,
        float* speed,
        float* pdop
    );

    bool GetIMUInfo(
        int* step,
        char* info, uint32_t len
    );

    void GetRTCInfo(
        char* dateTime, uint32_t len
    );

    void GetBatteryInfo(
        int* usage,
        float* voltage,
        char* state, uint32_t len
    );

    void GetStorageInfo(
        bool* detect,
        const char** type,
        char* size, uint32_t len
    );

    void GetMemoryInfo(
        char* stackInfo, uint32_t stackLen,
        char* heapInfo, uint32_t heapLen
    );

    void GetSkyInfo(HAL::Sky_Info_t* info);

    void SetStatusBarStyle(DataProc::StatusBar_Style_t style);

private:
    Account* account;

private:

};

}

#endif
