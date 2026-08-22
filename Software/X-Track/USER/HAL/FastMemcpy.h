/*
 * MIT License
 * Copyright (c) 2026 _VIFEXTech / X-Track
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __FAST_MEMCPY_H
#define __FAST_MEMCPY_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief 高性能 32 字节 LDMIA/STMIA 循环展开多寄存器快速内存复制 (ARM Cortex-M4 专用)
 * 针对 4 字节自然对齐的大块内存拷贝进行 8 寄存器突发读写优化，充分利用 AHB 突发传输与写缓冲。
 * 内置头部与尾部非对齐字节剥离，杜绝 UsageFault (UNALIGNED) 硬件异常。
 */
static inline void* arm_fast_memcpy(void* __restrict dst, const void* __restrict src, size_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;

    if (n < 16)
    {
        return memcpy(dst, src, n);
    }

    // 检查源与目标地址低 2 位对齐偏差是否一致
    uintptr_t d_align = (uintptr_t)d & 3;
    uintptr_t s_align = (uintptr_t)s & 3;

    if (d_align != s_align)
    {
        // 相对非对齐：安全回退到标准库 memcpy 避免 UsageFault
        return memcpy(dst, src, n);
    }

    // 先拷贝前导非 4 字节对齐的头部字节
    if (d_align)
    {
        size_t head = 4 - d_align;
        if (head > n) head = n;
        n -= head;
        while (head--)
        {
            *d++ = *s++;
        }
    }

    uint32_t* d32 = (uint32_t*)d;
    const uint32_t* s32 = (const uint32_t*)s;

    // 8 寄存器 32 字节 (8 x 32-bit Words) 突发批量复制 (循环展开)
    while (n >= 32)
    {
#if defined(__GNUC__) || defined(__ARMCC_VERSION)
        // GCC / ARM Clang 8 寄存器 LDMIA/STMIA 汇编流水线
        uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
        __asm__ volatile (
            "ldmia %[src]!, {%[r0], %[r1], %[r2], %[r3], %[r4], %[r5], %[r6], %[r7]}\n\t"
            "stmia %[dst]!, {%[r0], %[r1], %[r2], %[r3], %[r4], %[r5], %[r6], %[r7]}\n\t"
            : [dst] "+&r" (d32), [src] "+&r" (s32),
              [r0] "=&r" (r0), [r1] "=&r" (r1), [r2] "=&r" (r2), [r3] "=&r" (r3),
              [r4] "=&r" (r4), [r5] "=&r" (r5), [r6] "=&r" (r6), [r7] "=&r" (r7)
            :
            : "memory"
        );
#elif defined(__CC_ARM)
        // Keil AC5 编译器寄存器批量加载
        uint32_t a0 = s32[0], a1 = s32[1], a2 = s32[2], a3 = s32[3];
        uint32_t a4 = s32[4], a5 = s32[5], a6 = s32[6], a7 = s32[7];
        d32[0] = a0; d32[1] = a1; d32[2] = a2; d32[3] = a3;
        d32[4] = a4; d32[5] = a5; d32[6] = a6; d32[7] = a7;
        s32 += 8;
        d32 += 8;
#else
        d32[0] = s32[0]; d32[1] = s32[1]; d32[2] = s32[2]; d32[3] = s32[3];
        d32[4] = s32[4]; d32[5] = s32[5]; d32[6] = s32[6]; d32[7] = s32[7];
        s32 += 8;
        d32 += 8;
#endif
        n -= 32;
    }

    // 4 字节剩余 Word 拷贝
    while (n >= 4)
    {
        *d32++ = *s32++;
        n -= 4;
    }

    // 尾部 1~3 字节拷贝
    d = (uint8_t*)d32;
    s = (const uint8_t*)s32;
    while (n--)
    {
        *d++ = *s++;
    }

    return dst;
}

#endif // __FAST_MEMCPY_H
