#include "HAL.h"

#if CONFIG_SHOW_STACK_INFO

#include "StackInfo/StackInfo.h"

static void Memory_ShowStackInfo()
{
    Serial.print("Stack: ");
    Serial.print(StackInfo_GetMaxUtilization() * 100);
    Serial.print("% used (free 0x");
    Serial.print(StackInfo_GetMinFreeSize(), HEX);
    Serial.println(")");
}

#endif

#if CONFIG_SHOW_HEAP_INFO

#include <stdarg.h>

static int Memory_HeapPrint(void* param, char const* format, ...)
{
    char buf[64];

    va_list args;
    va_start(args, format);
    int ret_status = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    Serial.print("Heap: ");
    Serial.println(buf);

    return ret_status;
}

static void Memory_ShowHeapInfo()
{
    int size = 0;
    __heapstats((__heapprt)Memory_HeapPrint, &size);
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
