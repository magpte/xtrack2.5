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
#include "PointContainer.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define POINT_CONTAINER_CHUNK_SIZE 64

PointContainer::PointContainer()
{
    memset(&priv, 0, sizeof(priv));
}

PointContainer::~PointContainer()
{

}

bool PointContainer::IsFlag(const DiffPoint_t* point)
{
    if (point->x == FLAG_END_POINT && point->y == FLAG_END_POINT)
    {
        return true;
    }

    if (point->x == FLAG_FULL_POINT && point->y == FLAG_FULL_POINT)
    {
        return true;
    }

    return false;
}

void PointContainer::PushPoint(const FullPoint_t* point)
{
    bool needNewChunk = vecChunks.empty() || (vecChunks.back().pointCount >= POINT_CONTAINER_CHUNK_SIZE);

    if (needNewChunk)
    {
        Chunk_t chunk;
        chunk.bbox.minX = point->x;
        chunk.bbox.maxX = point->x;
        chunk.bbox.minY = point->y;
        chunk.bbox.maxY = point->y;
        chunk.startPointIndex = (uint32_t)vecPoints.size();
        chunk.pointCount = 1;

        priv.curPushPoint = *point;
        PushFullPoint(point);
        vecChunks.push_back(chunk);
    }
    else
    {
        Chunk_t& curChunk = vecChunks.back();
        if (point->x < curChunk.bbox.minX) curChunk.bbox.minX = point->x;
        if (point->x > curChunk.bbox.maxX) curChunk.bbox.maxX = point->x;
        if (point->y < curChunk.bbox.minY) curChunk.bbox.minY = point->y;
        if (point->y > curChunk.bbox.maxY) curChunk.bbox.maxY = point->y;
        curChunk.pointCount++;

        int32_t diffX = point->x - priv.curPushPoint.x;
        int32_t diffY = point->y - priv.curPushPoint.y;
        priv.curPushPoint = *point;

        if (std::abs((int)diffX) > SCHAR_MAX || std::abs((int)diffY) > SCHAR_MAX)
        {
            PushFullPoint(point);
        }
        else
        {
            const DiffPoint_t diffPoint = { (int8_t)diffX, (int8_t)diffY };
            if (IsFlag(&diffPoint))
            {
                PushFullPoint(point);
            }
            else
            {
                vecPoints.push_back(diffPoint);
            }
        }
    }
}

void PointContainer::PushFullPoint(const FullPoint_t* point)
{
    vecPoints.push_back(MakeFlag(FLAG_FULL_POINT));
    const DiffPoint_t* pData = (const DiffPoint_t*)point;
    for (int i = 0; i < sizeof(FullPoint_t) / sizeof(DiffPoint_t); i++)
    {
        vecPoints.push_back(pData[i]);
    }
}

bool PointContainer::PopFullPoint(FullPoint_t* point)
{
    size_t size = vecPoints.size();
    if (size - priv.curPopIndex < 4)
    {
        return false;
    }

    DiffPoint_t* pData = (DiffPoint_t*)point;
    for (int i = 0; i < sizeof(FullPoint_t) / sizeof(DiffPoint_t); i++)
    {
        pData[i] = vecPoints[priv.curPopIndex];
        priv.curPopIndex++;
    }
    return true;
}

bool PointContainer::PopPoint(FullPoint_t* point)
{
    size_t size = vecPoints.size();

    if (priv.useFilter)
    {
        while (priv.curChunkIndex < vecChunks.size())
        {
            const Chunk_t& chunk = vecChunks[priv.curChunkIndex];

            if (priv.curPopIndex == chunk.startPointIndex)
            {
                bool intersects = !(chunk.bbox.maxX < priv.filterArea.minX ||
                                    chunk.bbox.minX > priv.filterArea.maxX ||
                                    chunk.bbox.maxY < priv.filterArea.minY ||
                                    chunk.bbox.minY > priv.filterArea.maxY);
                if (!intersects)
                {
                    uint32_t nextStart = (priv.curChunkIndex + 1 < vecChunks.size())
                                         ? vecChunks[priv.curChunkIndex + 1].startPointIndex
                                         : (uint32_t)size;
                    priv.curPopIndex = nextStart;
                    priv.curChunkIndex++;
                    continue;
                }
            }

            if (priv.curChunkIndex + 1 < vecChunks.size() &&
                priv.curPopIndex >= vecChunks[priv.curChunkIndex + 1].startPointIndex)
            {
                priv.curChunkIndex++;
                continue;
            }

            break;
        }
    }

    if (size - priv.curPopIndex == 0)
    {
        return false;
    }

    DiffPoint_t diffPoint = vecPoints[priv.curPopIndex];

    if (diffPoint.x == FLAG_FULL_POINT && diffPoint.y == FLAG_FULL_POINT)
    {
        priv.curPopIndex++;
        if (PopFullPoint(point))
        {
            priv.curPopPoint = *point;
        }
        else
        {
            return false;
        }
    }
    else
    {
        priv.curPopPoint.x += diffPoint.x;
        priv.curPopPoint.y += diffPoint.y;
        *point = priv.curPopPoint;
        priv.curPopIndex++;
    }

    return true;
}

void PointContainer::PopStart()
{
    priv.curPopIndex = 0;
    priv.curChunkIndex = 0;
    priv.useFilter = false;
}

void PointContainer::PopStartWithArea(const BBox_t* filterArea)
{
    priv.curPopIndex = 0;
    priv.curChunkIndex = 0;
    if (filterArea != nullptr)
    {
        priv.filterArea = *filterArea;
        priv.useFilter = true;
    }
    else
    {
        priv.useFilter = false;
    }
}
