/*
 * MIT License
 * Copyright (c) 2018-2020 _VIFEXTech
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the follo18wing conditions:
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
#include "MillisTaskManager.h"
#include "Arduino.h"
#include <stdio.h>

#ifndef NULL
#   define NULL nullptr
#endif

#define TASK_NEW(task) do{task = new Task_t;}while(0)
#define TASK_DEL(task) do{delete task;}while(0)

/**
  * @brief  初始化任务列表
  * @param  priorityEnable:设定是否开启优先级
  * @retval 无
  */
MillisTaskManager::MillisTaskManager(bool priorityEnable)
{
    PriorityEnable = priorityEnable;
    Head = nullptr;
    Tail = nullptr;
}

/**
  * @brief  调度器析构，释放任务链表内存
  * @param  无
  * @retval 无
  */
MillisTaskManager::~MillisTaskManager()
{
    /*移动到链表头*/
    Task_t* now = Head;
    while(now != nullptr)
    {
        /*将当前节点缓存，等待删除*/
        Task_t* now_del = now;

        /*移动到下一个节点*/
        now = now->Next;

        /*删除当前节点内存*/
        TASK_DEL(now_del);
    }
    Head = nullptr;
    Tail = nullptr;
}

/**
  * @brief  往任务链表添加一个任务，设定间隔执行时间
  * @param  func:任务函数指针
  * @param  timeMs:周期时间设定(毫秒)
  * @param  state:任务开关
  * @param  name:任务名称标识
  * @retval 任务节点地址
  */
MillisTaskManager::Task_t* MillisTaskManager::Register(TaskFunction_t func, uint32_t timeMs, bool state, const char* name)
{
    /*寻找当前函数*/
    Task_t* task = Find(func);
    
    /*如果被注册*/
    if(task != nullptr)
    {
        /*更新信息*/
        task->Time = timeMs;
        task->State = state;
        if(name != nullptr)
        {
            task->Name = name;
        }
        return task;
    }

    /*为新任务申请内存*/
    TASK_NEW(task);

    /*是否申请成功*/
    if(task == nullptr)
    {
        return nullptr;
    }

    task->Name = name;            //任务名称
    task->Function = func;        //任务回调函数
    task->Time = timeMs;          //任务执行周期
    task->State = state;          //任务状态
    task->TimePrev = 0;           //上一次时间
    task->TimeCost = 0;           //时间开销
    task->MaxTimeCost = 0;        //最大时间开销
    task->TotalTimeCost = 0;      //总时间开销
    task->RunCount = 0;           //运行次数
    task->TimeError = 0;          //误差时间
    task->Next = nullptr;         //下一个节点
    
    /*如果任务链表为空*/
    if(Head == nullptr)
    {
        /*将当前任务作为链表的头*/
        Head = task;
    }
    else
    {
        /*从任务链表尾部添加任务*/
        Tail->Next = task;
    }
    
    /*将当前任务作为链表的尾*/
    Tail = task;
    return task;
}

/**
  * @brief  寻找任务,返回任务节点
  * @param  func:任务函数指针
  * @retval 任务节点地址
  */
MillisTaskManager::Task_t* MillisTaskManager::Find(TaskFunction_t func)
{
    Task_t* now = Head;
    while(now != nullptr)
    {
        if(now->Function == func)//判断函数地址是否相等
        {
            return now;
        }

        now = now->Next;//移动到下一个节点
    }
    return nullptr;
}

/**
  * @brief  获取当前节点的前一个节点
  * @param  task:当前任务节点地址
  * @retval 前一个任务节点地址
  */
MillisTaskManager::Task_t* MillisTaskManager::GetPrev(Task_t* task)
{
    Task_t* now = Head;    //当前节点
    Task_t* prev = nullptr;//前一节点
    
    /*开始遍历链表*/
    while(now != nullptr)
    {
        /*如果当前节点与被搜索的节点匹配*/
        if(now == task)
        {
            return prev;
        }
        
        /*当前节点保存为前一节点*/
        prev = now;
        
        /*节点后移*/
        now = now->Next;
    }
    return nullptr;
}

/**
  * @brief  注销任务（单次遍历完成解链与物理释放）
  * @param  func:任务函数指针
  * @retval true:成功 ; false:失败
  */
bool MillisTaskManager::Logout(TaskFunction_t func)
{
    Task_t* now = Head;
    Task_t* prev = nullptr;

    while(now != nullptr)
    {
        if(now->Function == func)
        {
            if(prev == nullptr)
            {
                Head = now->Next;
            }
            else
            {
                prev->Next = now->Next;
            }

            if(Tail == now)
            {
                Tail = prev;
            }

            TASK_DEL(now);
            return true;
        }
        prev = now;
        now = now->Next;
    }

    return false;
}

/**
  * @brief  任务状态控制
  * @param  func:任务函数指针
  * @param  state:任务状态
  * @retval true:成功 ; false:失败
  */
bool MillisTaskManager::SetState(TaskFunction_t func, bool state)
{
    Task_t* task = Find(func);
    if(task == nullptr)
        return false;

    task->State = state;
    return true;
}

