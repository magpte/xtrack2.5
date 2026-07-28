#include "HAL.h"
#include "App/Version.h"
#include "cm_backtrace/cm_backtrace.h"
#include "SdFat.h"  // 添加SD卡支持  

static void Delay(uint32_t ms)
{
    volatile uint32_t i = F_CPU / 1000 * ms / 5;
    while(i--);
}


void HAL::FaultHandle_Init()
{
    cm_backtrace_init(
        VERSION_FIRMWARE_NAME,
        VERSION_HARDWARE,
        VERSION_SOFTWARE " " __DATE__
    );
}

void cmb_printf(const char *__restrict __format, ...)  
{  
    char printf_buff[256];  
      
    va_list args;  
    va_start(args, __format);  
    int ret_status = vsnprintf(printf_buff, sizeof(printf_buff), __format, args);  
    va_end(args);  
      
    // 写入crash.log文件  
    HAL::SD_WriteCrashLog(printf_buff);  
}

extern "C"
{
    /*
    void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
    {
        char strBuf[configMAX_TASK_NAME_LEN + 1];
        snprintf(strBuf, sizeof(strBuf), "stack overflow\n < %s >", pcTaskName);
        DisplayError_SetReports(strBuf);
        Reboot();
    }
    
    void vApplicationMallocFailedHook()
    {
        DisplayError_SetReports("malloc failed");
        Reboot();
    }
    */
    
    void vApplicationHardFaultHook()  
    {  
    HAL::Display_DumpCrashInfo("FXXK HardFault!");  
      
    // 下面这两个等按键的循环理论上会一直等下去，但实际上不会真的无限等待——
    // HardFault_Handler 在 ARM Cortex-M 上的中断优先级固定是最高的，
    // 不受 NVIC 里配置的优先级影响，比 HAL.cpp 里喂狗用的
    // TIM4 定时器中断优先级更高。所以卡在这两个循环里的时候，
    // TIM4 中断根本没法抢占执行，喂狗跟着停摆，
    // IWDG 会在 CONFIG_WATCH_DOG_TIMEOUT（见 HAL_Config.h，目前 10 秒）
    // 之后自然超时硬复位——也就是说没人按键的话，
    // 最多 10 秒左右也会自动重启，不是真的卡死不动。
    // 等按键这个设计本身是为了让人有机会先看到崩溃信息再重启，
    // 不是想让设备真的死等。
    // 等待用户按键后再重启  
    while(digitalRead(CONFIG_ENCODER_PUSH_PIN) == HIGH)  
    {  
        Delay(100);  // 减少延时，提高响应性  
    }  
      
    // 等待按键释放  
    while(digitalRead(CONFIG_ENCODER_PUSH_PIN) == LOW)  
    {  
        Delay(100);  
    }  
      
    NVIC_SystemReset();  
    }
    
    __asm void HardFault_Handler()
    {
        extern vApplicationHardFaultHook
        extern cm_backtrace_fault
            
        mov r0, lr
        mov r1, sp
        bl cm_backtrace_fault
        bl vApplicationHardFaultHook
fault_loop
        b fault_loop
    }
}
