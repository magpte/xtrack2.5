#include "HAL.h"
#include "Config/Config.h"
#include "SdFat.h"
#include <string.h>

namespace DataProc
{
    void Recorder_PeriodicTask();
}

static SdFat SD(&CONFIG_SD_SPI);

static bool SD_IsReady = false;
static uint32_t SD_CardSize = 0;

static HAL::SD_CallbackFunction_t SD_EventCallback = nullptr;

/*
 * User provided date time callback function.
 * See SdFile::dateTimeCallback() for usage.
 */
static void SD_GetDateTime(uint16_t* date, uint16_t* time)
{
    // User gets date and time from GPS or real-time
    // clock in real callback function
    HAL::Clock_Info_t clock;
    HAL::Clock_GetInfo(&clock);

    // return date using FAT_DATE macro to format fields
    *date = FAT_DATE(clock.year, clock.month, clock.day);

    // return time using FAT_TIME macro to format fields
    *time = FAT_TIME(clock.hour, clock.minute, clock.second);
}

static bool SD_CheckDir(const char* path)
{
    bool retval = true;
    if(!SD.exists(path))
    {
        Serial.printf("SD: Auto create path \"%s\"...", path);
        retval = SD.mkdir(path);
        Serial.println(retval ? "success" : "failed");
    }
    return retval;
}

// ---------------------------------------------------------------------
// 原始 NMEA 语句落盘（供 u-center 回放）。跟 DP_Recorder.cpp 里 GPX
// 轨迹文件的思路基本一致（写缓冲攒批 + 按时间 sync，原因见那边的
// 注释），但这里故意没有走 lv_fs（App 层给 UI/DataProc 用的文件系统
// 抽象），而是直接用本文件已经持有的 SdFat 对象——原始 NMEA 落盘是
// GPS_Update() 每次收到一个字节就可能触发的高频操作，放在 HAL 层
// 离数据源更近，不用跨层传一份数据再打包成 App 层的账户/消息。
//
// 文件懒加载：不在 SD_Init() 里主动开文件，而是等第一条要落盘的
// NMEA 语句真正到达（HAL::NMEA_Log_Write() 第一次被调用）时才开，
// 避免 GPS 还没吐出任何数据、或者这次开机 GPS 模块干脆没接好的情况下
// 平白在 SD 卡上留一个空文件。
// ---------------------------------------------------------------------
#define NMEA_LOG_FILE_NAME_FMT      "/" CONFIG_NMEA_LOG_FILE_DIR_NAME "/%02d%02d%02d%02d.LOG"
// 跟 DP_Recorder.cpp 的 RECORDER_WRITE_BUF_SIZE（1024）看齐，不是凑巧
// 选一样的数字——SdFat（FatVolume::m_cache）整张卡只有一个 512 字节的
// 扇区缓存，NMEA 日志和 GPX 轨迹这两个文件共用它。缓冲区越小，我们
// 触发 SdFat 底层 write() 的次数就越多，两个文件的 write() 在时间上
// 撞在一起的概率也越高——每撞一次，共享缓存就要多付一次"写回旧扇区+
// 读入新扇区"的额外 SD 物理 I/O。调大到跟 GPX 一致，两边触发底层
// write() 的频率更接近、次数也更少，降低撞车概率。注意这是应用层
// 缓冲区，跟 SdFat 内部那个写死 512 字节（SD 卡物理扇区大小）的
// FatCache 是两回事，后者没法调大。
#define NMEA_LOG_WRITE_BUF_SIZE     24576 // 24KB (48 * 512B sectors)
#define NMEA_LOG_SYNC_INTERVAL_MS   30000 // 30s
#define SD_SECTOR_SIZE              512

