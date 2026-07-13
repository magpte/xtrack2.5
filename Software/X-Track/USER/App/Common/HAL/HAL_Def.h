#ifndef __HAL_DEF_H
#define __HAL_DEF_H

#include <stdint.h>

namespace HAL
{

/* Clock */
typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t week;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
} Clock_Info_t;

/* GPS */
typedef struct
{
    double longitude;
    double latitude;
    float altitude;
    float course;
    float speed;
    int16_t satellites;
    bool isVaild;
    Clock_Info_t clock;
} GPS_Info_t;

/* IMU */
typedef struct
{
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
    int16_t steps;
} IMU_Info_t;

/* Sky plot (satellite azimuth/elevation from GSV) */
typedef enum
{
    SKY_CONSTELLATION_GPS,
    SKY_CONSTELLATION_BDS,
    SKY_CONSTELLATION_GLONASS,
    SKY_CONSTELLATION_UNKNOWN,
} Sky_Constellation_t;

typedef struct
{
    uint8_t prn;
    uint8_t elevation;    // 0~90 度，90 = 正头顶
    uint16_t azimuth;     // 0~359 度，0 = 正北，顺时针
    uint8_t snr;          // 0~99 dB-Hz，0 表示只是"在视野内"但没有实际跟踪到信号
    Sky_Constellation_t constellation;
} Sky_Satellite_t;

#define SKY_MAX_SATELLITES  32

typedef struct
{
    Sky_Satellite_t satellites[SKY_MAX_SATELLITES];
    uint8_t count;
} Sky_Info_t;

/* SportStatus */
typedef struct
{
    uint32_t lastTick;

    float weight;

    float speedKph;
    float speedMaxKph;
    float speedAvgKph;

    union
    {
        uint32_t totalTimeUINT32[2];
        uint64_t totalTime;
    };

    float totalDistance;

    union
    {
        uint32_t singleTimeUINT32[2];
        uint64_t singleTime;
    };

    float singleDistance;
    
} SportStatus_Info_t;

/* Power */
typedef struct
{
    uint16_t voltage;
    uint8_t usage;
    bool isCharging;
    uint16_t fullcharge_capacity;
    uint16_t remaining_capacity;
    uint16_t design_capacity;
    int16_t current;
    int16_t average_power;
    uint16_t time_to;
} Power_Info_t;

}

#endif
