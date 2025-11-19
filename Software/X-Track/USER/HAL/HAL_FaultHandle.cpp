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
