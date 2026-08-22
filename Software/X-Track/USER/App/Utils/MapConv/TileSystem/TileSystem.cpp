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
static const float MinLongitude_f = -180.0f;
static const float MaxLongitude_f = 180.0f;
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
    float lat = Clip_f((float)latitude, MinLatitude_f, MaxLatitude_f);
    float lon = Clip_f((float)longitude, MinLongitude_f, MaxLongitude_f);

    float x = (lon + 180.0f) / 360.0f;
    float sinLatitude = FAST_SIN(lat * (MATH_PI_f / 180.0f));
    sinLatitude = Clip_f(sinLatitude, -0.9999f, 0.9999f);
    float y = 0.5f - logf((1.0f + sinLatitude) / (1.0f - sinLatitude)) / (4.0f * MATH_PI_f);

    uint32_t mapSize = MapSize(levelOfDetail);
    float mapSize_f = (float)mapSize;
    *pixelX = (int)Clip_f(x * mapSize_f + 0.5f, 0.0f, mapSize_f - 1.0f);
    *pixelY = (int)Clip_f(y * mapSize_f + 0.5f, 0.0f, mapSize_f - 1.0f);
}

void TileSystem::PixelXYToLatLong(int pixelX, int pixelY, int levelOfDetail, double* latitude, double* longitude)
{
    float mapSize = (float)MapSize(levelOfDetail);
    float x = (Clip_f((float)pixelX, 0.0f, mapSize - 1.0f) / mapSize) - 0.5f;
    float y = 0.5f - (Clip_f((float)pixelY, 0.0f, mapSize - 1.0f) / mapSize);

    *latitude = (double)(90.0f - 360.0f * atanf(expf(-y * 2.0f * MATH_PI_f)) / MATH_PI_f);
    *longitude = (double)(360.0f * x);
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
