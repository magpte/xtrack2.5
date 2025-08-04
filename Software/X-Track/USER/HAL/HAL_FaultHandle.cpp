#include "HAL.h"
#include "App/Version.h"
#include "cm_backtrace/cm_backtrace.h"

#include "SdFat.h"  // ���SD��֧��  
  
extern SdFat SD;  // ����SD��ʵ��

static void Delay(uint32_t ms)
{
    volatile uint32_t i = F_CPU / 1000 * ms / 5;
    while(i--);
}

static void Reboot()
{
    while(digitalRead(CONFIG_ENCODER_PUSH_PIN) == HIGH)
    {
        Delay(1000);
    }
    NVIC_SystemReset();
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
      
    // ���������  
    Serial.print(printf_buff);  
      
    // д��crash.log�ļ�  
    static bool crash_file_opened = false;  
    static File crashFile;  
      
    if (!crash_file_opened && HAL::SD_GetReady()) {  
        crashFile = SD.open("/crash.log", FILE_WRITE);  
        if (crashFile) {  
            crash_file_opened = true;  
            // д��ʱ���ͷ��  
            HAL::Clock_Info_t clock;  
            HAL::Clock_GetInfo(&clock);  
            crashFile.printf("=== CRASH LOG %04d-%02d-%02d %02d:%02d:%02d ===\n",   
                           clock.year, clock.month, clock.day,   
                           clock.hour, clock.minute, clock.second);  
        }  
    }  
      
    if (crash_file_opened && crashFile) {  
        crashFile.print(printf_buff);  
        crashFile.flush(); // ����д��  
    }  
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
    // ȷ��crash.log�ļ�����ȷ�ر�  
    extern File crashFile;  
    if (crashFile) {  
        crashFile.close();  
    }  
      
    HAL::Display_DumpCrashInfo("FXXK HardFault!");  
    Reboot();  
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
