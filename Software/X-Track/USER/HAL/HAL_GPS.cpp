#include "HAL.h"
#include "TinyGPSPlus/src/TinyGPS++.h"

// atoi/memcmp/memset 等，Sky_ParseLine() 和 NMEA_Log_ShouldKeep() 都要用，
// 放在最外层，不依赖 CONFIG_GPS_SKY_ENABLE / CONFIG_GPS_NMEA_LOG_ENABLE
// 里的哪一个打开。
#include <stdlib.h>
#include <string.h>

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

#if CONFIG_GPS_NMEA_LOG_ENABLE
// ---------------------------------------------------------------------
// 原始 NMEA 语句落盘，供后续拖进 u-center 回放/分析。跟上面 Sky_Feed
// 一样按行缓冲，但这里要把语句原始字节（含校验和、\r\n）原封不动交
// 给 HAL::NMEA_Log_Write()，所以不能复用 s_skyLineBuf——Sky_ParseLine()
// 会就地把逗号替换成 '\0'，是破坏性解析，两边必须各自留一份。
//
// 落盘目标（对应 PCAS03 配置，见 GPS_Init()）：
//   GGA / RMC：模块仍以 2Hz 发送（LiveMap 需要这个刷新率），这里按
//              "逢一条丢一条"做 2:1 抽取，落盘时凑够 1Hz，不影响
//              TinyGPS++ / LiveMap 拿到的仍然是完整 2Hz 数据流。
//   GSA：      模块原生按 1Hz 发送（PCAS03 里新打开的），直接收录。
//   GSV：      模块原生按 0.5Hz 发送（PCAS03 里从 5 秒一次改成 2 秒
//              一次），直接收录。
//   其余语句（GLL/VTG/ZDA/...）：PCAS03 已经在源头关掉了，这里的
//              类型过滤只是双重保险。
// ---------------------------------------------------------------------
#define NMEA_LOG_LINE_MAX   128
static char    s_nmeaLogLineBuf[NMEA_LOG_LINE_MAX];
static uint8_t s_nmeaLogLineLen = 0;

// 决定这一条语句要不要写进日志文件。GSA/GSV 模块本身已经是目标频率，
// 来一条收一条；GGA/RMC 模块仍是 2 倍频率，这里做 2:1 抽取。
static bool NMEA_Log_ShouldKeep(const char* line, uint8_t len)
{
    if (len < 6 || line[0] != '$') return false;

    const char* type = &line[3];

    if (memcmp(type, "GSA", 3) == 0) return true;
    if (memcmp(type, "GSV", 3) == 0) return true;

    if (memcmp(type, "GGA", 3) == 0)
    {
        static bool keep = false;
        keep = !keep;
        return keep;
    }
    if (memcmp(type, "RMC", 3) == 0)
    {
        static bool keep = false;
        keep = !keep;
        return keep;
    }

    return false; // 其余语句类型不落盘
}

static void NMEA_Log_Feed(char c)
{
    if (s_nmeaLogLineLen < NMEA_LOG_LINE_MAX - 1)
    {
        s_nmeaLogLineBuf[s_nmeaLogLineLen++] = c;
    }

    if (c == '\n')
    {
        if (NMEA_Log_ShouldKeep(s_nmeaLogLineBuf, s_nmeaLogLineLen))
        {
            HAL::NMEA_Log_Write(s_nmeaLogLineBuf, s_nmeaLogLineLen);
        }
        s_nmeaLogLineLen = 0;
    }
    else if (s_nmeaLogLineLen >= NMEA_LOG_LINE_MAX - 1)
    {
        // 单行异常超长（正常 NMEA 语句不会到 128 字节），丢弃重新
        // 同步到下一个换行符，避免把半条坏数据当正常语句写进日志。
        s_nmeaLogLineLen = 0;
    }
}
#endif

// ---------------------------------------------------------------------
// AID-INI 开机辅助定位（CASIC 二进制协议 Class 0x0B, ID 0x01）。跟上面
// NMEA 走的文本协议完全是两套东西——这里是 CASIC 自己的二进制协议
// （CSIP），包结构、校验和算法都不一样：
//   0xBA 0xCE | len(2B,LE) | class(1B) | id(1B) | payload(len B) | ckSum(4B,LE)
// 校验和算法（已经用官方协议文档 + 社区实测过的 JS 实现交叉核对过）：
// 先把 len(2B)+class(1B)+id(1B) 拼成第一个小端 32 位字，payload 再按
// 4 字节一组、小端拼成后续的字，全部 32 位环绕加法累加起来。
// ---------------------------------------------------------------------

