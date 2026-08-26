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
#ifndef __LOWPASS_FILTER_H
#define __LOWPASS_FILTER_H

#include "FilterBase.h"

namespace Filter
{

template <typename T> class Lowpass : public Base<T>
{
public:
    Lowpass(float dt, float cutoff)
    {
        this->Reset();

        this->dT = dt;
        if (cutoff > 0.001f)
        {
            float RC = 1.0f / (2.0f * 3.1415926535f * cutoff);
            this->rc = dt / (RC + dt);
        }
        else
        {
            this->rc = 1.0f;
        }
    }

    virtual T GetNext(T value)
    {
        if (this->CheckFirst())
        {
            return this->lastValue = value;
        }
        else
        {
            this->lastValue = (T)(this->lastValue + (value - this->lastValue) * this->rc);
            return this->lastValue;
        }
    }

private:
    float dT;
    float rc;
};

}

#endif
