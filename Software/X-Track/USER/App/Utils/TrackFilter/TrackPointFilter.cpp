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
#include "TrackPointFilter.h"
#include <string.h>

TrackPointFilter::TrackPointFilter()
{
    memset(&priv, 0, sizeof(priv));
    priv.offsetThresholdSq = 4; // 默认 2 像素阈值 (2^2 = 4)
}

TrackPointFilter::~TrackPointFilter()
{

}

void TrackPointFilter::Reset()
{
    priv.pointCnt = 0;
    priv.pointOutputCnt = 0;
}

void TrackPointFilter::SetOffsetThreshold(int32_t offset)
{
    if (offset <= 0)
    {
        offset = 1;
    }
    priv.offsetThresholdSq = (int64_t)offset * offset;
}

void TrackPointFilter::SetOutputPointCallback(Callback_t callback)
{
    priv.outputCallback = callback;
}

void TrackPointFilter::SetSecondFilterModeEnable(bool en)
{
    priv.secondFilterMode = en;
}

void TrackPointFilter::OutputPoint(const Point_t* point)
{
    if (priv.outputCallback)
    {
        priv.outputCallback(this, point);
    }
    priv.pointOutputCnt++;
}

bool TrackPointFilter::PushPoint(const Point_t* point)
{
    bool retval = false;

    if (priv.pointCnt == 0)
    {
        // 第 1 个点：直接作为起点输出
        retval = true;
        OutputPoint(point);
        priv.refPoint = *point;
    }
    else if (priv.pointCnt == 1)
    {
        // 第 2 个点：与起点形成第一条参考基线
        // 暂不输出，等待第 3 个点判定
    }
    else
    {
        // 空间几何判定：当前点 P2 (point), 上一点 P1 (priv.prePoint), 上上点 P0 (priv.tailPoint), 基线起点 Pref (priv.refPoint)
        int64_t dx = (int64_t)priv.prePoint.x - priv.refPoint.x;
        int64_t dy = (int64_t)priv.prePoint.y - priv.refPoint.y;
        int64_t baseLenSq = dx * dx + dy * dy;

        // 条件 A：垂直偏距检测 (Cross-Track Distance)
        // 向量叉积 cross = (P1 - Pref) x (P2 - Pref)
        int64_t vx = (int64_t)point->x - priv.refPoint.x;
        int64_t vy = (int64_t)point->y - priv.refPoint.y;
        int64_t cross = dx * vy - dy * vx;
        int64_t crossSq = cross * cross;

        if (baseLenSq > 0 && crossSq > priv.offsetThresholdSq * baseLenSq)
        {
            // P2 偏离基线 Pref->P1 超过阈值，判定 P1 为转向拐点
            retval = true;
            if (priv.secondFilterMode)
            {
                OutputPoint(&priv.tailPoint);
            }
            OutputPoint(&priv.prePoint);
            priv.refPoint = priv.prePoint; // 以 P1 为新的基线起点
        }
        else
        {
            // 条件 B：法线转折判别 (运动向量点积是否反向 / 夹角 > 90 度)
            // 向量 u = P1 - P0, 向量 w = P2 - P1
            int64_t ux = (int64_t)priv.prePoint.x - priv.tailPoint.x;
            int64_t uy = (int64_t)priv.prePoint.y - priv.tailPoint.y;
            int64_t wx = (int64_t)point->x - priv.prePoint.x;
            int64_t wy = (int64_t)point->y - priv.prePoint.y;
            int64_t dot = ux * wx + uy * wy;

            if ((ux != 0 || uy != 0) && (wx != 0 || wy != 0) && dot < 0)
            {
                // 运动方向越过垂线发生锐角转折，判定 P1 为特征转折点
                retval = true;
                if (priv.secondFilterMode)
                {
                    OutputPoint(&priv.tailPoint);
                }
                OutputPoint(&priv.prePoint);
                priv.refPoint = priv.prePoint; // 以 P1 为新的基线起点
            }
        }
    }

    priv.tailPoint = priv.prePoint;
    priv.prePoint = *point;
    priv.pointCnt++;

    return retval;
}

void TrackPointFilter::PushEnd()
{
    if (priv.pointCnt > 0)
    {
        OutputPoint(&priv.prePoint);
    }
    Reset();
}
