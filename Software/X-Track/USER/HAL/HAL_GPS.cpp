#include "HAL.h"
#include "TinyGPSPlus/src/TinyGPS++.h"
#include "App/Utils/Time/TimeLib.h"
#include <stdlib.h>
#include <string.h>

#define GPS_SERIAL             CONFIG_GPS_SERIAL
#define DEBUG_SERIAL           CONFIG_DEBUG_SERIAL
#define GPS_USE_TRANSPARENT    CONFIG_GPS_USE_TRANSPARENT

static TinyGPSPlus gps;

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
// ---------------------------------------------------------------------
// 统一 NMEA 行缓冲，供 Sky_ParseLine() 和 NMEA_Log_ProcessLine() 使用
// ---------------------------------------------------------------------
#define NMEA_LINE_MAX     192
static char    s_nmeaLineBuf[NMEA_LINE_MAX];
static uint8_t s_nmeaLineLen = 0;
#endif

#if CONFIG_GPS_SKY_ENABLE
static HAL::Sky_Info_t s_skyBuilding;
static HAL::Sky_Info_t s_skyCurrent;

static int NMEA_ParseIntField(const char* s, int fieldIndex)
{
    int cur = 0;
    const char* p = s;
    while (*p && cur < fieldIndex)
    {
        if (*p == ',') cur++;
        p++;
    }
    if (cur != fieldIndex || !*p || *p == ',' || *p == '*') return -1;
    return atoi(p);
}

static void Sky_ParseLine(const char* line)
{
    if (line[0] != '$') return;
    if (line[3] != 'G' || line[4] != 'S' || line[5] != 'V') return;

    HAL::Sky_Constellation_t sys = HAL::SKY_CONSTELLATION_UNKNOWN;
    if (line[1] == 'G' && line[2] == 'P') sys = HAL::SKY_CONSTELLATION_GPS;
    else if (line[1] == 'B' && line[2] == 'D') sys = HAL::SKY_CONSTELLATION_BDS;
    else if (line[1] == 'G' && line[2] == 'L') sys = HAL::SKY_CONSTELLATION_GLONASS;
    else return;

    int totalMsgs = NMEA_ParseIntField(line, 1);
    int msgNum    = NMEA_ParseIntField(line, 2);
    if (totalMsgs <= 0 || msgNum <= 0 || msgNum > totalMsgs) return;

    if (msgNum == 1 && sys == HAL::SKY_CONSTELLATION_GPS)
    {
        memset(&s_skyBuilding, 0, sizeof(s_skyBuilding));
    }

    int field = 4;
    while (s_skyBuilding.count < SKY_MAX_SATELLITES)
    {
        int prn = NMEA_ParseIntField(line, field);
        if (prn <= 0) break;

        int elev = NMEA_ParseIntField(line, field + 1);
        int azim = NMEA_ParseIntField(line, field + 2);
        int snr  = NMEA_ParseIntField(line, field + 3);

        HAL::Sky_Satellite_t* sat = &s_skyBuilding.satellites[s_skyBuilding.count];
        sat->prn           = (uint8_t)prn;
        sat->elevation     = (uint8_t)(elev >= 0 ? elev : 0);
        sat->azimuth       = (uint16_t)(azim >= 0 ? azim : 0);
        sat->snr           = (uint8_t)(snr >= 0 ? snr : 0);
        sat->constellation = sys;

        s_skyBuilding.count++;
        field += 4;
    }

    if (msgNum >= totalMsgs)
    {
        s_skyCurrent = s_skyBuilding;
    }
}
#endif

static float s_pdopCurrent = 0.0f;
static void GSA_ParseLine(const char* line)
{
    if (line[0] != '$') return;
    if (line[3] != 'G' || line[4] != 'S' || line[5] != 'A') return;

    int commaCount = 0;
    const char* p = line;
    while (*p && commaCount < 15)
    {
        if (*p == ',') commaCount++;
        p++;
    }
    if (commaCount == 15 && *p && *p != ',' && *p != '*')
    {
        float val = (float)atof(p);
        if (val > 0.0f && val < 99.0f)
        {
            s_pdopCurrent = val;
        }
    }
}

