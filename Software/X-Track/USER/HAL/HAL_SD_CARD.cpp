#include "HAL.h"
#include "Config/Config.h"
#include "SdFat.h"

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

// Event Recorder 调试用：仅用于临时排查 GPS DMA-RX 与 SD 卡握手之间
// 是否存在时序/资源冲突，不影响正常固件行为。EventRecord2() 内部只是
// 写一条帯时间戳的记录到环形缓冲区，不做 I/O、不阻塞，ISR 里调用也安全。
// 排查结束后可以整段删掉，不需要保留在正式版本里。
#include "EventRecorder.h"
#define EVR_SD_BEGIN   0xB0   // "SD_Init" 相关事件统一用这个 slot 号

bool HAL::SD_Init()
{
    bool retval = true;

    EventRecord2(EVR_SD_BEGIN, 0x00000001, 0);   // 标记：SD_Init() 入口

    pinMode(CONFIG_SD_CD_PIN, INPUT_PULLUP);
    if(digitalRead(CONFIG_SD_CD_PIN))
    {
        Serial.println("SD: CARD was not inserted");
        retval = false;
    }

    Serial.print("SD: init...");
    // 30MHz -> 15MHz：DMA 批量传输把原来逐字节软件轮询之间的间隙去掉了，
    // 相当于同样的分频比下总线上真实跑出来的是背靠背的连续时钟，
    // 之前"能用"可能部分吃的是那些间隙给走线/电平转换的余量。
    // 现在没法接串口口确认信号完整性，先降速换稳定性；
    // 确认能稳定读写之后如果需要更高吞吐，再逐步往上调（例如 20/25/30MHz）
    // 并做长时间读写测试验证。
    EventRecord2(EVR_SD_BEGIN, 0x00000002, 0);   // 标记：即将调用 SD.begin()
    retval = SD.begin(CONFIG_SD_CS_PIN, SD_SCK_MHZ(15));
    EventRecord2(EVR_SD_BEGIN, 0x00000003, retval);   // 标记：SD.begin() 返回

    if(retval)
    {
        SD_CardSize = SD.card()->cardSize();
        SdFile::dateTimeCallback(SD_GetDateTime);
        SD_CheckDir(CONFIG_TRACK_RECORD_FILE_DIR_NAME);
        Serial.printf(
            "success, Type: %s, Size: %0.2f GB\r\n",
            SD_GetTypeName(),
            SD_GetCardSizeMB() / 1024.0f
        );
    }
    else
    {
        uint32_t err = SD.cardErrorCode();
        // 0xFFFFFFFF 作为哨兵值，方便在 Event Recorder 窗口里一眼认出
        // "这条是失败原因"，第二个参数就是 SD.cardErrorCode() 的值。
        EventRecord2(EVR_SD_BEGIN, 0xFFFFFFFF, err);
        Serial.printf("failed: 0x%x\r\n", err);
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
        bool ret = HAL::SD_Init();

        if(ret && SD_EventCallback)
        {
            SD_EventCallback(true);
        }

        HAL::Audio_PlayMusic(ret ? "DeviceInsert" : "Error");
    }
    else
    {
        SD_IsReady = false;

        if(SD_EventCallback)
        {
            SD_EventCallback(false);
            SD_CardSize = 0;
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
    bool isInsert = (digitalRead(CONFIG_SD_CD_PIN) == LOW);

    CM_VALUE_MONITOR(isInsert, SD_Check(isInsert));
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

