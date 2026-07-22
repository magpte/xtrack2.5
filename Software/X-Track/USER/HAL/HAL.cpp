#include "HAL.h"
#include "App/Version.h"
#include "MillisTaskManager/MillisTaskManager.h"
#include "EventRecorder.h"   // 调试用，排查完可移除（连同 HAL_Init() 里那行初始化调用）

static MillisTaskManager taskManager;

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
        taskManager.Register(HAL::IMU_Update, 1000);
    }
#endif
}

#endif

static void HAL_TimerInterrputUpdate()
{
#if !CONFIG_LIPO_FUEL_GAUGE_ENABLE
    HAL::Power_Update();
#endif
    HAL::Encoder_Update();
    HAL::Audio_Update();

#if CONFIG_WATCH_DOG_ENABLE
    // 喂狗从协作式任务调度器（taskManager）挪到这个硬件定时器中断里。
    // 原来喂狗是靠 taskManager.Register 注册的一个"任务"，在主循环里跑，
    // 一旦主循环被某个阻塞调用卡住（比如 SD 卡 sync() 偶尔慢一拍），
    // 喂狗也会跟着停摆，看门狗超时把整个系统硬复位——哪怕那次阻塞本身
    // 并不是真的死循环，最终还是会执行完。
    // 挪到这里之后，喂狗完全独立于主循环是否卡顿，只要这颗硬件定时器
    // 中断本身还在正常触发（这是芯片级别的，不受协作式调度影响），狗
    // 就一直能被喂到。代价是看门狗因此没法再检测"主循环被慢操作偶尔
    // 卡住"这种情况了——这是有意为之：这类情况应该靠别的手段处理（比如
    // DP_Recorder.cpp 里新加的写入缓冲，减少同步的次数和阻塞概率），
    // 看门狗只用来兜底"真的死循环/死锁"这种更严重的问题。
    WDG_ReloadCounter();
#endif
}

void HAL::HAL_Init()
{
    // 调试用：必须在 GPS_Init()/SD_Init() 之前、也就是 HAL_Init() 一进来
    // 就调用，不然后面那些 EventRecord2() 全都是空调用。排查完可整段删掉。
    EventRecorderInitialize(EventRecordAll, 1);

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
    // 喂狗改到 HAL_TimerInterrputUpdate() 里的硬件定时器中断执行，
    // 不再靠 taskManager 这个协作式调度器喂狗——见上面的注释说明原因。
    Serial.printf("WatchDog: Timeout = %dms\r\n", timeout);
#endif

    taskManager.Register(Power_EventMonitor, 100);
    taskManager.Register(GPS_Update, 200);
    taskManager.Register(SD_Update, 500);
    taskManager.Register(Memory_DumpInfo, 1000);
    
#if CONFIG_LIPO_FUEL_GAUGE_ENABLE
    taskManager.Register(Power_Update, 500);
#endif
    Timer_SetInterrupt(CONFIG_HAL_UPDATE_TIM, 10 * 1000, HAL_TimerInterrputUpdate);
    Timer_SetEnable(CONFIG_HAL_UPDATE_TIM, true);
}

void HAL::HAL_Update()
{
    taskManager.Running(millis());
}

void HAL::IMU_SetEnable(bool enable)
{
    taskManager.SetState(HAL::IMU_Update, enable);
}