/**
  * @brief  任务执行周期设置
  * @param  func:任务函数指针
  * @param  timeMs:任务执行周期
  * @retval true:成功 ; false:失败
  */
bool MillisTaskManager::SetIntervalTime(TaskFunction_t func, uint32_t timeMs)
{
    Task_t* task = Find(func);
    if(task == nullptr)
        return false;

    task->Time = timeMs;
    return true;
}

#if (MTM_USE_CPU_USAGE == 1)
static uint32_t UserFuncLoopUs = 0; //累计时间
/**
  * @brief  获取CPU占用率
  * @param  无
  * @retval CPU占用率，0~100%
  */
float MillisTaskManager::GetCPU_Usage()
{
    static uint32_t MtmStartUs;
    float usage = (float)UserFuncLoopUs / (micros() - MtmStartUs) * 100.0f;

    if(usage > 100.0f)
        usage = 100.0f;

    MtmStartUs = micros();
    UserFuncLoopUs = 0;
    return usage;
}
#endif

/**
  * @brief  获取任务单次耗费时间(us)
  * @param  func:任务函数指针
  * @retval 任务单次耗费时间(us)
  */
uint32_t MillisTaskManager::GetTimeCost(TaskFunction_t func)
{
    Task_t* task = Find(func);
    if(task == nullptr)
        return 0;

    return task->TimeCost;
}

/**
  * @brief  获取任务最大耗费时间(us)
  * @param  func:任务函数指针
  * @retval 任务最大耗费时间(us)
  */
uint32_t MillisTaskManager::GetMaxTimeCost(TaskFunction_t func)
{
    Task_t* task = Find(func);
    if(task == nullptr)
        return 0;

    return task->MaxTimeCost;
}

/**
  * @brief  导出所有任务的耗时统计信息到缓冲区
  * @param  buffer:输出目标缓冲区
  * @param  maxLen:缓冲区最大可写入字节数
  * @retval 实际写入字节数
  */
size_t MillisTaskManager::DumpTaskStats(char* buffer, size_t maxLen)
{
    if (buffer == nullptr || maxLen == 0) return 0;

    size_t offset = 0;
    int len = snprintf(buffer + offset, maxLen - offset, "\n=== [Task Timing Stats] ===\n");
    if (len > 0) offset += (size_t)len;

    Task_t* now = Head;
    while (now != nullptr && offset < maxLen)
    {
        uint32_t avgUs = (now->RunCount > 0) ? (now->TotalTimeCost / now->RunCount) : 0;
        const char* taskName = (now->Name != nullptr) ? now->Name : "UnnamedTask";

        len = snprintf(buffer + offset, maxLen - offset,
                       "  * %-20s | Last: %6lu us | Max: %6lu us | Avg: %6lu us | Runs: %lu\n",
                       taskName,
                       (unsigned long)now->TimeCost,
                       (unsigned long)now->MaxTimeCost,
                       (unsigned long)avgUs,
                       (unsigned long)now->RunCount);
        if (len > 0) offset += (size_t)len;
        now = now->Next;
    }
    return offset;
}

/**
  * @brief  调度器(内核)
  * @param  tick:提供一个精确到毫秒的系统时钟变量
  * @retval 无
  */
void MillisTaskManager::Running(uint32_t tick)
{
    Task_t* now = Head;
    while(now != nullptr)
    {
        /*预先保存下一个节点指针，防御任务回调内部自注销导致的 Use-After-Free*/
        Task_t* next = now->Next;

        if(now->Function != nullptr && now->State)
        {
            uint32_t elapsTime = GetTickElaps(tick, now->TimePrev);
            if(elapsTime >= now->Time)
            {
                /*获取时间误差，误差越大实时性越差*/
                now->TimeError = elapsTime - now->Time;
                
                /*消除累积相位误差：若延时超过2个周期或初始化，重置基准时间；否则平滑累加周期*/
                if (elapsTime >= (now->Time << 1) || now->TimePrev == 0)
                {
                    now->TimePrev = tick;
                }
                else
                {
                    now->TimePrev += now->Time;
                }

#if (MTM_USE_TIMING_STATS == 1)
                /*测量单次任务运行时间(us)与统计信息*/
                uint32_t startUs = micros();
                now->Function();
                uint32_t endUs = micros();

                /* 防御性校验：仅当时间单调递增且耗时在合理区间内时才纳入统计 */
                if (endUs >= startUs)
                {
                    uint32_t timeCost = endUs - startUs;
                    if (timeCost < 60000000UL) // 过滤 >60s 的异常下溢值
                    {
                        now->TimeCost = timeCost;
                        if (timeCost > now->MaxTimeCost)
                        {
                            now->MaxTimeCost = timeCost;
                        }
                        now->TotalTimeCost += timeCost;
                        now->RunCount++;

#if (MTM_USE_CPU_USAGE == 1)
                        UserFuncLoopUs += timeCost;
#endif
                    }
                }
#else
                now->Function();
#endif

                /*判断是否开启优先级*/
                if(PriorityEnable)
                {
                    break;
                }
            }
        }

        /*移动到下一个节点*/
        now = next;
    }
}
