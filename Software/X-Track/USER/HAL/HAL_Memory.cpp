#include "HAL.h"
#include <string.h>

#if CONFIG_SHOW_STACK_INFO

#include "StackInfo/StackInfo.h"

static void Memory_FormatStackInfo(char* buf, uint32_t len)
{
    snprintf(
        buf, len,
        "%0.1f%% used (free 0x%x)",
        StackInfo_GetMaxUtilization() * 100,
        StackInfo_GetMinFreeSize()
    );
}

static void Memory_ShowStackInfo()
{
    char buf[64];
    Memory_FormatStackInfo(buf, sizeof(buf));
    Serial.print("Stack: ");
    Serial.println(buf);
}

#endif

#if CONFIG_SHOW_HEAP_INFO

#include <stdarg.h>
#include <stdlib.h>

// __heapstats 会按堆里的每一段内存区域分别回调一次，不是只调一次给个总数。
// 这里把每次回调打印的内容都追加到同一个缓冲区里（用 "; " 隔开），而不是
// 尝试去解析出一个具体的"剩余字节数"数字——ARM 运行时库这个内部格式没有
// 公开文档，与其自己猜着解析、可能解析错，不如把它原样吐出来，反正是给
// 开发者自己看的诊断信息，不追求排版好看。
static char*  s_heapBuf;
static uint32_t s_heapBufLen;
static uint32_t s_heapBufUsed;

static int Memory_HeapCollect(void* param, char const* format, ...)
{
    char line[64];

    va_list args;
    va_start(args, format);
    int ret_status = vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    if (s_heapBuf != NULL && s_heapBufUsed < s_heapBufLen - 1)
    {
        int written = snprintf(
            s_heapBuf + s_heapBufUsed,
            s_heapBufLen - s_heapBufUsed,
            s_heapBufUsed == 0 ? "%s" : "; %s",
            line
        );
        if (written > 0)
        {
            s_heapBufUsed += written;
        }
    }

    return ret_status;
}

static void Memory_FormatHeapInfo(char* buf, uint32_t len)
{
    s_heapBuf = buf;
    s_heapBufLen = len;
    s_heapBufUsed = 0;
    buf[0] = '\0';

    int size = 0;
    __heapstats((__heapprt)Memory_HeapCollect, &size);

    s_heapBuf = NULL;
}

static void Memory_ShowHeapInfo()
{
    char buf[128];
    Memory_FormatHeapInfo(buf, sizeof(buf));
    Serial.print("Heap: ");
    Serial.println(buf);
}

#endif

void HAL::Memory_DumpInfo()
{
#if CONFIG_SHOW_STACK_INFO
    Memory_ShowStackInfo();
#endif

#if CONFIG_SHOW_HEAP_INFO
    Memory_ShowHeapInfo();
#endif
}

void HAL::Memory_GetStackInfo(char* buf, uint32_t len)
{
#if CONFIG_SHOW_STACK_INFO
    Memory_FormatStackInfo(buf, len);
#else
    // 开关没打开时给个明确的提示，而不是留空看起来像出错了——
    // 见 HAL_Config.h 里的 CONFIG_SHOW_STACK_INFO。
    snprintf(buf, len, "(disabled)");
#endif
}

void HAL::Memory_GetHeapInfo(char* buf, uint32_t len)
{
#if CONFIG_SHOW_HEAP_INFO
    Memory_FormatHeapInfo(buf, len);
#else
    snprintf(buf, len, "(disabled)");
#endif
}
