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

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
// ---------------------------------------------------------------------
// 统一 NMEA 行缓冲区，供给 Sky_ParseLine() 和 NMEA_Log_ProcessLine() 使用。
// 避免两边各自维持一套 byte-by-byte 拼行逻辑。
// ---------------------------------------------------------------------
#define NMEA_LINE_MAX     192
static char    s_nmeaLineBuf[NMEA_LINE_MAX];
static uint8_t s_nmeaLineLen = 0;
#endif

#if CONFIG_GPS_SKY_ENABLE
// ---------------------------------------------------------------------
// GSV（卫星方位角/仰角/信噪比）解析
// ---------------------------------------------------------------------
// TinyGPS++ 从头到尾只解析 GGA/RMC，完全不认识 GSV，所以这部分需要自己
// 手写一个小解析器，复用"按行缓冲、凑够一条完整语句再处理"的思路。
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
#endif

#if CONFIG_GPS_NMEA_LOG_ENABLE
// ---------------------------------------------------------------------
// 原始 NMEA 语句落盘，供后续拖进 u-center 回放/分析。
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

// 校验 NMEA 语句的异或校验和（$ 与 * 之间的字符异或和是否等于 * 后面的 2 位 Hex）
static bool NMEA_ValidateChecksum(const char* line, uint8_t len)
{
    if (len < 9 || line[0] != '$') return false; // 最短有效语句：$XXYYY*CS\r\n

    const char* star = strchr(line, '*');
    if (!star || (uint8_t)(star - line + 3) > len) return false;

    uint8_t calculated = 0;
    for (const char* p = line + 1; p < star; p++)
    {
        calculated ^= (uint8_t)(*p);
    }

    char h1 = star[1];
    char h2 = star[2];
    auto hexVal = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0xFF;
    };

    uint8_t v1 = hexVal(h1);
    uint8_t v2 = hexVal(h2);
    if (v1 == 0xFF || v2 == 0xFF) return false;

    return calculated == ((v1 << 4) | v2);
}

