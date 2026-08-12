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
#ifndef __POINT_CONTAINER_H
#define __POINT_CONTAINER_H

#include <stdint.h>
#include <vector>

class PointContainer
{
public:
    typedef struct
    {
        int32_t minX;
        int32_t minY;
        int32_t maxX;
        int32_t maxY;
    } BBox_t;

    typedef struct
    {
        BBox_t bbox;
        uint32_t startPointIndex;
        uint32_t pointCount;
    } Chunk_t;

    PointContainer();
    ~PointContainer();
    void PushPoint(int32_t x, int32_t y)
    {
        const FullPoint_t point = { x, y };
        PushPoint(&point);
    }

    bool PopPoint(int32_t* x, int32_t* y)
    {
        FullPoint_t point;
        bool retval = PopPoint(&point);

        if (retval)
        {
            *x = point.x;
            *y = point.y;
        }
        return retval;
    }

    void PopStart();
    void PopStartWithArea(const BBox_t* filterArea);

private:
    typedef enum
    {
        FLAG_END_POINT = 0,
        FLAG_FULL_POINT = 1,
    } Flag_t;

    typedef struct
    {
        int32_t x;
        int32_t y;
    } FullPoint_t;

    typedef struct
    {
        int8_t x;
        int8_t y;
    } DiffPoint_t;

private:
    void PushPoint(const FullPoint_t* point);
    bool PopPoint(FullPoint_t* point);
    void PushFullPoint(const FullPoint_t* point);
    bool PopFullPoint(FullPoint_t* point);
    bool IsFlag(const DiffPoint_t* point);
    inline DiffPoint_t MakeFlag(Flag_t flag)
    {
        DiffPoint_t point = { (int8_t)flag, (int8_t)flag };
        return point;
    }

private:
    std::vector<DiffPoint_t> vecPoints;
    std::vector<Chunk_t> vecChunks;
    struct
    {
        FullPoint_t curPushPoint;
        FullPoint_t curPopPoint;
        uint32_t curPopIndex;
        uint32_t curChunkIndex;
        BBox_t filterArea;
        bool useFilter;
    } priv;
};

#endif
