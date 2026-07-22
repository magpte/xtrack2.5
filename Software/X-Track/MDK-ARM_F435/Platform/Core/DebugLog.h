/*
 * DebugLog.h
 *
 * 没有可用串口时的调试手段：把最近发生的事件写进一个固定地址的环形
 * 缓冲区（g_DebugLog），用 Keil 自带的 Memory/Watch 窗口通过 ST-Link
 * 的 SWD 直接读这块 RAM——这个读取是"背景/非侵入式"的，不需要停机
 * (halt) target，甚至可以开着 Periodic Window Update 一边跑一边看，
 * 相当于一个不占用任何串口引脚的"伪串口日志"。
 *
 * 用法（在任意 .c/.cpp 里）：
 *
 *     #include "DebugLog.h"
 *     DEBUG_LOG(DEBUG_TAG('S','D','r','x'), length, (uint32_t)rxBuf);
 *
 * Keil 里怎么看：
 *   1. Debug 会话里打开 View -> Watch Windows -> Memory 1，
 *      地址栏输入 g_DebugLog，就能看到整块结构体的十六进制内容；
 *      或者用 Watch 1 窗口直接加 g_DebugLog 这个符号，能看到
 *      结构化的成员（entries[0].tag 等），比 Memory 窗口好读。
 *   2. 工具栏上那个"Periodic Window Update"按钮（时钟图标）打开后，
 *      target 全速跑的时候窗口内容也会持续刷新，不需要每次都手动
 *      暂停/继续。
 *   3. g_DebugLog.writeIndex 是从开机到现在总共写了多少条（一直往上
 *      涨，不回绕），最新一条在 entries[(writeIndex - 1) % 64]；
 *      如果 writeIndex 不再变化了，说明卡在了那条日志之后的地方，
 *      看那条日志的 tag/a/b 就知道卡在哪一步。
 *   4. tag 是 4 个字符压成的 uint32——在 Watch 窗口里如果显示成十进制
 *      不好认，把该项的显示格式改成 Char（右键 -> Number Base -> ...
 *      或者直接展开看 hex，按 ASCII 表换算，例如 0x53447278 就是
 *      "SDrx"）。
 */
#ifndef __DEBUG_LOG_H
#define __DEBUG_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEBUG_LOG_CAPACITY 64

/* 4 个字符压成一个 uint32，方便在 Watch 窗口里一眼看出这条日志是
 * 哪里打的，不用来回对照一份"日志编号表" */
#define DEBUG_TAG(c0, c1, c2, c3) \
    ( ((uint32_t)(uint8_t)(c0) << 24) | \
      ((uint32_t)(uint8_t)(c1) << 16) | \
      ((uint32_t)(uint8_t)(c2) << 8)  | \
       (uint32_t)(uint8_t)(c3) )

typedef struct
{
    uint32_t seq;   /* 全局递增序号，等于写入时的 writeIndex */
    uint32_t tick;  /* millis() 时间戳 */
    uint32_t tag;   /* DEBUG_TAG(...) 生成的 4 字符标签 */
    uint32_t a;     /* 自定义数据 1，具体含义由调用处的 tag 决定 */
    uint32_t b;     /* 自定义数据 2 */
} DebugLogEntry_t;

typedef struct
{
    volatile uint32_t writeIndex;  /* 从未回绕，真实总条数；下标用 %CAPACITY 取 */
    DebugLogEntry_t entries[DEBUG_LOG_CAPACITY];
} DebugLog_t;

/* 全局符号，直接在 Keil Watch/Memory 窗口里填 g_DebugLog 就能看到 */
extern volatile DebugLog_t g_DebugLog;

void DebugLog_Write(uint32_t tag, uint32_t a, uint32_t b);

#define DEBUG_LOG(tag, a, b) DebugLog_Write((tag), (uint32_t)(a), (uint32_t)(b))

#ifdef __cplusplus
}
#endif

#endif /* __DEBUG_LOG_H */
