#include "HAL.h"
#include "lwgps/lwgps.h"
#include "App/Utils/Time/TimeLib.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GPS_SERIAL             CONFIG_GPS_SERIAL
#define DEBUG_SERIAL           CONFIG_DEBUG_SERIAL
#define GPS_USE_TRANSPARENT    CONFIG_GPS_USE_TRANSPARENT

static lwgps_t gps;
static uint32_t s_lastFixTick = 0;
static uint32_t s_lastRxTick = 0;

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
// ---------------------------------------------------------------------
// 统一 NMEA 行缓冲（仅用于处理极少数跨环形缓冲区边界的半包语句）
// ---------------------------------------------------------------------
#define NMEA_LINE_MAX     192
static char    s_nmeaLineBuf[NMEA_LINE_MAX];
static uint8_t s_nmeaLineLen = 0;
#endif

#if CONFIG_GPS_SKY_ENABLE
static HAL::Sky_Info_t s_skyBuilding;
static HAL::Sky_Info_t s_skyCurrent;

static int NMEA_ParseIntField(const char* s, int fieldIndex, size_t maxLen = 256)
{
    int cur = 0;
    const char* p = s;
    const char* end = s + maxLen;
    while (p < end && *p && cur < fieldIndex)
    {
        if (*p == ',') cur++;
        p++;
    }
    if (cur != fieldIndex || p >= end || !*p || *p == ',' || *p == '*' || *p == '\r' || *p == '\n') return -1;
    return atoi(p);
}

