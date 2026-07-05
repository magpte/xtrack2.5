#include "HAL.h"
#include "TinyGPSPlus/src/TinyGPS++.h"

#define GPS_SERIAL             CONFIG_GPS_SERIAL
#define DEBUG_SERIAL           CONFIG_DEBUG_SERIAL
#define GPS_USE_TRANSPARENT    CONFIG_GPS_USE_TRANSPARENT

static TinyGPSPlus gps;

#if CONFIG_GPS_NMEA_LOG_ENABLE
// 简单的行缓冲：凑够一条完整的 NMEA 语句（以 '\n' 结尾）才写一次 SD 卡，
// 不是收到一个字节就开关一次文件——NMEA 语句一般不超过 82 字节，128
// 足够留余量。如果因为噪声/丢字节导致一直凑不到换行符，缓冲区满了就
// 直接丢弃重来，不让脏数据把 /nmea.log 弄得一团糟。
#define NMEA_LOG_LINE_MAX  128
static char    s_nmeaLineBuf[NMEA_LOG_LINE_MAX];
static uint8_t s_nmeaLineLen = 0;

static void NMEA_Log_Feed(char c)
{
    if (s_nmeaLineLen < NMEA_LOG_LINE_MAX - 1)
    {
        s_nmeaLineBuf[s_nmeaLineLen++] = c;
    }

    if (c == '\n')
    {
        s_nmeaLineBuf[s_nmeaLineLen] = '\0';
        HAL::SD_WriteNMEALog(s_nmeaLineBuf);
        s_nmeaLineLen = 0;
    }
    else if (s_nmeaLineLen >= NMEA_LOG_LINE_MAX - 1)
    {
        // 缓冲区满了还没见到换行符，说明这条不正常，丢弃重来
        s_nmeaLineLen = 0;
    }
}
#endif

void HAL::GPS_Init()
{
    GPS_SERIAL.begin(9600);

#if CONFIG_GPS_TRY_MODE7_ENABLE
    // 给模块一点时间完成内部启动，再发配置指令，提高指令被正确接收的概率。
    delay(100);

    // 实验性指令，见 HAL_Config.h 里 CONFIG_GPS_TRY_MODE7_ENABLE 的说明。
    // 校验和 0x1E 已经手动核对过（"PCAS04,7" 各字符异或结果），指令本身
    // 格式合法，但 Mode=7 不在官方公开文档范围内，实际效果未知。
    // 模块对这类 PCAS 指令通常不回 ACK/NACK，发出去之后没有反馈可看，
    // 只能靠之后抓 /nmea.log 里的 GSA 系统 ID 变化来判断有没有生效。
    GPS_SERIAL.print("$PCAS04,7*1E\r\n");
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

    while (GPS_SERIAL.available() > 0)
    {
        char c = GPS_SERIAL.read();
#if GPS_USE_TRANSPARENT
        DEBUG_SERIAL.write(c);
#endif
#if CONFIG_GPS_NMEA_LOG_ENABLE
        NMEA_Log_Feed(c);
#endif
        gps.encode(c);
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