// GPS 时间不跟随闰秒调整，从 1980-01-06 00:00:00 UTC 开始计数，目前
// 比 UTC 快 18 秒——这个偏移量自 2016-12-31 那次闰秒之后一直没变过
// （写这段代码时是 2026 年）。如果之后又插入新的闰秒，这里要跟着改。
#define GPS_UTC_LEAP_SECONDS   18

// 公历日期转"相对 1970-01-01 的天数"，用 Howard Hinnant 的
// days_from_civil 算法（http://howardhinnant.github.io/date_algorithms.html）。
// 没用标准库的 mktime/timegm——嵌入式 libc 对这两个函数的支持和时区
// 处理不一定完整/一致，自己算总天数更可控，而且已经用多组日期
// （含闰年、跨月）在 Python 里核对过结果。
static int32_t GPS_DaysFromCivil(int32_t y, uint32_t m, uint32_t d)
{
    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

// 把 RTC 的公历时间换算成 AID-INI 需要的 GPS 周数 + 周内秒（tow）。
static void GPS_UtcToWeekTow(const HAL::Clock_Info_t& clock, uint16_t* outWeek, double* outTow)
{
    // 1980-01-06（GPS 起始点）相对 1970-01-01 是第 3657 天，已经用
    // Python 的 date 运算核对过。
    int32_t daysSinceGpsEpoch = GPS_DaysFromCivil(clock.year, clock.month, clock.day) - 3657;

    double secOfDay = clock.hour * 3600.0 + clock.minute * 60.0 + clock.second
                    + clock.millisecond / 1000.0;

    double totalSec = (double)daysSinceGpsEpoch * 86400.0 + secOfDay + GPS_UTC_LEAP_SECONDS;

    uint16_t week = (uint16_t)(totalSec / 604800.0);
    *outWeek = week;
    *outTow  = totalSec - (double)week * 604800.0;
}

// CASIC 协议专用校验和（不是 NMEA 的 XOR），len 必须是 4 的整数倍——
// AID-INI 是 56 字节，满足这个要求。
static uint32_t CASIC_Checksum(uint8_t classId, uint8_t msgId, uint16_t len, const uint8_t* payload)
{
    uint32_t ckSum = (uint32_t)len
                   | ((uint32_t)classId << 16)
                   | ((uint32_t)msgId   << 24);

    for (uint16_t i = 0; i < len; i += 4)
    {
        uint32_t word = (uint32_t)payload[i]
                       | ((uint32_t)payload[i + 1] << 8)
                       | ((uint32_t)payload[i + 2] << 16)
                       | ((uint32_t)payload[i + 3] << 24);
        ckSum += word;
    }
    return ckSum;
}

void HAL::GPS_SendAidingData(double latitude, double longitude, const HAL::Clock_Info_t& clock)
{
#if CONFIG_GPS_AID_ENABLE
    // RTC 没校准过（年份明显不对，比如出厂/复位后的默认值）的话，发
    // 过去的时间辅助信息只会帮倒忙——2020 只是一个"肯定不是没校准过"
    // 的粗略下限，不是什么精确边界，跟 DP_Clock.cpp 里同样的判断呼应。
    if (clock.year < 2020)
    {
        return;
    }

    uint16_t week;
    double tow;
    GPS_UtcToWeekTow(clock, &week, &tow);

    uint8_t payload[56];
    memset(payload, 0, sizeof(payload));

    double height = 0.0; // 没有存过海拔，标 flags 里的"高度无效"位
    memcpy(&payload[0],  &latitude,  8);
    memcpy(&payload[8],  &longitude, 8);
    memcpy(&payload[16], &height,    8);
    memcpy(&payload[24], &tow,       8);

    float freqBias = 0.0f; // 没有频偏数据，flags 里对应位保持 0（无效）
    // 上次定位点是"记忆"里的位置，不是这次刚测出来的，实际准确度取决
    // 于这次开机前设备移动了多远。保守按 200km 估计——数量级上足够帮
    // 模块把搜索范围从"全球"缩小到"这一片"，又不会因为估计过于自信，
    // 在用户确实跑远了（比如坐飞机去了外地）的时候反而误导模块。
    float posAcc  = 200000.0f;
    // RTC 是石英钟，两次开机之间（哪怕隔了几周没用）漂移量级在秒级，
    // 5 秒是一个安全但不算离谱保守的估计。
    float timeAcc = 5.0f;
    float freqAcc = 0.0f;
    memcpy(&payload[32], &freqBias, 4);
    memcpy(&payload[36], &posAcc,   4);
    memcpy(&payload[40], &timeAcc,  4);
    memcpy(&payload[44], &freqAcc,  4);
    // payload[48..51] 是保留字段，前面 memset 已经清零

    memcpy(&payload[52], &week, 2);
    payload[54] = 3; // timeSource = 3（RTC），见协议里 NAV-SOL 的备注
    payload[55] = 0x01   // B0 位置有效
                | 0x02   // B1 时间有效
                | 0x20   // B5 位置是经纬度（LLA）格式，不是 ECEF
                | 0x40;  // B6 高度无效

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
        "GPS: AID-INI sent (week=%u, tow=%.1f, lat=%.5f, lon=%.5f)\r\n",
        week, tow, latitude, longitude
    );
#endif
}

