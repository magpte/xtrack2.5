#include "GPS_Transform.h"
#include <stdbool.h>
#include <math.h>

#define ABS(x) (((x)>0)?(x):-(x))

static const double pi = 3.14159265358979324;
static const double a = 6378245.0;
static const double ee = 0.00669342162296594323;

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
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(ABS(x));
    ret += (20.0 * sin(6.0 * x * pi) + 20.0 * sin(2.0 * x * pi)) * 2.0 / 3.0;
    ret += (20.0 * sin(y * pi) + 40.0 * sin(y / 3.0 * pi)) * 2.0 / 3.0;
    ret += (160.0 * sin(y / 12.0 * pi) + 320 * sin(y * pi / 30.0)) * 2.0 / 3.0;
    return ret;
}

static double transformLon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(ABS(x));
    ret += (20.0 * sin(6.0 * x * pi) + 20.0 * sin(2.0 * x * pi)) * 2.0 / 3.0;
    ret += (20.0 * sin(x * pi) + 40.0 * sin(x / 3.0 * pi)) * 2.0 / 3.0;
    ret += (150.0 * sin(x / 12.0 * pi) + 300.0 * sin(x / 30.0 * pi)) * 2.0 / 3.0;
    return ret;
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
    double magic = sin(radLat);
    magic = 1 - ee * magic * magic;
    double sqrtMagic = sqrt(magic);
    dLat = (dLat * 180.0) / ((a * (1 - ee)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (a / sqrtMagic * cos(radLat) * pi);
    *mgLat = wgLat + dLat;
    *mgLon = wgLon + dLon;
};