#if CONFIG_GPS_NMEA_LOG_ENABLE
#define NMEA_LOG_FILE_DIR_NAME  CONFIG_NMEA_LOG_FILE_DIR_NAME

static bool NMEA_Log_ShouldKeep(const char* line, uint8_t len)
{
    if (len < 6 || line[0] != '$') return false;
    const char* tag = line + 3;
    if (memcmp(tag, "GGA", 3) == 0) return true;
    if (memcmp(tag, "RMC", 3) == 0) return true;
    if (memcmp(tag, "GSA", 3) == 0) return true;
    if (memcmp(tag, "GSV", 3) == 0) return true;
    if (memcmp(tag, "ZDA", 3) == 0) return true;
    return false;
}

static void NMEA_Log_ProcessLine(const char* line, uint8_t len)
{
    if (NMEA_Log_ShouldKeep(line, len))
    {
        HAL::NMEA_Log_Write(line, len);
    }
}
#endif

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
static void NMEA_FeedLine(char c)
{
    static bool s_nmeaLineDiscard = false;

    if (s_nmeaLineDiscard)
    {
        if (c == '\n')
        {
            s_nmeaLineDiscard = false;
            s_nmeaLineLen = 0;
        }
        return;
    }

    if (s_nmeaLineLen < NMEA_LINE_MAX - 1)
    {
        s_nmeaLineBuf[s_nmeaLineLen++] = c;
    }
    else
    {
        s_nmeaLineDiscard = true;
        s_nmeaLineLen = 0;
        return;
    }

    if (c == '\n')
    {
        s_nmeaLineBuf[s_nmeaLineLen] = '\0';

#if CONFIG_GPS_NMEA_LOG_ENABLE
        NMEA_Log_ProcessLine(s_nmeaLineBuf, s_nmeaLineLen);
#endif

#if CONFIG_GPS_SKY_ENABLE
        Sky_ParseLine(s_nmeaLineBuf);
#endif
        GSA_ParseLine(s_nmeaLineBuf);

        s_nmeaLineLen = 0;
    }
}
#endif

// ---------------------------------------------------------------------
// AID-INI 开机辅助定位（CASIC 二进制协议 Class 0x0B, ID 0x01）
// ---------------------------------------------------------------------
static void GPS_UtcToWeekTow(const HAL::Clock_Info_t& clock, uint16_t* week, double* tow)
{
    tmElements_t tm;
    tm.Year = clock.year - 1970;
    tm.Month = clock.month;
    tm.Day = clock.day;
    tm.Hour = clock.hour;
    tm.Minute = clock.minute;
    tm.Second = clock.second;
    uint32_t unixTime = makeTime(tm);

    const uint32_t GPS_EPOCH_UNIX = 315964800UL; // 1980-01-06 00:00:00 UTC
    const uint32_t LEAP_SECONDS = 18;             // GPS - UTC 闰秒差

    uint32_t gpsSeconds = (unixTime >= GPS_EPOCH_UNIX) ? (unixTime - GPS_EPOCH_UNIX + LEAP_SECONDS) : 0;
    *week = (uint16_t)(gpsSeconds / 604800UL);
    *tow = (double)(gpsSeconds % 604800UL) + (double)(clock.millisecond) / 1000.0;
}

static uint32_t CASIC_Checksum(uint8_t classId, uint8_t msgId, uint16_t len, const uint8_t* payload)
{
    uint32_t ckSum = (uint32_t)len
                   | ((uint32_t)classId << 16)
                   | ((uint32_t)msgId   << 24);

    if (payload != NULL)
    {
        for (uint16_t i = 0; i < len; i += 4)
        {
            uint32_t word = 0;
            if (i < len)     word |= (uint32_t)payload[i];
            if (i + 1 < len) word |= ((uint32_t)payload[i + 1] << 8);
            if (i + 2 < len) word |= ((uint32_t)payload[i + 2] << 16);
            if (i + 3 < len) word |= ((uint32_t)payload[i + 3] << 24);
            ckSum += word;
        }
    }
    return ckSum;
}