static File     s_nmeaLogFile;
static bool     s_nmeaLogFileOpen = false;
static bool     s_nmeaLogOpenFailed = false; // 开过一次失败就不再重试，避免每条语句都去戳一次坏掉的 SD 卡
static bool     s_nmeaLogNeedSync = false;   // 标记是否有物理写入，避免无数据时空刷 sync() 导致的 Flash 磨损与主线程卡顿
#if defined(__GNUC__) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
#  define ALIGN_WORD4 __attribute__((aligned(4)))
#else
#  define ALIGN_WORD4
#endif

static char     s_nmeaLogWriteBuf[NMEA_LOG_WRITE_BUF_SIZE] ALIGN_WORD4;
static uint32_t s_nmeaLogWriteBufLen = 0;
static uint32_t s_nmeaLogLastSyncTick = 0;

static void NMEA_Log_FlushBuffer(bool forceAll = false)
{
    if(s_nmeaLogWriteBufLen == 0)
    {
        return;
    }

    uint32_t flushLen = s_nmeaLogWriteBufLen;
    if (!forceAll)
    {
        // 512 字节物理扇区对齐：只写入整扇区部分，尾部零头留在缓冲区中，消除 Read-Modify-Write 额外 Flash I/O
        flushLen = (s_nmeaLogWriteBufLen / SD_SECTOR_SIZE) * SD_SECTOR_SIZE;
    }

    if (flushLen == 0)
    {
        return;
    }

    s_nmeaLogFile.write((const uint8_t*)s_nmeaLogWriteBuf, flushLen);
    s_nmeaLogNeedSync = true;

    s_nmeaLogWriteBufLen -= flushLen;
    if (s_nmeaLogWriteBufLen > 0)
    {
        memmove(s_nmeaLogWriteBuf, s_nmeaLogWriteBuf + flushLen, s_nmeaLogWriteBufLen);
    }
}

static bool NMEA_Log_Open()
{
    HAL::Clock_Info_t clock;
    HAL::Clock_GetInfo(&clock);

    char path[32];
    snprintf(
        path, sizeof(path),
        NMEA_LOG_FILE_NAME_FMT,
        clock.month, clock.day,
        clock.hour, clock.minute
    );

    // SdFat 打开严格符合 8.3 格式的 SFN 路径时，会自动进入极速 SFN 分支，
    // 仅分配 1 个 32 字节目录槽位，且支持同一分钟内开机自动 Append 追加
    s_nmeaLogFile = SD.open(path, FILE_WRITE);
    if(!s_nmeaLogFile)
    {
        Serial.printf("NMEA: SFN log file \"%s\" open failed\r\n", path);
        return false;
    }

    Serial.printf("NMEA: logging to SFN \"%s\"\r\n", path);
    s_nmeaLogNeedSync = false;
    s_nmeaLogLastSyncTick = millis();
    return true;
}

void HAL::NMEA_Log_Write(const char* line, uint32_t len)
{
#if CONFIG_GPS_NMEA_LOG_ENABLE
    if(!SD_IsReady || s_nmeaLogOpenFailed || len == 0)
    {
        return;
    }

    // 纯内存写入：追加到 22KB 内存缓冲区中（耗时 < 2 us），绝不在高频 GPS 任务中同步创建文件或执行 SPI 写操作
    if(s_nmeaLogWriteBufLen + len <= NMEA_LOG_WRITE_BUF_SIZE)
    {
        memcpy(s_nmeaLogWriteBuf + s_nmeaLogWriteBufLen, line, len);
        s_nmeaLogWriteBufLen += len;
    }
#endif
}

void HAL::NMEA_Log_Close()
{
    if(!s_nmeaLogFileOpen)
    {
        return;
    }

    NMEA_Log_FlushBuffer(true);
    s_nmeaLogFile.sync();
    s_nmeaLogFile.close();
    s_nmeaLogFileOpen = false;
    s_nmeaLogOpenFailed = false; // 下次开机/下次插卡应该重新尝试
    s_nmeaLogNeedSync = false;
}