// 决定这一条语句要不要写进日志文件。GSA/GSV 模块本身已经是目标频率，
// 来一条收一条；GGA/RMC 模块仍是 2 倍频率，这里做 2:1 抽取。
// （注：其余语句如 GLL/VTG/ZDA 等在 HAL_Init 的 PCAS03 配置指令中已在源头关闭）。
static bool NMEA_Log_ShouldKeep(const char* line, uint8_t len)
{
    if (len < 6 || line[0] != '$') return false;

    // 先做 NMEA XOR 校验和完整性校验，排除 UART 传输噪点/脏数据，避免坏行落盘
    if (!NMEA_ValidateChecksum(line, len)) return false;

    const char* comma = strchr(line, ',');
    if (!comma) return false;
    uint8_t tagLen = (uint8_t)(comma - line - 1);
    if (tagLen < 3) return false;

    const char* type = comma - 3;

    // GSA 与 GSV 模块已是目标频率，来一条收一条
    if (type[0] == 'G' && type[1] == 'S' && (type[2] == 'A' || type[2] == 'V'))
    {
        return true;
    }

    // GGA 与 RMC 做 2:1 抽取为 1Hz：
    // 基于时间戳 Epoch 识别与整秒对齐机制：
    // 1. 同一时间戳的 GGA 与 RMC 共享相同的保留/丢弃判决，100% 确保同一 Epoch 内同留同丢；
    // 2. 当有时间戳时，每个整秒（秒数递增）只保留首个子帧，消除 0.5s 相位跳变；
    // 3. 在开机未获时钟前（时间戳为空），平滑降级为交替翻转。
    static char s_currentEpochTime[16] = {0};
    static bool s_currentEpochKept = false;
    static uint32_t s_lastKeptSecInt = 0xFFFFFFFF;
    static bool s_fallbackToggle = false;

    if ((type[0] == 'G' && type[1] == 'G' && type[2] == 'A') ||
        (type[0] == 'R' && type[1] == 'M' && type[2] == 'C'))
    {
        const char* nextComma = strchr(comma + 1, ',');
        uint8_t timeLen = nextComma ? (uint8_t)(nextComma - (comma + 1)) : 0;

        if (timeLen >= 6)
        {
            // 如果与当前记录的 Epoch 时间戳完全相同（如 GGA 先到达被判定后，RMC 随后以同时间到达）
            if (s_currentEpochTime[0] != '\0' &&
                memcmp(comma + 1, s_currentEpochTime, timeLen) == 0 &&
                s_currentEpochTime[timeLen] == '\0')
            {
                return s_currentEpochKept;
            }

            // 新 Epoch 时间戳到达，更新 Epoch 标识
            if (timeLen < sizeof(s_currentEpochTime))
            {
                memcpy(s_currentEpochTime, comma + 1, timeLen);
                s_currentEpochTime[timeLen] = '\0';
            }

            // 解析前 6 位整数秒 hhmmss
            uint32_t secInt = 0;
            for (uint8_t i = 0; i < 6; i++)
            {
                char c = comma[1 + i];
                if (c >= '0' && c <= '9')
                {
                    secInt = secInt * 10 + (c - '0');
                }
            }

            // 若进入了新的整数秒（如 1 秒 2 帧中只保留第 1 帧），则保留并更新记录
            if (secInt != s_lastKeptSecInt)
            {
                s_lastKeptSecInt = secInt;
                s_currentEpochKept = true;
            }
            else
            {
                s_currentEpochKept = false;
            }

            return s_currentEpochKept;
        }
        else
        {
            // 时间戳尚为空时（冷启动搜星初期的未定位包）
            if (type[0] == 'G' && type[1] == 'G' && type[2] == 'A')
            {
                s_fallbackToggle = !s_fallbackToggle;
                s_currentEpochKept = s_fallbackToggle;
                s_currentEpochTime[0] = '\0';
                return s_fallbackToggle;
            }
            if (type[0] == 'R' && type[1] == 'M' && type[2] == 'C')
            {
                return s_currentEpochKept;
            }
        }
    }

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

static float s_pdopCurrent = 0.0f;

static void GSA_ParseLine(char* line)
{
    if (line[0] != '$') return;
    if (!(line[3] == 'G' && line[4] == 'S' && line[5] == 'A')) return;

    char* star = strchr(line, '*');
    if (star != NULL) *star = '\0';

    char* fields[20];
    int fieldCount = 0;
    fields[fieldCount++] = line;
    for (char* p = line; *p != '\0' && fieldCount < 20; p++)
    {
        if (*p == ',')
        {
            *p = '\0';
            fields[fieldCount++] = p + 1;
        }
    }

    if (fieldCount > 15 && fields[15][0] != '\0')
    {
        float val = (float)atof(fields[15]);
        if (val > 0.0f)
        {
            s_pdopCurrent = val;
        }
    }
}

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
        // 单行异常超长（超过 192 字节），标记进入丢弃模式，丢弃该坏行的后半段，
        // 直到下一个 '\n' 为止，彻底消除截断尾巴（如 ",,,30,0*4D"）被当作新行落盘的问题。
        s_nmeaLineDiscard = true;
        s_nmeaLineLen = 0;
        return;
    }

    if (c == '\n')
    {
        s_nmeaLineBuf[s_nmeaLineLen] = '\0';

        // 先执行不破坏字符串结构的 NMEA Log 写入处理
#if CONFIG_GPS_NMEA_LOG_ENABLE
        NMEA_Log_ProcessLine(s_nmeaLineBuf, s_nmeaLineLen);
#endif

        // 再执行会就地替换逗号为 '\0' 的 Sky Parse 和 GSA PDOP 解析
#if CONFIG_GPS_SKY_ENABLE
        Sky_ParseLine(s_nmeaLineBuf);
#endif
        GSA_ParseLine(s_nmeaLineBuf);

        s_nmeaLineLen = 0;
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

void HAL::GPS_SendAidingData(double latitude, double longitude, const HAL::Clock_Info_t& clock,
                             uint32_t lastFixUnix, uint32_t nowUnix)
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

    // ------------------------------------------------------------------
    // 动态 posAcc / timeAcc：根据上次成功定位距今多久，分档选择精度估计。
    //
    // posAcc（位置精度，单位 m）决定模块要在多大的"置信圆"里搜卫星。
    // 圆越小，候选星越少，搜星越快，TTFF 越短。但如果估计过于自信、
    // 而用户实际上已经跑远了（比如坐飞机去了外地），反而会让模块在一
    // 个错误的地方死磕，适得其反。分档策略：
    //   < 1 小时  → 5 km：正常骑行/步行，移动距离很有限，估计可以很紧
    //   < 6 小时  → 30 km：稍长一些，但跑不出一个城市范围
    //   < 24 小时 → 100 km：隔天开机，可能坐过车，给一个省级范围
    //   其他/未知 → 200 km：保守的全局 fallback，跟改动前行为一致
    //
    // timeAcc（时间精度，单位 s）决定模块在多宽的多普勒频偏范围内搜信号。
    // RTC 是石英钟，长期不用偏差会积累，但几小时内漂移量级远不到 1 秒。
    // 时间差越短 → RTC 偏差越小 → timeAcc 可以更紧：
    //   < 6 小时  → 1 s：RTC 漂移极有限，几十毫秒量级
    //   < 24 小时 → 2 s：隔天，偏差通常 < 1 秒，留余量
    //   其他/未知 → 5 s：保守估计（跟改动前行为一致）
    // ------------------------------------------------------------------
    float posAcc;
    float timeAcc;
    const char* tier;

    // lastFixUnix == 0 说明从未有过有效定位，或者 nowUnix 无效
    if (lastFixUnix == 0 || nowUnix == 0 || nowUnix <= lastFixUnix)
    {
        posAcc  = 200000.0f;
        timeAcc = 5.0f;
        tier    = "none(cold)";
    }
    else
    {
        uint32_t elapsed = nowUnix - lastFixUnix; // 单位：秒

        if (elapsed < 3600U)           // < 1 小时
        {
            posAcc  = 5000.0f;
            timeAcc = 1.0f;
            tier    = "<1h";
        }
        else if (elapsed < 21600U)     // 1~6 小时
        {
            posAcc  = 30000.0f;
            timeAcc = 1.0f;
            tier    = "1-6h";
        }
        else if (elapsed < 86400U)     // 6~24 小时
        {
            posAcc  = 100000.0f;
            timeAcc = 2.0f;
            tier    = "6-24h";
        }
        else                           // > 24 小时
        {
            posAcc  = 200000.0f;
            timeAcc = 5.0f;
            tier    = ">24h";
        }
    }

    uint8_t payload[56];
    memset(payload, 0, sizeof(payload));

    double height = 0.0; // 没有存过海拔，标 flags 里的"高度无效"位
    memcpy(&payload[0],  &latitude,  8);
    memcpy(&payload[8],  &longitude, 8);
    memcpy(&payload[16], &height,    8);
    memcpy(&payload[24], &tow,       8);

    float freqBias = 0.0f; // 没有频偏数据，flags 里对应位保持 0（无效）
    float freqAcc  = 0.0f;
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
        "GPS: AID-INI sent (week=%u, tow=%.1f, lat=%.5f, lon=%.5f, tier=%s, posAcc=%.0fm, timeAcc=%.0fs)\r\n",
        week, tow, latitude, longitude, tier, (double)posAcc, (double)timeAcc
    );