void HAL::GPS_Init()
{
    GPS_SERIAL.begin(9600);

#if defined(AT32F435xx)
    // GPS 数据是持续不断的 NMEA 语句流（三星座联合定位打开后单独一条
    // GGA+RMC 语句也有几十字节），如果还用逐字节 RDBF 中断接，MCU 平均
    // 每隔 1 个字节的传输时间（9600bps 下约 1ms）就要进一次中断——这里
    // 换成 DMA 循环接收：USART2 (GPS_SERIAL) -> DMA1 Channel4，数据由
    // 硬件直接搬进 HardwareSerial 内部的环形缓冲区，CPU 只在收到一整
    // 段数据后（IDLE 空闲线中断）被唤醒一次，而不是每个字节都被打断。
    // 通道/请求号选择详见 HardwareSerial::enableRxDMA() 的注释，
    // 与显示屏用的 EDMA_STREAM1、SD 卡用的 DMA2 Channel1/2、ADC 用的
    // DMA1 Channel1 均不冲突。
    GPS_SERIAL.enableRxDMA(
        DMA1_CHANNEL4,
        DMA1MUX_CHANNEL4,
        DMAMUX_DMAREQ_ID_USART2_RX,
        DMA1_Channel4_IRQn
    );
#endif

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
    // 余量很紧。TinyGPS++（当前用的解析库）从始至终只解析 GGA 和 RMC
    // 这两种语句，但现在多了两个新用途会用到别的语句类型：
    //   1) 天球图（SystemInfos 页面）要 GSV，见 Sky_ParseLine()；
    //   2) 原始 NMEA 落盘要 GSA/GSV，供之后拖进 u-center 回放，见
    //      NMEA_Log_Feed()（GGA/RMC 落盘时另有 2:1 抽取，不需要模块
    //      在源头改频率，见那边的注释）。
    // 所以没法再像纯 GGA+RMC 那样把 GSA/GLL/VTG/ZDA 全部关掉，但仍然
    // 按"只留真正用得上的语句类型"的原则控制数据量。
    // 格式：$PCAS03,nGGA,nGLL,nGSA,nGSV,nRMC,nVTG,nZDA,nANT,...*校验和
    // 每个字段 0=关闭，1=每个周期都输出，N=每 N 个周期输出一次。
    // 当前模块定位频率是 2Hz（下面 PCAS02,500 那条）：
    //   nGGA=1, nRMC=1  ：不变，仍然每周期都发（2Hz）——LiveMap 需要
    //                     这个刷新率，见 CONFIG_GPS_REFR_PERIOD 的注释。
    //   nGSA=2          ：新打开，每 2 个周期一次，等效 1Hz，只给
    //                     NMEA 落盘用，TinyGPS++ 不解析 GSA。
    //   nGSV=4          ：从原来的 10（0.2Hz/5 秒一次）改成 4，等效
    //                     0.5Hz/2 秒一次——天球图本来 5 秒刷新一次，
    //                     现在数据更新更勤不会有副作用；NMEA 落盘要
    //                     的正好也是 0.5Hz。
    //   GLL/VTG/ZDA 继续保持关闭，没有别的地方用得上。
    // 校验和 0x04 已经手动核对过（"PCAS03,1,0,2,4,1,0,0,0,0,0,,,0,0"
    // 各字符异或结果）。
    GPS_SERIAL.print("$PCAS03,1,0,2,4,1,0,0,0,0,0,,,0,0*04\r\n");

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

void HAL::GPS_GetSkyInfo(Sky_Info_t* info)
{
#if CONFIG_GPS_SKY_ENABLE
    *info = s_skyCurrent;
#else
    memset(info, 0, sizeof(Sky_Info_t));
#endif
}