bool HAL::SD_Init()
{
    bool retval = false;

    pinMode(CONFIG_SD_CD_PIN, INPUT_PULLUP);
    if(digitalRead(CONFIG_SD_CD_PIN))
    {
        Serial.println("SD: CD pin HIGH (checking SPI direct)...");
    }

    // 给 SD 卡内部上电与控制器启动预留充分稳定时间（50ms）
    delay(50);

    Serial.print("SD: init...");
    // AT32F435 主频 288MHz，硬件 SPI2 采用 8 分频输出 36MHz 时钟（符合 SD 卡 SPI 模式 <50MHz 规范的最佳极速）
    // 最多重试 2 次，增强冷启动慢速卡与上电瞬态容错
    for (int retry = 0; retry < 2; retry++)
    {
        retval = SD.begin(CONFIG_SD_CS_PIN, SD_SCK_MHZ(36));
        if (retval)
        {
            break;
        }
        delay(30);
    }

    if(retval)
    {
        SD_CardSize = SD.card()->cardSize();
        SdFile::dateTimeCallback(SD_GetDateTime);
        SD_CheckDir(CONFIG_TRACK_RECORD_FILE_DIR_NAME);
#if CONFIG_GPS_NMEA_LOG_ENABLE
        SD_CheckDir(CONFIG_NMEA_LOG_FILE_DIR_NAME);
#endif
        Serial.printf(
            "success, Type: %s, Size: %0.2f GB\r\n",
            SD_GetTypeName(),
            SD_GetCardSizeMB() / 1024.0f
        );
    }
    else
    {
        uint32_t err = SD.cardErrorCode();
        Serial.printf("failed: 0x%x\r\n", err);
        SD_CardSize = 0;
    }

    SD_IsReady = retval;

    return retval;
}

bool HAL::SD_GetReady()
{
    return SD_IsReady;
}

float HAL::SD_GetCardSizeMB()
{
#   define CONV_MB(size) (size*0.000512f)
    return CONV_MB(SD_CardSize);
}

const char* HAL::SD_GetTypeName()
{
    const char* type = "Unknown";

    if(!SD_CardSize)
    {
        goto failed;
    }

    switch (SD.card()->type())
    {
    case SD_CARD_TYPE_SD1:
        type = "SD1";
        break;

    case SD_CARD_TYPE_SD2:
        type = "SD2";
        break;

    case SD_CARD_TYPE_SDHC:
        type = (SD_CardSize < 70000000) ? "SDHC" : "SDXC";
        break;

    default:
        break;
    }

failed:
    return type;
}

static void SD_Check(bool isInsert)
{
    if(isInsert)
    {
        if(SD_IsReady)
        {
            return; // 已经就绪，不重复初始化
        }

        bool ret = HAL::SD_Init();

        if(ret && SD_EventCallback)
        {
            SD_EventCallback(true);
        }

        if(ret)
        {
            HAL::Audio_PlayMusic("DeviceInsert");
        }
        else
        {
            HAL::Audio_PlayMusic("Error");
        }
    }
    else
    {
        if(!SD_IsReady)
        {
            return; // 本来就未就绪，不重复执行拔卡清理
        }

        // 卡被拔出之前先把 NMEA 和串口日志缓冲区落盘、关文件——如果等
        // SD_IsReady 已经置 false 之后再关，数据写入会直接跳过，
        // 缓冲区里剩的数据就再也没机会写进去了，所以顺序上必须放在这一行前面。
#if CONFIG_GPS_NMEA_LOG_ENABLE
        HAL::NMEA_Log_Close();
#endif

        SD_IsReady = false;
        SD_CardSize = 0;

        if(SD_EventCallback)
        {
            SD_EventCallback(false);
        }

        HAL::Audio_PlayMusic("DevicePullout");
    }
}

void HAL::SD_SetEventCallback(SD_CallbackFunction_t callback)
{
    SD_EventCallback = callback;
}

