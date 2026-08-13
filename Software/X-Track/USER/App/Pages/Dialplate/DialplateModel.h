#ifndef __DIALPLATE_MODEL_H
#define __DIALPLATE_MODEL_H

#include "Common/DataProc/DataProc.h"
#include "TinyGPSPlus/src/TinyGPS++.h"

namespace Page
{

class DialplateModel
{
public:
    typedef enum
    {
        REC_START    = DataProc::RECORDER_CMD_START,
        REC_PAUSE    = DataProc::RECORDER_CMD_PAUSE,
        REC_CONTINUE = DataProc::RECORDER_CMD_CONTINUE,
        REC_STOP     = DataProc::RECORDER_CMD_STOP,
        REC_READY_STOP
    } RecCmd_t;

public:
    HAL::SportStatus_Info_t sportStatusInfo;

public:
    void Init();
    void Deinit();

    bool GetGPSReady();

    float GetSpeed()
    {
        return sportStatusInfo.speedKph;
    }
		
    float GetAvgSpeed()
    {
        return sportStatusInfo.speedAvgKph;
    }
		
	  float GetCourse()  
		{  
        HAL::GPS_Info_t gps;  
        if(account->Pull("GPS", &gps, sizeof(gps)) != Account::RES_OK)  
        {  
            return 0.0f;  
        }  
        return gps.course;  
		}  
  
    const char* GetCourseDirection()  
    {  
        float course = GetCourse();  
        while (course < 0) course += 360.0f;
        while (course >= 360.0f) course -= 360.0f;
        return TinyGPSPlus::cardinal(course);  
    }

    void RecorderCommand(RecCmd_t cmd);
    void PlayMusic(const char* music);
    void SetStatusBarStyle(DataProc::StatusBar_Style_t style);

    int32_t GetScreenBrightness();
    void SetScreenBrightness(int32_t value);

private:
    Account* account;

private:
    static int onEvent(Account* account, Account::EventParam_t* param);
};

}

#endif
