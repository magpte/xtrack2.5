#include "HAL.h"
#include "TinyGPSPlus/src/TinyGPS++.h"

#define GPS_SERIAL             CONFIG_GPS_SERIAL
#define DEBUG_SERIAL           CONFIG_DEBUG_SERIAL
#define GPS_USE_TRANSPARENT    CONFIG_GPS_USE_TRANSPARENT

static TinyGPSPlus gps;

#if CONFIG_GPS_SKY_ENABLE
// ---------------------------------------------------------------------
// GSV（卫星方位角/仰角/信噪比）解析
// ---------------------------------------------------------------------
// TinyGPS++ 从头到尾只解析 GGA/RMC，完全不认识 GSV，所以这部分需要自己
// 手写一个小解析器，跟上面 NMEA_Log_Feed 一样复用"按行缓冲、凑够一条
// 完整语句再处理"的思路。
//
// GSV 语句格式（以 $GPGSV,3,1,10,03,05,325,,10,46,176,23,...*CS 为例）：
//   [0] 语句名（含 talker ID，比如 GP/BD/GL）
//   [1] 这颗星座这一轮一共分几条 GSV 消息发完（消息可能很长，一条最多
//       塞 4 颗卫星，卫星多了要分好几条发）
//   [2] 这是第几条
//   [3] 这颗星座总共有几颗卫星在视野内
//   [4..] 每 4 个字段一组：PRN、仰角、方位角、信噪比，最后可能跟着一个
//         NMEA 4.11 新增的 Signal ID 字段（单个数字，不属于任何卫星），
//         下面的解析循环会自然跳过它（不足 4 个字段就不再当一组处理）。
//
// 一轮完整的天球快照需要集齐 GPS/BDS/GLONASS 三个星座各自的最后一条
// 消息才算数——每颗星座是否发完靠 msgNum >= totalMsgs 判断。集齐之后
// 整体切换成"当前快照"，没集齐之前 UI 侧看到的还是上一轮的数据，不会
// 看到只有一部分星座、图一半新一半旧的中间状态。
// ---------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#define SKY_LINE_MAX     96
static char    s_skyLineBuf[SKY_LINE_MAX];
static uint8_t s_skyLineLen = 0;

static HAL::Sky_Info_t s_skyBuilding;
static HAL::Sky_Info_t s_skyCurrent;
static bool s_skySeenGPS = false, s_skySeenBDS = false, s_skySeenGLONASS = false;

static HAL::Sky_Constellation_t Sky_TalkerToConstellation(const char* talker2)
{
    if (talker2[0] == 'G' && talker2[1] == 'P') return HAL::SKY_CONSTELLATION_GPS;
    if (talker2[0] == 'B' && talker2[1] == 'D') return HAL::SKY_CONSTELLATION_BDS;
    if (talker2[0] == 'G' && talker2[1] == 'B') return HAL::SKY_CONSTELLATION_BDS;
    if (talker2[0] == 'G' && talker2[1] == 'L') return HAL::SKY_CONSTELLATION_GLONASS;
    return HAL::SKY_CONSTELLATION_UNKNOWN;
}

