/*
 * DebugLog.c
 * 见 DebugLog.h 顶部注释：这是没有串口时，用 Keil + ST-Link 的
 * Memory/Watch 窗口直接看 RAM 的调试日志实现。
 */
#include "DebugLog.h"
#include "delay.h"
#include "mcu_core.h"

volatile DebugLog_t g_DebugLog;

/**
  * @brief  写一条调试日志
  * @note   用短暂关中断的方式保证一条记录（seq/tick/tag/a/b 五个字段
  *         加上 writeIndex 自增）不会被另一个中断里的 DebugLog_Write
  *         调用打断到一半——即使目前项目里还没有在中断里调用它，这里
  *         也直接做对，免得以后往 ISR 里加日志时踩坑。关中断的窗口
  *         只有几条 store 指令，时间极短，不会影响其它中断的实时性。
  * @param  tag: DEBUG_TAG(...) 生成的 4 字符标签
  * @param  a, b: 自定义数据
  * @retval 无
  */
void DebugLog_Write(uint32_t tag, uint32_t a, uint32_t b)
{
    __disable_irq();

    uint32_t idx = g_DebugLog.writeIndex % DEBUG_LOG_CAPACITY;

    g_DebugLog.entries[idx].seq  = g_DebugLog.writeIndex;
    g_DebugLog.entries[idx].tick = millis();
    g_DebugLog.entries[idx].tag  = tag;
    g_DebugLog.entries[idx].a    = a;
    g_DebugLog.entries[idx].b    = b;

    __DMB();

    g_DebugLog.writeIndex++;

    __enable_irq();
}
