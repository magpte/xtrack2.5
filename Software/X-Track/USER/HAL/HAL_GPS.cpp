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

    // 打开 GPS+BDS+GLONASS 三星座联合定位。
    // 校验和 0x1E 已经手动核对过（"PCAS04,7" 各字符异或结果），指令本身
    // 格式合法，但 Mode=7 不在官方 AT300 手册文档范围内，属于社区渠道
    // 验证过的值（其他厂商基于同一颗 CASIC 芯片的项目里能查到这份完整
    // 参数表：1=GPS 2=BDS 3=GPS+BDS 4=GLONASS 5=GPS+GLONASS 6=BDS+GLONASS
    // 7=GPS+BDS+GLONASS）。实测已确认生效：GSA 系统ID 里出现了 1/2/4，
    // 卫星总数从双星座的 7 颗涨到三星座的 12 颗。
    GPS_SERIAL.print("$PCAS04,7*1E\r\n");

    // 三星座联合定位打开后，模块吐出的数据量在 9600 波特率下已经跑到
    // 理论带宽的 83% 左右（实测 784 字节/秒 vs 960 字节/秒理论上限），
    // 余量很紧。但 TinyGPS++（当前用的解析库）从始至终只解析 GGA 和
    // RMC 这两种语句，GSA/GSV/GLL/VTG/ZDA 全部被 gps.encode() 当无关
    // 语句丢弃——也就是说这些字节纯粹是浪费带宽，从没被真正用上过。
    // 用 PCAS03 只保留 GGA+RMC（GGA 本身就带卫星总数/HDOP，RMC 带速度/
    // 航向/日期时间，GPS_Info_t 需要的字段两者全覆盖），实测能把数据量
    // 压到 146 字节/秒，只占理论带宽的 15%，比调高波特率风险小得多
    // （不用改 UART 驱动、不用担心缓冲区够不够），也不影响三星座定位
    // 本身——GGA 里的卫星数字段照样是三个星座加起来的总数。
    // 格式：$PCAS03,nGGA,nGLL,nGSA,nGSV,nRMC,nVTG,nZDA,nANT,...*校验和
    // 每个字段 0=关闭，1=每次定位都输出。
    GPS_SERIAL.print("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
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
