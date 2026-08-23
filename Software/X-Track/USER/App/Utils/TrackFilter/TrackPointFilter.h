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
#ifndef __TRACK_POINT_FILTER_H
#define __TRACK_POINT_FILTER_H

#include <stdint.h>

class TrackPointFilter
{
public:
    typedef struct
    {
        int32_t x;
        int32_t y;
    } Point_t;

    typedef void (*Callback_t)(TrackPointFilter* filter, const Point_t* point);

public:
    void* userData;

public:
    TrackPointFilter();
    ~TrackPointFilter();
    void Reset();
    bool PushPoint(int32_t x, int32_t y)
    {
        Point_t point = { x, y };
        return PushPoint(&point);
    }
    bool PushPoint(double x, double y)
    {
        Point_t point = { (int32_t)x, (int32_t)y };
        return PushPoint(&point);
    }
    bool PushPoint(const Point_t* point);
    void PushEnd();
    void SetOffsetThreshold(int32_t offset);
    void SetOffsetThreshold(double offset)
    {
        SetOffsetThreshold((int32_t)(offset + 0.5));
    }
    void SetOutputPointCallback(Callback_t callback);
    void SetSecondFilterModeEnable(bool en);
    void GetCounts(uint32_t* sum, uint32_t* output)
    {
        *sum = priv.pointCnt;
        *output = priv.pointOutputCnt;
    }

private:
    struct
    {
        int64_t offsetThresholdSq;
        Callback_t outputCallback;
        Point_t refPoint;   // 当前基线起点
        Point_t tailPoint;  // 上上个点 P0
        Point_t prePoint;   // 上一个点 P1
        uint32_t pointCnt;
        uint32_t pointOutputCnt;
        bool secondFilterMode;
    } priv;

private:
    void OutputPoint(const Point_t* point);
};

#endif
