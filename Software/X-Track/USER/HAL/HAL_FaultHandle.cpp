#include "HAL.h"
#include "App/Version.h"
#include "cm_backtrace/cm_backtrace.h"
// 注意：SdFat.h 已从本文件移除（原由 SD_WriteCrashLog 调用引入，
// 该函数声明已在 HAL.h 中，无需在此重复包含）。

/*
 * P4-H Fix: cmb_printf() 运行在 HardFault ISR 上下文中。
 * 原实现每次调用都执行 HAL::SD_WriteCrashLog() → SD.open()，
 * 而 SD.open() 内部可能触发堆操作（new/malloc）——若堆本身已因
 * 崩溃损坏，此处会触发二次 fault 或死锁，导致崩溃日志完全丢失。
 *
 * 修复策略：
 *   1. cmb_printf() 仅做纯内存操作：将格式化输出追加到预分配
 *      静态缓冲区 s_crashBuf（2KB，BSS 段，不依赖堆）。
 *   2. cm_backtrace_fault() 返回后（所有回溯已完成），在
 *      vApplicationHardFaultHook() 中一次性调用
 *      HAL::SD_WriteCrashLog(s_crashBuf) 写入 SD 卡。
 *      单次 SD 写比 N 次 open/write/close 安全得多，且即使失败
 *      也不会阻止后续的屏幕显示和按键等待流程。
 */
#define CRASH_LOG_BUF_SIZE  2048
static char     s_crashBuf[CRASH_LOG_BUF_SIZE];
static uint32_t s_crashBufLen = 0;

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
    int n = vsnprintf(printf_buff, sizeof(printf_buff), __format, args);
    va_end(args);

    // 输出至串口（轮询阻塞式，HardFault 上下文中安全）
    Serial.print(printf_buff);

    // 纯内存追加到静态缓冲区，不在此处调用任何 SD/堆操作
    if (n > 0)
    {
        uint32_t copyLen = (uint32_t)n;
        if (s_crashBufLen + copyLen >= CRASH_LOG_BUF_SIZE - 1)
        {
            copyLen = (s_crashBufLen < CRASH_LOG_BUF_SIZE - 1)
                    ? (CRASH_LOG_BUF_SIZE - 1 - s_crashBufLen) : 0;
        }
        if (copyLen > 0)
        {
            memcpy(s_crashBuf + s_crashBufLen, printf_buff, copyLen);
            s_crashBufLen += copyLen;
            s_crashBuf[s_crashBufLen] = '\0';
        }
    }
}

extern "C"
{
    __attribute__((used)) void vApplicationHardFaultHook()
    {
        // 1. 输出任务调度统计（通过串口轮询，安全）
        HAL::Task_DumpDiagnostics();

        // 2. 在屏幕上显示崩溃信息
        HAL::Display_DumpCrashInfo("FXXK HardFault!");

        // 3. cm_backtrace_fault() 已完成，s_crashBuf 积累了所有回溯文本，
        //    现在一次性写入 SD 卡。若 SD 卡不可用或写失败，安静返回，
        //    不影响后续等待按键与复位流程。
        if (s_crashBufLen > 0)
        {
            HAL::SD_WriteCrashLog(s_crashBuf);
        }

        // 4. 等待用户按下编码器按键后执行软复位
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
    /* naked 函数：编译器不生成 prologue/epilogue，sp 即为进入 HardFault
     * 时的 MSP，直接作为 r1 传给 cm_backtrace_fault，符合其调用约定。 */
    __attribute__((used, naked)) void HardFault_Handler(void)
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
