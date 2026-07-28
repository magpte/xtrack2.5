#include "HAL.h"
#include "App/Version.h"
#include "MillisTaskManager/MillisTaskManager.h"

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
    // 就一直能被喂到。
    //
    // 但这样会带来一个真空：如果主循环是真的卡死（死循环/死锁，不是
    // 偶尔慢一拍），既没有触发 CPU 异常（HardFault_Handler 那条路径
    // 走不到），这个定时器中断又是普通外设中断、跟主循环状态无关、
    // 照样正常触发——狗会被永远喂饱，IWDG 永远不超时，设备卡死了也
    // 不会自动复位。
    // 用 s_mainLoopAliveTick 这个心跳补上这个真空：只有主循环最近
    // （CONFIG_WATCH_DOG_TIMEOUT 时间窗口内）确实完整跑完过一圈，才
    // 真正喂狗；主循环卡死导致心跳长时间不更新的话，这里主动不喂，
    // 让 IWDG 自然超时硬复位。一次性的慢操作（比如 SD sync 偶尔卡
    // 一下）只要整体上没有超过这个时间窗口，心跳还是能追上，不会
    // 误触发。
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
    // 喂狗改到 HAL_TimerInterrputUpdate() 里的硬件定时器中断执行，
    // 不再靠 taskManager 这个协作式调度器喂狗——见上面的注释说明原因。
    // 心跳先初始化成当前时间，避免定时器一使能、第一次中断里的心跳
    // 判断就是拿 millis() 去减一个陈旧的 0，虽然这种情况下差值本来
    // 也小，不会真的误判，这里只是让初始状态更明确、不留歧义。
    s_mainLoopAliveTick = millis();
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
