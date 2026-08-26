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
#include "AppFactory.h"
#include "Config/Config.h"

#if CONFIG_PAGE_TEMPLATE_ENABLE
#include "_Template/Template.h"
#endif

#include "LiveMap/LiveMap.h"
#include "Dialplate/Dialplate.h"
#include "SystemInfos/SystemInfos.h"
#include "Startup/Startup.h"
#include <string.h>

PageBase* AppFactory::CreatePage(const char* name)
{
    static Page::Startup     pageStartup;
    static Page::Dialplate   pageDialplate;
    static Page::LiveMap     pageLiveMap;
    static Page::SystemInfos pageSystemInfos;

#if CONFIG_PAGE_TEMPLATE_ENABLE
    static Page::Template    pageTemplate;
    if (strcmp(name, "Template") == 0)    return &pageTemplate;
#endif

    if (strcmp(name, "Startup") == 0)     return &pageStartup;
    if (strcmp(name, "Dialplate") == 0)   return &pageDialplate;
    if (strcmp(name, "LiveMap") == 0)     return &pageLiveMap;
    if (strcmp(name, "SystemInfos") == 0) return &pageSystemInfos;

    return nullptr;
}

