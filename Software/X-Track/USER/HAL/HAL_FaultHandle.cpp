#include "HAL.h"
#include "App/Version.h"
#include "cm_backtrace/cm_backtrace.h"
#include "SdFat.h"  // SDKazuchi

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
    vsnprintf(printf_buff, sizeof(printf_buff), __format, args);  
    va_end(args);  
      
    Serial.print(printf_buff);
    HAL::SD_WriteCrashLog(printf_buff);  
}

extern "C"
{
    void vApplicationHardFaultHook()  
    {  
        HAL::Task_DumpDiagnostics();

        HAL::Display_DumpCrashInfo("FXXK HardFault!");  
          
        while(digitalRead(CONFIG_ENCODER_PUSH_PIN) == HIGH)  
        {  
            Delay(100);  
        }  
          
        while(digitalRead(CONFIG_ENCODER_PUSH_PIN) == LOW)  
        {  
            Delay(100);  
        }  
          
        NVIC_SystemReset();  
    }
    
#if defined(__CC_ARM)
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
#elif defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
    __attribute__((naked)) void HardFault_Handler(void)
    {
        __asm volatile(
            "mov r0, lr\n"
            "mov r1, sp\n"
            "bl cm_backtrace_fault\n"
            "bl vApplicationHardFaultHook\n"
            "fault_loop:\n"
            "b fault_loop\n"
        );
    }
#endif
}