void HAL::SD_Update()
{
    // SD 卡热插拔加固状态机：
    // 1. 开机保护期（前 3.0 秒）：仅同步电平基准，屏蔽上电瞬态毛刺与机械接触抖动；
    // 2. 4 周期（2.0 秒）严格消抖；
    // 3. 失败冷却保护：挂载失败后进入 10 秒冷却期，杜绝无卡/坏卡时的死循环 2 秒超时卡死；
    // 4. 已就绪保护：若 SD 卡在开机时已成功就绪，避免因无硬件 CD 开关导致的误卸载。
    bool rawInsert = (digitalRead(CONFIG_SD_CD_PIN) == LOW);
    static bool s_lastPhysicalState = (digitalRead(CONFIG_SD_CD_PIN) == LOW);
    static uint8_t s_debounceCount = 0;
    static uint32_t s_lastFailedRetryTick = 0;

    uint32_t now = millis();

    if (now < 3000)
    {
        s_lastPhysicalState = rawInsert;
        s_debounceCount = 0;
    }
    else if (rawInsert != s_lastPhysicalState)
    {
        s_debounceCount++;
        if (s_debounceCount >= 4)
        {
            s_lastPhysicalState = rawInsert;
            s_debounceCount = 0;

            if (rawInsert)
            {
                // 只有未就绪且脱离失败冷却期（10s）时才尝试挂载
                if (!SD_IsReady && (s_lastFailedRetryTick == 0 || (now - s_lastFailedRetryTick > 10000)))
                {
                    SD_Check(true);
                    if (!SD_IsReady)
                    {
                        s_lastFailedRetryTick = now;
                    }
                    else
                    {
                        s_lastFailedRetryTick = 0;
                    }
                }
            }
            else
            {
                if (SD_IsReady)
                {
                    SD_Check(false);
                }
                s_lastFailedRetryTick = 0; // 物理拔出后复位失败标记，允许下次插入时立即尝试
            }
        }
    }
    else
    {
        s_debounceCount = 0;
    }

#if CONFIG_GPS_NMEA_LOG_ENABLE
    // 在 SD_Update() 后台周期（500ms）中集中平滑处理文件打开、刷新 NMEA 缓冲区与执行 sync()
    if (SD_IsReady)
    {
        // 开机动画阶段（前 3.0 秒）不执行 SD 卡文件创建，将 NMEA 数据暂存在 22KB 内存缓冲区中，
        // 确保开机动画及页面切换 100% 满帧无任何 SPI I/O 阻塞。
        if (!s_nmeaLogFileOpen && !s_nmeaLogOpenFailed && s_nmeaLogWriteBufLen > 0 && millis() > 3000)
        {
            if (!NMEA_Log_Open())
            {
                s_nmeaLogOpenFailed = true;
            }
            else
            {
                s_nmeaLogFileOpen = true;
            }
        }

        if (s_nmeaLogFileOpen)
        {
            if (s_nmeaLogWriteBufLen >= SD_SECTOR_SIZE)
            {
                NMEA_Log_FlushBuffer(false);
            }

            uint32_t now = millis();
            if (now - s_nmeaLogLastSyncTick >= NMEA_LOG_SYNC_INTERVAL_MS)
            {
                if (s_nmeaLogNeedSync || s_nmeaLogWriteBufLen > 0)
                {
                    NMEA_Log_FlushBuffer(true);
                    s_nmeaLogFile.sync();
                    s_nmeaLogNeedSync = false;
                }
                s_nmeaLogLastSyncTick = now;
            }
        }
    }
#endif

    if (SD_IsReady)
    {
        DataProc::Recorder_PeriodicTask();
    }
}

// ��HAL_SD_CARD.cpp������  
bool HAL::SD_WriteCrashLog(const char* data)  
{  
    if (!SD_IsReady) {  
        return false;  
    }  
      
    File crashFile = SD.open("/crash.log", FILE_WRITE);  
    if (crashFile) {  
        crashFile.print(data);  
        crashFile.close();  
        return true;  
    }  
    return false;  
}
