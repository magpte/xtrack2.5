#include "GPS_Transform.h"
#include <stdbool.h>
#include <math.h>

#ifndef sqrtf
#  define sqrtf(x) sqrt((double)(x))
#endif
#ifndef sinf
#  define sinf(x) sin((double)(x))
#endif
#ifndef cosf
#  define cosf(x) cos((double)(x))
#endif

#define FAST_SIN(x) sinf((float)(x))
#define FAST_COS(x) cosf((float)(x))
#define FAST_SQRT(x) sqrtf((float)(x))

#define ABS(x) (((x)>0)?(x):-(x))

static const double pi = 3.14159265358979324;
static const double a = 6378245.0;
static const double ee = 0.00669342162296594323;

static const float pi_f = 3.14159265358979324f;

// GCJ-02（"火星坐标系"）偏移是中国大陆法规要求的坐标混淆算法，公式本身
// 只在中国境内的范围内标定过、只在这个范围内有意义——用户反馈"在海外用
// Live Map 有偏移"，根因就是这里原来完全没做"是否在中国境内"的判断，
// 不管坐标在哪儿都套用这个公式，境外经纬度代入之后算出来的偏移量是没
// 意义的乱飘。这里补上主流实现（高德/百度 SDK 一样的做法）都会做的
// 矩形粗略判断：经纬度落在这个范围外，直接判定"不在中国"，原样返回，
// 不做偏移。范围本身是业界通用的近似值，没有单独收窄/放宽过。
static bool outOfChina(double lat, double lon)
{
    return (lon < 72.004 || lon > 137.8347 ||
            lat < 0.8293 || lat > 55.8271);
}

static double transformLat(double x, double y)
{
    float fx = (float)x;
    float fy = (float)y;
    float ret = -100.0f + 2.0f * fx + 3.0f * fy + 0.2f * fy * fy + 0.1f * fx * fy + 0.2f * FAST_SQRT(ABS(fx));
    ret += (20.0f * FAST_SIN(6.0f * fx * pi_f) + 20.0f * FAST_SIN(2.0f * fx * pi_f)) * 2.0f / 3.0f;
    ret += (20.0f * FAST_SIN(fy * pi_f) + 40.0f * FAST_SIN(fy / 3.0f * pi_f)) * 2.0f / 3.0f;
    ret += (160.0f * FAST_SIN(fy / 12.0f * pi_f) + 320.0f * FAST_SIN(fy * pi_f / 30.0f)) * 2.0f / 3.0f;
    return (double)ret;
}

static double transformLon(double x, double y)
{
    float fx = (float)x;
    float fy = (float)y;
    float ret = 300.0f + fx + 2.0f * fy + 0.1f * fx * fx + 0.1f * fx * fy + 0.1f * FAST_SQRT(ABS(fx));
    ret += (20.0f * FAST_SIN(6.0f * fx * pi_f) + 20.0f * FAST_SIN(2.0f * fx * pi_f)) * 2.0f / 3.0f;
    ret += (20.0f * FAST_SIN(fx * pi_f) + 40.0f * FAST_SIN(fx / 3.0f * pi_f)) * 2.0f / 3.0f;
    ret += (150.0f * FAST_SIN(fx / 12.0f * pi_f) + 300.0f * FAST_SIN(fx / 30.0f * pi_f)) * 2.0f / 3.0f;
    return (double)ret;
}

void GPS_Transform(double wgLat, double wgLon, double* mgLat, double* mgLon)
{
    if (outOfChina(wgLat, wgLon))
    {
        *mgLat = wgLat;
        *mgLon = wgLon;
        return;
    }

    double dLat = transformLat(wgLon - 105.0, wgLat - 35.0);
    double dLon = transformLon(wgLon - 105.0, wgLat - 35.0);
    double radLat = wgLat / 180.0 * pi;
    double magic = FAST_SIN(radLat);
    magic = 1.0 - ee * magic * magic;
    double sqrtMagic = FAST_SQRT(magic);
    dLat = (dLat * 180.0) / ((a * (1.0 - ee)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (a / sqrtMagic * FAST_COS(radLat) * pi);
    *mgLat = wgLat + dLat;
    *mgLon = wgLon + dLon;
};

