#include "TileSystem.h"

#include <algorithm>
#include <math.h>
#include <string.h>

#define FAST_SIN(x) sinf(x)
#define FAST_COS(x) cosf(x)

using namespace Microsoft_MapPoint;

static const float EarthRadius_f = 6378137.0f;
static const float MinLatitude_f = -85.05112878f;
static const float MaxLatitude_f = 85.05112878f;
static const float MATH_PI_f = 3.14159265358979323846f;

/// <summary>  
/// Clips a number to the specified minimum and maximum values (branch-free/inline single-precision).  
/// </summary>  
static inline float Clip_f(float n, float minValue, float maxValue)
{
    return (n < minValue) ? minValue : ((n > maxValue) ? maxValue : n);
}

uint32_t TileSystem::MapSize(int levelOfDetail)
{
    return (uint32_t)256 << levelOfDetail;
}

double TileSystem::GroundResolution(double latitude, int levelOfDetail)
{
    float lat = Clip_f((float)latitude, MinLatitude_f, MaxLatitude_f);
    float res = FAST_COS(lat * (MATH_PI_f / 180.0f)) * 2.0f * MATH_PI_f * EarthRadius_f / (float)MapSize(levelOfDetail);
    return (double)res;
}
 
double TileSystem::MapScale(double latitude, int levelOfDetail, int screenDpi)
{
    return GroundResolution(latitude, levelOfDetail) * (double)screenDpi / 0.0254;
}

void TileSystem::LatLongToPixelXY(double latitude, double longitude, int levelOfDetail, int* pixelX, int* pixelY)
{
    double lat = latitude < -85.05112878 ? -85.05112878 : (latitude > 85.05112878 ? 85.05112878 : latitude);
    double lon = longitude < -180.0 ? -180.0 : (longitude > 180.0 ? 180.0 : longitude);

    double x = (lon + 180.0) / 360.0;
    double sinLatitude = sin(lat * (3.14159265358979323846 / 180.0));
    if (sinLatitude < -0.9999) sinLatitude = -0.9999;
    if (sinLatitude > 0.9999) sinLatitude = 0.9999;
    double y = 0.5 - log((1.0 + sinLatitude) / (1.0 - sinLatitude)) / (4.0 * 3.14159265358979323846);

    uint32_t mapSize = MapSize(levelOfDetail);
    double mapSize_d = (double)mapSize;
    double px = x * mapSize_d + 0.5;
    double py = y * mapSize_d + 0.5;
    if (px < 0.0) px = 0.0;
    if (px > mapSize_d - 1.0) px = mapSize_d - 1.0;
    if (py < 0.0) py = 0.0;
    if (py > mapSize_d - 1.0) py = mapSize_d - 1.0;

    *pixelX = (int)px;
    *pixelY = (int)py;
}

void TileSystem::PixelXYToLatLong(int pixelX, int pixelY, int levelOfDetail, double* latitude, double* longitude)
{
    double mapSize = (double)MapSize(levelOfDetail);
    double px = (double)pixelX;
    double py = (double)pixelY;
    if (px < 0.0) px = 0.0;
    if (px > mapSize - 1.0) px = mapSize - 1.0;
    if (py < 0.0) py = 0.0;
    if (py > mapSize - 1.0) py = mapSize - 1.0;

    double x = (px / mapSize) - 0.5;
    double y = 0.5 - (py / mapSize);

    *latitude = 90.0 - 360.0 * atan(exp(-y * 2.0 * 3.14159265358979323846)) / 3.14159265358979323846;
    *longitude = 360.0 * x;
}

void TileSystem::PixelXYToTileXY(int pixelX, int pixelY, int* tileX, int* tileY)
{
    *tileX = pixelX / 256;
    *tileY = pixelY / 256;
}
 
void TileSystem::TileXYToPixelXY(int tileX, int tileY, int* pixelX, int* pixelY)
{
    *pixelX = tileX * 256;
    *pixelY = tileY * 256;
}

void TileSystem::TileXYToQuadKey(int tileX, int tileY, int levelOfDetail, char* quadKeyBuffer, uint32_t len)
{
    uint32_t quadKeyIndex = 0;
    for (int i = levelOfDetail; i > 0; i--)
    {
        char digit = '0';
        int mask = 1 << (i - 1);
        if ((tileX & mask) != 0)
        {
            digit++;
        }
        if ((tileY & mask) != 0)
        {
            digit++;
            digit++;
        }
        quadKeyBuffer[quadKeyIndex] = digit;
        quadKeyIndex++;

        if (quadKeyIndex >= len - 1)
        {
            break;
        }
    }

    quadKeyBuffer[quadKeyIndex] = '\0';
}

void TileSystem::QuadKeyToTileXY(const char* quadKey, int* tileX, int* tileY, int* levelOfDetail)
{
    *tileX = *tileY = 0;
    int len = (int)strlen(quadKey);
    *levelOfDetail = len;
    for (int i = len; i > 0; i--)
    {
        int mask = 1 << (i - 1);
        switch (quadKey[len - i])
        {
        case '0':
            break;

        case '1':
            *tileX |= mask;
            break;

        case '2':
            *tileY |= mask;
            break;

        case '3':
            *tileX |= mask;
            *tileY |= mask;
            break;

        default:
            //throw new ArgumentException("Invalid QuadKey digit sequence.");
            break;
        }
    }
}
