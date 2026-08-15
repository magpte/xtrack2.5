#include "HAL.h"
#include "App/Version.h"
#include "MillisTaskManager/MillisTaskManager.h"
#include "lvgl/lvgl.h"

static MillisTaskManager taskManager;

#if CONFIG_WATCH_DOG_ENABLE
// 主循环心跳，见 main.cpp 的 loop() 末尾对 HAL::WatchDog_Feed() 的调用，
// 以及下面 HAL_TimerInterrputUpdate() 里怎么用它门控喂狗。
static volatile uint32_t s_mainLoopAliveTick = 0;
#endif

static bool HAL_I2C_Init()
{
    if(HAL::I2C_Scan() <= 0)
    {
        Serial.println("I2C: disable sensors");
        return false;
    }
    return true;
}

#if CONFIG_SENSOR_ENABLE

static void HAL_Sensor_Init()
{

#if CONFIG_SENSOR_IMU_ENABLE
    if(HAL::IMU_Init())
    {
        taskManager.Register(HAL::IMU_Update, 1000, true, "IMU_Update");
    }
#endif
}

#endif

static void HAL_LVGL_Update()
{
    lv_task_handler();
}

void HAL::Task_DumpDiagnostics()
{
    static char statsBuf[1024];
    taskManager.DumpTaskStats(statsBuf, sizeof(statsBuf));
    Serial.print(statsBuf);
}

static void HAL_TimerInterrputUpdate()
{
#if !CONFIG_LIPO_FUEL_GAUGE_ENABLE
    HAL::Power_Update();
#endif
    HAL::Encoder_Update();
    HAL::Audio_Update();

#if CONFIG_WATCH_DOG_ENABLE
    if(millis() - s_mainLoopAliveTick < CONFIG_WATCH_DOG_TIMEOUT)
    {
        WDG_ReloadCounter();
    }
#endif
}

void HAL::HAL_Init()
{
    Serial.begin(115200);
    Serial.println(VERSION_FIRMWARE_NAME);
    Serial.println("Version: " VERSION_SOFTWARE);
    Serial.println("Author: "  VERSION_AUTHOR_NAME);
    Serial.println("Project: " VERSION_PROJECT_LINK);
    
#if CONFIG_SENSOR_ENABLE
    bool hasI2CDevice = HAL_I2C_Init();
#endif

    FaultHandle_Init();

    Memory_DumpInfo();

    Power_Init();
    Backlight_Init();
    Encoder_Init();
    Clock_Init();
    Buzz_init();
    GPS_Init();
#if CONFIG_SENSOR_ENABLE
    if(hasI2CDevice){
        HAL_Sensor_Init();
    }
#endif
    Audio_Init();
    SD_Init();

    Display_Init();

#if CONFIG_WATCH_DOG_ENABLE
    uint32_t timeout = WDG_Init(CONFIG_WATCH_DOG_TIMEOUT);
    s_mainLoopAliveTick = millis();
    Serial.printf("WatchDog: Timeout = %dms\r\n", timeout);
#endif

    taskManager.Register(Power_EventMonitor, 100, true, "Power_EventMonitor");
    taskManager.Register(GPS_Update, 20, true, "GPS_Update"); // 高频 20ms 平滑调度
    taskManager.Register(SD_Update, 500, true, "SD_Update");
    taskManager.Register(Memory_DumpInfo, 1000, true, "Memory_DumpInfo");
    
#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
    taskManager.Register(Power_Update, 500, true, "Power_Update");
#endif

    taskManager.Register(HAL_LVGL_Update, 0, true, "LVGL_Render");

    Timer_SetInterrupt(CONFIG_HAL_UPDATE_TIM, 10 * 1000, HAL_TimerInterrputUpdate);
    Timer_SetEnable(CONFIG_HAL_UPDATE_TIM, true);
}

void HAL::HAL_Update()
{
    taskManager.Running(millis());
}

void HAL::WatchDog_Feed()
{
#if CONFIG_WATCH_DOG_ENABLE
    s_mainLoopAliveTick = millis();
#endif
}

void HAL::IMU_SetEnable(bool enable)
{
    taskManager.SetState(HAL::IMU_Update, enable);
}
