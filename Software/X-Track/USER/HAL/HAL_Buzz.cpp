#include "HAL.h"

static bool IsEnable = true;

// 调试用：在这里直接拦一道，不管别处（比如 DP_SysConfig.cpp 加载
// sysConfig.soundEnable 时调用的 Buzz_SetEnable(true)）后面又把
// IsEnable 改回 true，只要这个宏是 1，Buzz_Tone() 一律直接返回。
// 排查完记得把下面这个宏改回 0，或者把这几行删掉。
#define DEBUG_HARD_MUTE_BUZZER  1

void HAL::Buzz_init()
{
    pinMode(CONFIG_BUZZ_PIN, OUTPUT);
}

void HAL::Buzz_SetEnable(bool en)
{
    if(!en)
    {
        Buzz_Tone(0);
    }

    IsEnable = en;
}

void HAL::Buzz_Tone(uint32_t freq, int32_t duration)
{
#if DEBUG_HARD_MUTE_BUZZER
    (void)freq;
    (void)duration;
#else
    if(!IsEnable)
    {
        return;
    }

    if(duration >= 0)
    {
        tone(CONFIG_BUZZ_PIN, freq, duration);
    }
    else
    {
        tone(CONFIG_BUZZ_PIN, freq);
    }
#endif
}