#endif
}

void HAL::GPS_Init()
{
    GPS_SERIAL.begin(9600);

#if CONFIG_GPS_TRY_MODE7_ENABLE
    // 给模块一点时间完成内部启动，再发配置指令，提高指令被正确接收的概率。
    delay(100);

    // 将 GPS 模块串口波特率切换为 38400 bps（$PCAS01,3，校验和 0x1F）。
    // 38400 bps 下吞吐上限提升至 ~3.84 KB/s，数据带宽占用率从 9600 bps 下的 83%
    // 大幅降至 20%，留出 80% 安全余量，彻底消除丢包与缓冲溢出，同时物理信号波形稳健。
    GPS_SERIAL.print("$PCAS01,3*1F\r\n");

#if defined(AT32F435xx)
    // 显式等待 9600 bps 下的数据彻底发送完毕，避免在波特率指令发送途中重置 USART 硬件
    while(usart_flag_get(USART2, USART_TDC_FLAG) == RESET);
#endif
    delay(50);

    // 重新将 MCU 串口波特率切至 38400 bps
    GPS_SERIAL.begin(38400);
#endif

#if defined(AT32F435xx)
    // 在 38400 波特率下为该串口开启 DMA 循环接收，替代逐字节 RDBF 中断
    GPS_SERIAL.enableRxDMA(
        DMA1_CHANNEL4,
        DMA1MUX_CHANNEL4,
        DMAMUX_DMAREQ_ID_USART2_RX,
        DMA1_Channel4_IRQn
    );
#endif

#if CONFIG_GPS_TRY_MODE7_ENABLE
    // 打开 GPS+BDS+GLONASS 三星座联合定位。
    // 校验和 0x1E 已经手动核对过（"PCAS04,7" 各字符异或结果）。
    GPS_SERIAL.print("$PCAS04,7*1E\r\n");

    // 配置 NMEA 语句输出类型与频率（GGA/RMC 2Hz, GSA 1Hz, GSV 0.5Hz）
    // 校验和 0x04 已经手动核对过。
    GPS_SERIAL.print("$PCAS03,1,0,2,4,1,0,0,0,0,0,,,0,0*04\r\n");

    // 把模块本身的定位频率从默认 1Hz 提到 2Hz。
    // 校验和 0x1A 已经手动核对过（"PCAS02,500" 各字符异或结果）。
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
        // 若超过 3 秒未收到数据，检查并强行清除硬件 Overrun / Framing / Noise 标志以防锁死
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
        maxBytes = 256; // 缓冲区积压时加大单次 Tick 消化容量，防止数据套圈
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