static void Sky_ParseLine(char* line)
{
    if (line[0] != '$') return;
    if (!(line[3] == 'G' && line[4] == 'S' && line[5] == 'V'))
    {
        return;  // 只处理 GSV，其他语句（GGA/RMC 已经交给 TinyGPS++ 了）跳过
    }

    HAL::Sky_Constellation_t constellation = Sky_TalkerToConstellation(&line[1]);
    if (constellation == HAL::SKY_CONSTELLATION_UNKNOWN) return;

    char* star = strchr(line, '*');
    if (star != NULL) *star = '\0';

    // 手动按逗号拆字段，不用校验和校验——这不是安全攸关的功能，个别语句
    // 偶尔损坏，最坏情况只是这一轮天球图少画/错画几颗星，可以接受。
    #define SKY_MAX_FIELDS  24
    char* fields[SKY_MAX_FIELDS];
    int fieldCount = 0;
    fields[fieldCount++] = line;
    for (char* p = line; *p != '\0' && fieldCount < SKY_MAX_FIELDS; p++)
    {
        if (*p == ',')
        {
            *p = '\0';
            fields[fieldCount++] = p + 1;
        }
    }
    if (fieldCount < 4) return;

    int totalMsgs = atoi(fields[1]);
    int msgNum    = atoi(fields[2]);

    int idx = 4;
    while (idx + 4 <= fieldCount)
    {
        const char* prnStr  = fields[idx];
        const char* elevStr = fields[idx + 1];
        const char* azimStr = fields[idx + 2];
        const char* snrStr  = fields[idx + 3];
        idx += 4;

        // 仰角/方位角任一为空，说明接收机还没解出这颗卫星的具体位置
        // （只是"看到"了信号），没法画在天球图上，跳过。
        if (prnStr[0] == '\0' || elevStr[0] == '\0' || azimStr[0] == '\0')
        {
            continue;
        }

        if (s_skyBuilding.count < SKY_MAX_SATELLITES)
        {
            HAL::Sky_Satellite_t* sat = &s_skyBuilding.satellites[s_skyBuilding.count++];
            sat->prn = (uint8_t)atoi(prnStr);
            sat->elevation = (uint8_t)atoi(elevStr);
            sat->azimuth = (uint16_t)atoi(azimStr);
            sat->snr = (uint8_t)atoi(snrStr);
            sat->constellation = constellation;
        }
    }

    if (msgNum >= totalMsgs)
    {
        switch (constellation)
        {
        case HAL::SKY_CONSTELLATION_GPS:      s_skySeenGPS = true; break;
        case HAL::SKY_CONSTELLATION_BDS:      s_skySeenBDS = true; break;
        case HAL::SKY_CONSTELLATION_GLONASS:  s_skySeenGLONASS = true; break;
        default: break;
        }
    }

    if (s_skySeenGPS && s_skySeenBDS && s_skySeenGLONASS)
    {
        s_skyCurrent = s_skyBuilding;
        memset(&s_skyBuilding, 0, sizeof(s_skyBuilding));
        s_skySeenGPS = s_skySeenBDS = s_skySeenGLONASS = false;
    }
}

static void Sky_Feed(char c)
{
    if (s_skyLineLen < SKY_LINE_MAX - 1)
    {
        s_skyLineBuf[s_skyLineLen++] = c;
    }

    if (c == '\n')
    {
        s_skyLineBuf[s_skyLineLen] = '\0';
        Sky_ParseLine(s_skyLineBuf);
        s_skyLineLen = 0;
    }
    else if (s_skyLineLen >= SKY_LINE_MAX - 1)
    {
        s_skyLineLen = 0;
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
    // 每个字段 0=关闭，1=每个周期都输出，N=每 N 个周期输出一次。
    // GSV（nGSV）为了天球图重新打开，但设成"每 10 个周期一次"——现在是
    // 2Hz，10 个周期正好是 5 秒，跟 SystemInfos 天球图本来就是 5 秒刷新
    // 一次对上，没必要跟着 2Hz 一起收 GSV（卫星在天上移动很慢，5 秒一次
    // 完全够用）。GSA/GLL/VTG/ZDA 继续保持关闭，没有别的地方用得上。
    // 校验和 0x33 已经手动核对过。
    GPS_SERIAL.print("$PCAS03,1,0,0,10,1,0,0,0,0,0,,,0,0*33\r\n");

    // 把模块本身的定位频率从默认 1Hz 提到 2Hz。只改这一条，不改
    // CONFIG_GPS_REFR_PERIOD 的话，app 这边还是按 1 秒才去问一次，等于
    // 模块算了两次新定位、app 只用上一半——两处要一起改，见 Config.h。
    // 校验和 0x1A 已经手动核对过（"PCAS02,500" 各字符异或结果）。
    // 同样是 ROM 版不保存配置，每次开机都要重发。
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

    while (GPS_SERIAL.available() > 0)
    {
        char c = GPS_SERIAL.read();
#if GPS_USE_TRANSPARENT
        DEBUG_SERIAL.write(c);
#endif

#if CONFIG_GPS_SKY_ENABLE
        Sky_Feed(c);
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

void HAL::GPS_GetSkyInfo(Sky_Info_t* info)
{
#if CONFIG_GPS_SKY_ENABLE
    *info = s_skyCurrent;
#else
    memset(info, 0, sizeof(Sky_Info_t));
#endif
}
