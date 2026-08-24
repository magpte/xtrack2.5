/*
 * MIT License
 * Copyright (c) 2021 _VIFEXTech
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

#if defined(ARDUINO) || defined(NDEBUG)

#include "lvgl/lvgl.h"
#include <new>
#include <stdlib.h>
#include <string.h>

typedef void* (*alloc_func_t)(size_t);

static void* first_alloc(size_t size);

static alloc_func_t alloc_func = first_alloc;

static void* first_alloc(size_t size)
{
    if(!lv_is_initialized())
    {
        lv_init();
    }

    alloc_func = lv_mem_alloc;
    return lv_mem_alloc(size);
}

void *operator new(size_t size)
{
    return alloc_func(size);
}

void *operator new[](size_t size)
{
    return alloc_func(size);
}

void *operator new(size_t size, const std::nothrow_t&) noexcept
{
    return alloc_func(size);
}

void *operator new[](size_t size, const std::nothrow_t&) noexcept
{
    return alloc_func(size);
}

void operator delete(void* ptr) noexcept
{
    if(ptr) lv_mem_free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    if(ptr) lv_mem_free(ptr);
}

void operator delete(void* ptr, size_t) noexcept
{
    if(ptr) lv_mem_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
    if(ptr) lv_mem_free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    if(ptr) lv_mem_free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    if(ptr) lv_mem_free(ptr);
}

extern "C" {
void* malloc(size_t size)
{
    return alloc_func(size);
}

void free(void* ptr)
{
    if(ptr) lv_mem_free(ptr);
}

void* realloc(void* ptr, size_t size)
{
    if(!ptr) return alloc_func(size);
    if(!size)
    {
        lv_mem_free(ptr);
        return nullptr;
    }
    if(!lv_is_initialized())
    {
        lv_init();
    }
    return lv_mem_realloc(ptr, size);
}

void* calloc(size_t num, size_t size)
{
    size_t total = num * size;
    void* ptr = alloc_func(total);
    if(ptr)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}
}

#endif