void HAL::GPS_SendAidingData(double latitude, double longitude, const HAL::Clock_Info_t& clock,
                             uint32_t lastFixUnix, uint32_t nowUnix)
{
#if CONFIG_GPS_AID_ENABLE
    if (clock.year < 2020)
    {
        return;
    }

    uint16_t week;
    double tow;
    GPS_UtcToWeekTow(clock, &week, &tow);

    float posAcc;
    float timeAcc;
    const char* tier;

    if (lastFixUnix == 0 || nowUnix == 0 || nowUnix <= lastFixUnix)
    {
        posAcc  = 200000.0f;
        timeAcc = 5.0f;
        tier    = "none(cold)";
    }
    else
    {
        uint32_t elapsed = nowUnix - lastFixUnix;

        if (elapsed < 3600U)
        {
            posAcc  = 5000.0f;
            timeAcc = 1.0f;
            tier    = "<1h";
        }
        else if (elapsed < 21600U)
        {
            posAcc  = 30000.0f;
            timeAcc = 1.0f;
            tier    = "1-6h";
        }
        else if (elapsed < 86400U)
        {
            posAcc  = 100000.0f;
            timeAcc = 2.0f;
            tier    = "6-24h";
        }
        else
        {
            posAcc  = 200000.0f;
            timeAcc = 5.0f;
            tier    = ">24h";
        }
    }

    uint8_t payload[56];
    memset(payload, 0, sizeof(payload));

    double height = 0.0;
    memcpy(&payload[0],  &latitude,  8);
    memcpy(&payload[8],  &longitude, 8);
    memcpy(&payload[16], &height,    8);
    memcpy(&payload[24], &tow,       8);

    float freqBias = 0.0f;
    float freqAcc  = 0.0f;
    memcpy(&payload[32], &freqBias, 4);
    memcpy(&payload[36], &posAcc,   4);
    memcpy(&payload[40], &timeAcc,  4);
    memcpy(&payload[44], &freqAcc,  4);

    memcpy(&payload[52], &week, 2);
    payload[54] = 3; // timeSource = 3 (RTC)
    payload[55] = 0x01   // B0 位置有效
                | 0x02   // B1 时间有效
                | 0x20   // B5 坐标系为大地经纬度 (LLA)
                | 0x40;  // B6 高度有效

    const uint8_t  classId = 0x0B;
    const uint8_t  msgId   = 0x01;
    const uint16_t len     = sizeof(payload);

    uint32_t ckSum = CASIC_Checksum(classId, msgId, len, payload);

    uint8_t packet[6 + sizeof(payload) + 4];
    packet[0] = 0xBA;
    packet[1] = 0xCE;
    packet[2] = (uint8_t)(len & 0xFF);
    packet[3] = (uint8_t)(len >> 8);
    packet[4] = classId;
    packet[5] = msgId;
    memcpy(&packet[6], payload, sizeof(payload));
    packet[6 + sizeof(payload) + 0] = (uint8_t)(ckSum);
    packet[6 + sizeof(payload) + 1] = (uint8_t)(ckSum >> 8);
    packet[6 + sizeof(payload) + 2] = (uint8_t)(ckSum >> 16);
    packet[6 + sizeof(payload) + 3] = (uint8_t)(ckSum >> 24);

    GPS_SERIAL.write(packet, sizeof(packet));

    Serial.printf(
        "GPS: AID-INI sent (week=%u, tow=%.1f, lat=%.5f, lon=%.5f, tier=%s, posAcc=%.0fm, timeAcc=%.0fs)\r\n",
        week, tow, latitude, longitude, tier, (double)posAcc, (double)timeAcc
    );
#endif
}

