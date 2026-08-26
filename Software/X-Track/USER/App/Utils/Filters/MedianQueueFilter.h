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
#ifndef __MEDIAN_QUEUE_FILTER_H
#define __MEDIAN_QUEUE_FILTER_H

#include "MedianFilter.h"

namespace Filter
{

template <typename T, size_t bufferSize> class MedianQueue : public Median<T, bufferSize>
{
public:
    MedianQueue() : Median<T, bufferSize>()
    {
    }

    virtual T GetNext(T value)
    {
        if (this->isFirst)
        {
            if (this->dataIndex < bufferSize)
            {
                this->buffer[this->dataIndex++] = value;
            }
            if (this->dataIndex >= bufferSize)
            {
                this->isFirst = false;
                for (size_t i = 0; i < bufferSize; i++)
                {
                    this->bufferSort[i] = this->buffer[i];
                }
                std::sort(this->bufferSort, this->bufferSort + bufferSize);
                this->dataIndex = 0;
            }
            this->lastValue = value;
        }
        else
        {
            T oldValue = this->buffer[this->dataIndex];
            this->buffer[this->dataIndex] = value;
            this->dataIndex = (this->dataIndex + 1) % bufferSize;

            // 在已排序数组中找到 oldValue 的位置
            size_t oldPos = 0;
            while (oldPos < bufferSize && this->bufferSort[oldPos] != oldValue)
            {
                oldPos++;
            }
            if (oldPos >= bufferSize)
            {
                oldPos = bufferSize - 1;
            }

            // 寻找新值 value 的插入位置
            size_t newPos = 0;
            while (newPos < bufferSize && this->bufferSort[newPos] < value)
            {
                newPos++;
            }

            if (oldPos < newPos)
            {
                for (size_t i = oldPos; i + 1 < newPos && i + 1 < bufferSize; i++)
                {
                    this->bufferSort[i] = this->bufferSort[i + 1];
                }
                size_t insertIdx = (newPos > 0) ? (newPos - 1) : 0;
                this->bufferSort[insertIdx] = value;
            }
            else if (oldPos > newPos)
            {
                for (size_t i = oldPos; i > newPos; i--)
                {
                    this->bufferSort[i] = this->bufferSort[i - 1];
                }
                this->bufferSort[newPos] = value;
            }
            else
            {
                this->bufferSort[oldPos] = value;
            }

            this->lastValue = this->bufferSort[bufferSize / 2];
        }

        return this->lastValue;
    }

protected:
    T bufferSort[bufferSize];
};

}

#endif
