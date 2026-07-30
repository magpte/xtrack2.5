#include "HAL.h"
#include "App/Version.h"
#include "cm_backtrace/cm_backtrace.h"
#include "SdFat.h"  // ����SD��֧��  

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
      
    // д��crash.log�ļ�  
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
      
    // 由于两个按键的等待循环会阻塞程序执行，导致系统无法及时喂狗，
    // HardFault_Handler 在 ARM Cortex-M 中的中断优先级是固定最高的，
    // 不受 NVIC 中配置的优先级影响，比 HAL.cpp 中配置的
    // TIM4 定时器中断优先级更高。因此在这两个循环执行时，
    // TIM4 中断无法抢占执行，喂狗操作被阻塞，
    // IWDG 会在 CONFIG_WATCH_DOG_TIMEOUT（见 HAL_Config.h，目前为 10 秒）
    // 之后发生超时并触发硬件复位——也就是你看到的按键现象，
    // 按住超过 10 秒系统就会自动重启，并不是程序“卡死不动”。
    //
    // 等待按键释放的这个逻辑只是为了让用户有机会看到报警信息，
    // 并不是系统真正卡住或死循环。
    // 等待用户按键释放之后继续执行
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