static void Sky_ParseLine(const char* line, size_t len)
{
    if (len < 6 || line[0] != '$') return;
    if (line[3] != 'G' || line[4] != 'S' || line[5] != 'V') return;

    HAL::Sky_Constellation_t sys = HAL::SKY_CONSTELLATION_UNKNOWN;
    if (line[1] == 'G' && line[2] == 'P') sys = HAL::SKY_CONSTELLATION_GPS;
    else if (line[1] == 'B' && line[2] == 'D') sys = HAL::SKY_CONSTELLATION_BDS;
    else if (line[1] == 'G' && line[2] == 'B') sys = HAL::SKY_CONSTELLATION_BDS;
    else if (line[1] == 'G' && line[2] == 'L') sys = HAL::SKY_CONSTELLATION_GLONASS;
    else if (line[1] == 'G' && line[2] == 'A') sys = HAL::SKY_CONSTELLATION_UNKNOWN;
    else if (line[1] == 'G' && line[2] == 'Q') sys = HAL::SKY_CONSTELLATION_GPS;
    else return;

    int totalMsgs = NMEA_ParseIntField(line, 1, len);
    int msgNum    = NMEA_ParseIntField(line, 2, len);
    if (totalMsgs <= 0 || msgNum <= 0 || msgNum > totalMsgs) return;

    static uint32_t s_lastGsvTick = 0;
    uint32_t now = millis();

    if ((now - s_lastGsvTick > 1500) || (msgNum == 1 && (sys == HAL::SKY_CONSTELLATION_GPS || now - s_lastGsvTick > 500)))
    {
        memset(&s_skyBuilding, 0, sizeof(s_skyBuilding));
    }
    s_lastGsvTick = now;

    int field = 4;
    while (s_skyBuilding.count < SKY_MAX_SATELLITES)
    {
        int prn = NMEA_ParseIntField(line, field, len);
        if (prn <= 0) break;

        int elev = NMEA_ParseIntField(line, field + 1, len);
        int azim = NMEA_ParseIntField(line, field + 2, len);
        int snr  = NMEA_ParseIntField(line, field + 3, len);

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
static void GSA_ParseLine(const char* line, size_t len)
{
    if (len < 6 || line[0] != '$') return;
    if (line[3] != 'G' || line[4] != 'S' || line[5] != 'A') return;

    int commaCount = 0;
    const char* p = line;
    const char* end = line + len;
    while (p < end && *p && commaCount < 15)
    {
        if (*p == ',') commaCount++;
        p++;
    }
    if (commaCount == 15 && p < end && *p && *p != ',' && *p != '*' && *p != '\r' && *p != '\n')
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

static void GPS_ProcessLine(const char* line, size_t len)
{
#if CONFIG_GPS_NMEA_LOG_ENABLE
    NMEA_Log_ProcessLine(line, (uint8_t)len);
#endif

#if CONFIG_GPS_SKY_ENABLE
    Sky_ParseLine(line, len);
#endif
    GSA_ParseLine(line, len);
}

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

    HAL::SysLog_Write(
        "[GPS_AID] AID-INI sent (week=%u, tow=%.1f, lat=%.5f, lon=%.5f, tier=%s, posAcc=%.0fm, timeAcc=%.0fs)",
        week, tow, latitude, longitude, tier, (double)posAcc, (double)timeAcc
    );
#endif
}

#if CONFIG_GPS_TRY_MODE7_ENABLE
static void GPS_SendConfigCommands()
{
    GPS_SERIAL.print("$PCAS04,7*1E\r\n");
    GPS_SERIAL.print("$PCAS03,1,0,2,4,1,0,0,0,0,0,,,0,0*04\r\n");
    GPS_SERIAL.print("$PCAS02,500*1A\r\n");
}
#endif

void HAL::GPS_Init()
{
    GPS_SERIAL.begin(9600);

#if CONFIG_GPS_TRY_MODE7_ENABLE
    delay(100);
#if defined(AT32F435xx)
    usart_flag_clear(USART2, USART_TDC_FLAG);
#endif
    GPS_SERIAL.print("$PCAS01,3*1F\r\n");

#if defined(AT32F435xx)
    uint32_t waitCount = 100000;
    while(usart_flag_get(USART2, USART_TDC_FLAG) == RESET && --waitCount > 0);
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
    GPS_SendConfigCommands();
#endif

    HAL::SysLog_Write("[GPS_INIT] Booting GPS with LwGPS parser (38400 baud, Mode7, 2Hz, GSV=4, RxDMA active)");
    lwgps_init(&gps);
    Serial.println("GPS: LwGPS parser v2.4.0 initialized");
}

static uint8_t s_recoverRetryCount = 0;

static void GPS_Recover(uint32_t silentMs)
{
    uint32_t usartSts = 0;
#if defined(AT32F435xx)
    usartSts = USART2->sts;
    uint16_t dmaRemain = dma_data_number_get(DMA1_CHANNEL4);
#else
    uint16_t dmaRemain = 0;
#endif

    s_recoverRetryCount++;

    // 仅在首次触发或关键节点记录日志，避免高频写 SD 卡造成总线拥堵掉帧
    if (s_recoverRetryCount <= 1 || (s_recoverRetryCount % 6 == 0))
    {
        HAL::SysLog_Write("[GPS_RECOVER] Triggered (#%u)! Silent=%lums, STS=0x%08X (RO=%d FE=%d NE=%d PE=%d), DMA_Rem=%u, Fix=%d, Sats=%u",
            s_recoverRetryCount,
            silentMs,
            usartSts,
            (usartSts & USART_ROERR_FLAG) ? 1 : 0,
            (usartSts & USART_FERR_FLAG) ? 1 : 0,
            (usartSts & USART_NERR_FLAG) ? 1 : 0,
            (usartSts & USART_PERR_FLAG) ? 1 : 0,
            dmaRemain,
            gps.fix,
            gps.sats_in_use
        );
    }

#if defined(AT32F435xx)
    // 硬件级清除 DMA 与 USART2 错误与状态标志 (耗时 < 1μs)
    dma_flag_clear(DMA1_GL4_FLAG | DMA1_FDT4_FLAG | DMA1_HDT4_FLAG | DMA1_DTERR4_FLAG);
    usart_flag_clear(USART2, USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG | USART_PERR_FLAG | USART_IDLEF_FLAG | USART_TDC_FLAG);
#endif

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
    s_nmeaLineLen = 0; // 清除可能残留的半包破损 NMEA 语句
#endif

#if CONFIG_GPS_TRY_MODE7_ENABLE
    // Fix: 短时路径连续失败 >= 10 次后强制升级为深度路径。
    // 日志实证：DMA_Rem=2048（缓冲区全空）且每次 recover 后仍无数据，说明模块已进入
    // 协议/内部状态异常，纯粹重拉 DMA 和重发配置无法恢复，必须通过热重启才能唤醒。
    uint32_t effectiveSilentMs = silentMs;
    if (s_recoverRetryCount >= 10 && effectiveSilentMs < 20001)
    {
        effectiveSilentMs = 20001; // 强制进入深度路径
        HAL::SysLog_Write("[GPS_RECOVER] Escalating to deep path after %u failed short retries", s_recoverRetryCount);
    }

    if (effectiveSilentMs > 60000)
    {
        // 极深度掉线（> 60s）：深度路径持续失败的最后手段。
        // 冷重启会清除星历与历书，重新定位需约 30~60s，但能解决模块彻底挂死的问题。
        GPS_SERIAL.begin(9600);
        GPS_SERIAL.print("$PCAS10,2*1E\r\n"); // 冷重启：清除星历、历书、位置，从头搜星
        delay(500);                            // 等待冷重启完成（模块需约 300~500ms）
        GPS_SERIAL.print("$PCAS01,3*1F\r\n"); // 切换至 38400 波特率
        delay(50);                             // 等待波特率切换生效，模块需约 20~50ms
        GPS_SERIAL.begin(38400);
#if defined(AT32F435xx)
        GPS_SERIAL.enableRxDMA(
            DMA1_CHANNEL4,
            DMA1MUX_CHANNEL4,
            DMAMUX_DMAREQ_ID_USART2_RX,
            DMA1_Channel4_IRQn
        );
#endif
        delay(10); // 等待 USART 寄存器与 DMA 稳定后再发配置指令
        GPS_SendConfigCommands();
        HAL::SysLog_Write("[GPS_RECOVER] Extreme offline >60s: COLD RESTART + rebaud + config sent");
    }
    else if (effectiveSilentMs > 20000)
    {
        // 深度掉线（20~60s，或短时路径升级）：模块极可能已内部复位退回 9600 波特率。
        // Fix: 正确顺序加入必要延时：
        //   ① 9600 握手 → ② 热重启（保留星历）→ ③ delay(300) 等待模块启动完成
        //   → ④ 切 38400 → ⑤ delay(50) 等待波特率生效 → ⑥ 武装 DMA
        //   → ⑦ delay(10) 等待稳定 → ⑧ 下发全配置
        // 原版遗漏了步骤 ③ 和 ⑤，导致 $PCAS01 波特率切换指令被模块忽略。
        GPS_SERIAL.begin(9600);
        GPS_SERIAL.print("$PCAS10,0*1C\r\n"); // 热重启：唤醒定位引擎，保留星历快速搜星
        delay(300);                            // Fix: 等待热重启完成（CASIC 建议 200ms，留 100ms 余量）
        GPS_SERIAL.print("$PCAS01,3*1F\r\n"); // 切换至 38400 波特率
        delay(50);                             // Fix: 等待波特率切换生效，模块需约 20~50ms
        GPS_SERIAL.begin(38400);
#if defined(AT32F435xx)
        GPS_SERIAL.enableRxDMA(
            DMA1_CHANNEL4,
            DMA1MUX_CHANNEL4,
            DMAMUX_DMAREQ_ID_USART2_RX,
            DMA1_Channel4_IRQn
        );
#endif
        delay(10); // Fix: 等待 USART 寄存器与 DMA 稳定后再发配置指令
        GPS_SendConfigCommands();
        HAL::SysLog_Write("[GPS_RECOVER] Deep offline >20s: warm restart + rebaud + config sent");
    }
    else
    {
        // 短时掉线（4.5~20s）：模块大概率仍在 38400，直接重拉 DMA 并重锁波特率与配置。
        // 不下发任何复位指令，不破坏模块内部的星历和定位状态。
        GPS_SERIAL.begin(38400);
#if defined(AT32F435xx)
        GPS_SERIAL.enableRxDMA(
            DMA1_CHANNEL4,
            DMA1MUX_CHANNEL4,
            DMAMUX_DMAREQ_ID_USART2_RX,
            DMA1_Channel4_IRQn
        );
#endif
        delay(10);                             // Fix: 等待 USART 寄存器复位与 DMA 武装完全稳定
        GPS_SERIAL.print("$PCAS01,3*1F\r\n"); // 重锁 38400 波特率
        GPS_SendConfigCommands();              // Mode7 多星座 + 2Hz 输出配置
        HAL::SysLog_Write("[GPS_RECOVER] Short offline: DMA re-arm + config sent");
    }
#else
    // 不启用 Mode7 时：仅重拉 DMA，保持默认波特率
    GPS_SERIAL.begin(38400);
#if defined(AT32F435xx)
    GPS_SERIAL.enableRxDMA(
        DMA1_CHANNEL4,
        DMA1MUX_CHANNEL4,
        DMAMUX_DMAREQ_ID_USART2_RX,
        DMA1_Channel4_IRQn
    );
#endif
#endif
}

void HAL::GPS_Update()
{
#if CONFIG_GPS_BUF_OVERLOAD_CHK && !GPS_USE_TRANSPARENT
    int avail_dbg = GPS_SERIAL.available();
    DEBUG_SERIAL.printf("GPS: Buffer available = %d", avail_dbg);
    if(avail_dbg >= SERIAL_RX_BUFFER_SIZE / 2)
    {
        DEBUG_SERIAL.print(", maybe overload!");
    }
    DEBUG_SERIAL.println();
#endif

    static uint32_t s_lastRecoverTick = 0;
    static uint32_t s_lastHeartbeatTick = 0;
    static bool s_lastFixValid = false;
    static bool s_hasEverFixed = false;
    static int s_lastSats = -1;

    uint32_t now = millis();
    int available = GPS_SERIAL.available();

    if (available > 0)
    {
        s_lastRxTick = now;
        s_recoverRetryCount = 0; // 收到物理数据，复位重试计数器
    }
    else if (s_hasEverFixed && s_lastRxTick > 0 && now > 6000)
    {
        // 阶梯自愈看门狗：仅在“曾经成功定位”且“物理串口彻底无任何数据输出”时才触发
        // (若仅是进隧道/室内遮挡导致卫星为0，串口仍有NMEA/UTC输出，绝不触发自愈)

        // 阶段 1 (> 2500ms)：快速清除 USART/DMA 硬件错误标志，防止 DMA 挂起
        if (now - s_lastRxTick > 2500)
        {
#if defined(AT32F435xx)
            uint32_t sts = USART2->sts;
            uint16_t dmaRemain = dma_data_number_get(DMA1_CHANNEL4);
            if (sts & (USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG | USART_PERR_FLAG))
            {
                HAL::SysLog_Write("[GPS_WARN] Rx silent %lums! USART errors: STS=0x%08X (RO=%d FE=%d NE=%d PE=%d), DMA_Rem=%u",
                    now - s_lastRxTick, sts,
                    (sts & USART_ROERR_FLAG) ? 1 : 0,
                    (sts & USART_FERR_FLAG) ? 1 : 0,
                    (sts & USART_NERR_FLAG) ? 1 : 0,
                    (sts & USART_PERR_FLAG) ? 1 : 0,
                    dmaRemain
                );
            }
            dma_flag_clear(DMA1_DTERR4_FLAG);
            if (usart_flag_get(USART2, USART_ROERR_FLAG) != RESET ||
                usart_flag_get(USART2, USART_FERR_FLAG) != RESET ||
                usart_flag_get(USART2, USART_NERR_FLAG) != RESET ||
                usart_flag_get(USART2, USART_PERR_FLAG) != RESET)
            {
                usart_flag_clear(USART2, USART_ROERR_FLAG | USART_FERR_FLAG | USART_NERR_FLAG | USART_PERR_FLAG);
            }
#endif
        }

        // 阶段 2 (> 4500ms)：自适应退避无阻塞自愈
        // 前 3 次重试每 5 秒尝试一次，之后退避至每 30 秒尝试一次，彻底避免 UI 掉帧卡顿
        uint32_t retryInterval = (s_recoverRetryCount < 3) ? 5000 : 30000;
        if (now - s_lastRxTick > 4500 && (now - s_lastRecoverTick > retryInterval))
        {
            s_lastRecoverTick = now;
            GPS_Recover(now - s_lastRxTick);
        }
    }

    // 定位状态改变监测（定位获取 / 定位丢失）
    bool currentFixValid = gps.is_valid && (gps.fix > 0) && (now - s_lastFixTick < 2500);
    if (currentFixValid != s_lastFixValid)
    {
        s_lastFixValid = currentFixValid;
        if (currentFixValid)
        {
            s_hasEverFixed = true; // 记录本次开机已成功定位过
            HAL::SysLog_Write("[GPS_STATE] FIX ACQUIRED! Sats=%d, Pos=(%.5f,%.5f), Speed=%.1fkm/h, HDOP=%.1f",
                gps.sats_in_use, (double)gps.latitude, (double)gps.longitude, (float)(gps.speed * 1.852f), (float)gps.dop_h);
        }
        else
        {
            HAL::SysLog_Write("[GPS_STATE] FIX LOST! LastSats=%d, FixMode=%d",
                gps.sats_in_use, gps.fix_mode);
        }
    }

    // 卫星数量显著变动监测 (如降到 0 或大幅度变化)
    int currentSats = (gps.fix > 0 && now - s_lastFixTick < 2500) ? gps.sats_in_use : 0;
    if (currentSats != s_lastSats)
    {
        if (s_lastSats != -1 && (currentSats == 0 || s_lastSats == 0 || abs(currentSats - s_lastSats) >= 3))
        {
            HAL::SysLog_Write("[GPS_STATE] Sats changed: %d -> %d (InUse=%u, InView=%u, Valid=%d)",
                s_lastSats, currentSats, gps.sats_in_use, gps.sats_in_view, gps.is_valid ? 1 : 0);
        }
        s_lastSats = currentSats;
    }

    // 周期性心跳诊断日志 (每 10 秒)
    if (now - s_lastHeartbeatTick >= 10000)
    {
        s_lastHeartbeatTick = now;
        uint16_t dmaRemain = 0;
#if defined(AT32F435xx)
        dmaRemain = dma_data_number_get(DMA1_CHANNEL4);
#endif
        HAL::SysLog_Write("[GPS_HB] Fix=%d, Sats=%d(view:%d), Spd=%.1f, HDOP=%.1f, Alt=%.1f, Pos=(%.5f,%.5f), DMA_Rem=%u, Avail=%d",
            currentFixValid ? 1 : 0,
            currentSats,
            gps.sats_in_view,
            currentFixValid ? (float)(gps.speed * 1.852f) : 0.0f,
            (float)gps.dop_h,
            (float)gps.altitude,
            (double)gps.latitude, (double)gps.longitude,
            dmaRemain,
            available
        );
    }

    int maxBytes = (available > 512) ? 1024 : 512;
    int totalProcessed = 0;

    while (totalProcessed < maxBytes)
    {
        uint16_t contLen = 0;
        const uint8_t* p = GPS_SERIAL.getReadPtr(&contLen);
        if (!p || contLen == 0)
        {
            break;
        }

        uint16_t toProcess = contLen;
        if (totalProcessed + toProcess > maxBytes)
        {
            toProcess = maxBytes - totalProcessed;
        }

        // 零拷贝扫描：在当前连续内存切片中搜索完整行 ('\n')
        uint16_t consumed = 0;
        while (consumed < toProcess)
        {
            const uint8_t* start = p + consumed;
            uint16_t remain = toProcess - consumed;
            const uint8_t* nl = (const uint8_t*)memchr(start, '\n', remain);

            if (nl)
            {
                uint16_t lineLen = (uint16_t)(nl - start + 1);

#if GPS_USE_TRANSPARENT
                for (uint16_t k = 0; k < lineLen; k++)
                {
                    DEBUG_SERIAL.write(start[k]);
                }
#endif

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
                // 如果之前有跨环形缓冲区未完成的残片，拼接到暂存区中处理
                if (s_nmeaLineLen > 0)
                {
                    uint16_t copyLen = lineLen;
                    if (s_nmeaLineLen + copyLen > NMEA_LINE_MAX - 1)
                    {
                        copyLen = (NMEA_LINE_MAX - 1 > s_nmeaLineLen) ? (NMEA_LINE_MAX - 1 - s_nmeaLineLen) : 0;
                    }
                    if (copyLen > 0)
                    {
                        memcpy(s_nmeaLineBuf + s_nmeaLineLen, start, copyLen);
                        s_nmeaLineLen += copyLen;
                        s_nmeaLineBuf[s_nmeaLineLen] = '\0';
                        GPS_ProcessLine(s_nmeaLineBuf, s_nmeaLineLen);
                    }
                    s_nmeaLineLen = 0;
                }
                else
                {
                    // 纯零拷贝路径（覆盖 99% 以上场景）：直接以指针切片解析
                    GPS_ProcessLine((const char*)start, lineLen);
                }
#endif

                // 批量喂入 LwGPS 状态机（极速块解析）
                lwgps_process(&gps, start, lineLen);
                if (gps.is_valid && gps.fix > 0)
                {
                    s_lastFixTick = millis();
                }

                consumed += lineLen;
            }
            else
            {
                // 本段切片末尾没有找到换行符，属于跨边界分片或半包：
#if GPS_USE_TRANSPARENT
                for (uint16_t k = 0; k < remain; k++)
                {
                    DEBUG_SERIAL.write(start[k]);
                }
#endif

#if CONFIG_GPS_SKY_ENABLE || CONFIG_GPS_NMEA_LOG_ENABLE
                if (s_nmeaLineLen + remain < NMEA_LINE_MAX - 1)
                {
                    memcpy(s_nmeaLineBuf + s_nmeaLineLen, start, remain);
                    s_nmeaLineLen += remain;
                }
                else
                {
                    s_nmeaLineLen = 0; // 溢出丢弃
                }
#endif

                lwgps_process(&gps, start, remain);
                if (gps.is_valid && gps.fix > 0)
                {
                    s_lastFixTick = millis();
                }

                consumed += remain;
            }
        }

        GPS_SERIAL.advanceTail(consumed);
        totalProcessed += consumed;
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

    uint32_t now = millis();
    // 时效性校验：超过 2.5 秒未收到新定位数据即判定为定位丢失，防止假点写入与时钟冻结
    bool isLocationValid = gps.is_valid && (gps.fix > 0) && (now - s_lastFixTick < 2500);
    bool isTimeValid     = (gps.date > 0 || gps.hours > 0 || gps.minutes > 0 || gps.seconds > 0) && (now - s_lastRxTick < 2500);

    info->isVaild = isLocationValid;
    info->longitude = (double)gps.longitude;
    info->latitude = (double)gps.latitude;
    info->altitude = (float)gps.altitude;
    info->speed = isLocationValid ? (float)(gps.speed * 1.852f) : 0.0f; // 节(knots)转公里/小时(km/h)
    info->course = isLocationValid ? (float)gps.course : 0.0f;

    if (isTimeValid)
    {
        if (gps.year > 0)
        {
            info->clock.year = (uint16_t)(2000 + gps.year);
            info->clock.month = gps.month;
            info->clock.day = gps.date;
        }
        info->clock.hour = gps.hours;
        info->clock.minute = gps.minutes;
        info->clock.second = gps.seconds;
        info->clock.millisecond = 0;
    }

    info->satellites = (gps.fix > 0 && now - s_lastFixTick < 2500) ? gps.sats_in_use : 0;

    info->pdop = s_pdopCurrent;
    if (info->pdop == 0.0f && gps.dop_p > 0.0f)
    {
        info->pdop = (float)gps.dop_p;
    }
    else if (info->pdop == 0.0f && gps.dop_h > 0.0f)
    {
        info->pdop = (float)gps.dop_h;
    }

    return info->isVaild;
}

bool HAL::GPS_LocationIsValid()
{
    return gps.is_valid && (gps.fix > 0) && (millis() - s_lastFixTick < 2500);
}

double HAL::GPS_GetDistanceOffset(GPS_Info_t* info,  double preLong, double preLat)
{
    if (!info || (info->latitude == 0.0 && info->longitude == 0.0) || (preLat == 0.0 && preLong == 0.0))
    {
        return 0.0;
    }
    // 针对近距离点（周期 500ms，点距 < 500米），等矩平面投影具有毫米级精度且计算极快
    float dLat = (float)(info->latitude - preLat) * (3.1415926535897932f / 180.0f);
    float dLon = (float)(info->longitude - preLong) * (3.1415926535897932f / 180.0f);
    float meanLat = (float)((info->latitude + preLat) * 0.5) * (3.1415926535897932f / 180.0f);
    float x = dLon * cosf(meanLat) * 6371000.0f;
    float y = dLat * 6371000.0f;
    return (double)sqrtf(x * x + y * y);
}

void HAL::GPS_GetSkyInfo(Sky_Info_t* info)
{
#if CONFIG_GPS_SKY_ENABLE
    *info = s_skyCurrent;
#else
    memset(info, 0, sizeof(Sky_Info_t));
#endif
}