void HAL::GPS_Init()
{
    GPS_SERIAL.begin(9600);

#if CONFIG_GPS_TRY_MODE7_ENABLE
    delay(100);
    GPS_SERIAL.print("$PCAS01,3*1F\r\n");

#if defined(AT32F435xx)
    while(usart_flag_get(USART2, USART_TDC_FLAG) == RESET);
#endif
    delay(50);

    GPS_SERIAL.begin(38400);
#endif

#if defined(AT32F435xx)
    GPS_SERIAL.enableRxDMA(
        DMA1_CHANNEL4,
        DMA1MUX_CHANNEL4,
        DMAMUX_DMAREQ_ID_USART2_RX,
        DMA1_Channel4_IRQn
    );
#endif

#if CONFIG_GPS_TRY_MODE7_ENABLE
    GPS_SERIAL.print("$PCAS04,7*1E\r\n");
    GPS_SERIAL.print("$PCAS03,1,0,2,4,1,0,0,0,0,0,,,0,0*04\r\n");
    GPS_SERIAL.print("$PCAS02,500*1A\r\n");
#endif

    Serial.print("GPS: TinyGPS++ library v. ");
    Serial.print(TinyGPSPlus::libraryVersion());
    Serial.println(" by Mikal Hart");
}

void HAL::GPS_Update()
{
#if CONFIG_GPS_BUF_OVERLOAD_CHK && !GPS_USE_TRANSPARENT
    int available = GPS_SERIAL.available();
    DEBUG_SERIAL.printf("GPS: Buffer available = %d", available);
    if(available >= SERIAL_RX_BUFFER_SIZE / 2)
    {
        DEBUG_SERIAL.print(", maybe overload!");
    }
    DEBUG_SERIAL.println();
#endif

    static uint32_t s_lastRxTick = 0;
    int available = GPS_SERIAL.available();

    if (available > 0)
    {
        s_lastRxTick = millis();
    }
    else if (s_lastRxTick > 0 && (millis() - s_lastRxTick > 3000))
    {
#if defined(AT32F435xx)
        if (usart_flag_get(USART2, USART_ROERR_FLAG) != RESET ||
            usart_flag_get(USART2, USART_FERR_FLAG) != RESET ||
            usart_flag_get(USART2, USART_NERR_FLAG) != RESET)
        {
            usart_flag_clear(USART2, USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG);
            usart_data_receive(USART2);
        }
#endif
    }

    int bytesProcessed = 0;
    int maxBytes = 128;
    if (available > 256)
    {
        maxBytes = 256;
    }

    while (GPS_SERIAL.available() > 0 && bytesProcessed < maxBytes)
    {
        char c = GPS_SERIAL.read();
#if GPS_USE_TRANSPARENT
        DEBUG_SERIAL.write(c);
#endif

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
        NMEA_FeedLine(c);
#endif

        gps.encode(c);
        bytesProcessed++;
    }

#if GPS_USE_TRANSPARENT
    while (DEBUG_SERIAL.available() > 0)
    {
        GPS_SERIAL.write(DEBUG_SERIAL.read());
    }
#endif
}

bool HAL::GPS_GetInfo(GPS_Info_t* info)
{
    memset(info, 0, sizeof(GPS_Info_t));

    info->isVaild = gps.location.isValid();
    info->longitude = gps.location.lng();
    info->latitude = gps.location.lat();
    info->altitude = gps.altitude.meters();
    info->speed = gps.speed.kmph();
    info->course = gps.course.deg();

    info->clock.year = gps.date.year();
    info->clock.month = gps.date.month();
    info->clock.day = gps.date.day();
    info->clock.hour = gps.time.hour();
    info->clock.minute = gps.time.minute();
    info->clock.second = gps.time.second();
    info->satellites = gps.satellites.value();

    info->pdop = s_pdopCurrent;
    if (info->pdop == 0.0f && gps.hdop.isValid())
    {
        info->pdop = (float)gps.hdop.hdop();
    }

    return info->isVaild;
}

bool HAL::GPS_LocationIsValid()
{
    return gps.location.isValid();
}

double HAL::GPS_GetDistanceOffset(GPS_Info_t* info,  double preLong, double preLat)
{
    return gps.distanceBetween(info->latitude, info->longitude, preLat, preLong);
}

void HAL::GPS_GetSkyInfo(Sky_Info_t* info)
{
#if CONFIG_GPS_SKY_ENABLE
    *info = s_skyCurrent;
#else
    memset(info, 0, sizeof(Sky_Info_t));
#endif
}