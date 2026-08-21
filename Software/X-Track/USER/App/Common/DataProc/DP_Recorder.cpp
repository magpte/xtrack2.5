#include <stdio.h>
#include <string.h>
#include "DataProc.h"
#include "Utils/GPX/GPX.h"
#include "Config/Config.h"
#include "Version.h"

using namespace DataProc;

#define RECORDER_GPX_TIME_FMT    "%d-%02d-%02dT%02d:%02d:%02dZ"
#define RECORDER_GPX_FILE_NAME   "/" CONFIG_TRACK_RECORD_FILE_DIR_NAME "/TRK_%d%02d%02d_%02d%02d%02d.gpx"
#define RECORDER_GPX_META_NAME   VERSION_FIRMWARE_NAME " " VERSION_SOFTWARE
#define RECORDER_GPX_META_DESC   VERSION_PROJECT_LINK

// ---------------------------------------------------------------------
// 写入缓冲：轨迹点先攒在这个内存缓冲区里，攒够一批再一次性写文件，
// 不是每来一个点就写一次——一次 GPX trkpt 大概 90~110 字节，缓冲区留
// 1KB 大概能攒 9~11 个点，把实际文件写入调用次数压到原来的十分之一
// 左右。
//
// SYNC 的时机改成按时间算，不再按点数——之前吃过一次亏：GPS 从 1Hz
// 提到 2Hz 之后，按"每 N 个点同步一次"实际对应的时间间隔在不知不觉
// 中缩短了一半，同步变勤了，容易撞上 SD 卡偶尔的慢操作导致看门狗
// 超时复位（已经在之前的对话里定位过这个根因）。改成按实际经过的
// 时间来判断，以后不管 GPS 频率再怎么调整，同步的时间节奏都不会跟着
// 意外改变。
//
// 注意：缓冲区能减少"写文件"和"同步"这两个操作本身的调用次数（省
// CPU、省电、减少 SD 卡磨损），但不能保证某一次同步操作的耗时上限——
// 真正防止"某次同步偶尔很慢"演变成"看门狗超时把整个系统复位"的，是
// 配合这次一起做的另一项改动：把喂狗从协作式任务调度器挪到硬件定时器
// 中断里（见 HAL.cpp），两者是互补关系，不是互相替代。
// ---------------------------------------------------------------------
#define RECORDER_WRITE_BUF_SIZE     6144  // 6KB (12 * 512B sectors)
#define RECORDER_SYNC_INTERVAL_MS   30000 // 30s

typedef struct
{
    GPX gpx;
    Recorder_Info_t recInfo;
    lv_fs_file_t file;
    bool active;
    Account* account;

#if defined(__GNUC__) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
#  define ALIGN_WORD4 __attribute__((aligned(4)))
#else
#  define ALIGN_WORD4
#endif

    char writeBuf[RECORDER_WRITE_BUF_SIZE] ALIGN_WORD4;
    uint32_t writeBufLen;      // 缓冲区里已经攒了多少字节，还没写文件
    uint32_t lastSyncTick;     // 上一次真正 sync() 落盘的时刻（lv_tick_get()）
} Recorder_t;

#define SD_SECTOR_SIZE              512

// 把缓冲区里已经攒的内容写进文件。
// forceAll = false 时按照 512 字节物理扇区对齐，仅将整扇区数据落盘，尾部零头留在缓冲区中，消除 Flash Read-Modify-Write。
// forceAll = true 时（如停止录制、文件同步或超出容量）将缓冲区全部内容落盘。
static lv_fs_res_t Recorder_FlushBuffer(Recorder_t* recorder, bool forceAll = false)
{
    if (recorder->writeBufLen == 0)
    {
        return LV_FS_RES_OK;  // 没什么好写的，不做无意义的空写入
    }

    uint32_t flushLen = recorder->writeBufLen;
    if (!forceAll)
    {
        flushLen = (recorder->writeBufLen / SD_SECTOR_SIZE) * SD_SECTOR_SIZE;
    }

    if (flushLen == 0)
    {
        return LV_FS_RES_OK;
    }

    lv_fs_res_t res = lv_fs_write(
                           &(recorder->file),
                           recorder->writeBuf,
                           flushLen,
                           NULL
                       );

    recorder->writeBufLen -= flushLen;
    if (recorder->writeBufLen > 0)
    {
        memmove(recorder->writeBuf, recorder->writeBuf + flushLen, recorder->writeBufLen);
    }

    return res;
}

// 把一段字符串追加进缓冲区；如果加进去会超出缓冲区容量，先把缓冲区里的整扇区数据落盘腾地方。
static lv_fs_res_t Recorder_BufferedWrite(Recorder_t* recorder, const char* str)
{
    uint32_t len = (uint32_t)strlen(str);

    if (len >= RECORDER_WRITE_BUF_SIZE)
    {
        Recorder_FlushBuffer(recorder, true);
        return lv_fs_write(&(recorder->file), str, len, NULL);
    }

    if (recorder->writeBufLen + len > RECORDER_WRITE_BUF_SIZE)
    {
        lv_fs_res_t res = Recorder_FlushBuffer(recorder, false);
        if (res != LV_FS_RES_OK)
        {
            return res;
        }
    }

    memcpy(recorder->writeBuf + recorder->writeBufLen, str, len);
    recorder->writeBufLen += len;
    return LV_FS_RES_OK;
}



static int Recorder_GetTimeConv(
    Recorder_t* recorder,
    const char* format,
    char* buf,
    uint32_t size)
{
    HAL::Clock_Info_t clock;
    int retval = -1;
    if (recorder->account->Pull("Clock", &clock, sizeof(clock)) == Account::RES_OK)
    {
        retval = snprintf(
            buf,
            size,
            format,
            clock.year,
            clock.month,
            clock.day,
            clock.hour,
            clock.minute,
            clock.second
        );
    }

    return retval;
}

static void Recorder_RecPoint(Recorder_t* recorder, HAL::GPS_Info_t* gpsInfo)
{
    // 校验定位有效性：未定位或经纬度为 0 时跳过记录，避免在冷启动/未搜到星时记录假点 (0,0) 以及星历校准时产生的时间回跳
    if (!gpsInfo->isVaild || (gpsInfo->longitude == 0.0 && gpsInfo->latitude == 0.0))
    {
        return;
    }

    //LV_LOG_USER("Track recording...");

    char timeBuf[64];
    int ret = Recorder_GetTimeConv(
        recorder,
        RECORDER_GPX_TIME_FMT,
        timeBuf,
        sizeof(timeBuf)
    );

    if (ret < 0)
    {
        LV_LOG_WARN("cant't get time");
        return;
    }

    recorder->gpx.setEle(String(gpsInfo->altitude, 2));
    recorder->gpx.setTime(timeBuf);

    String gpxStr = recorder->gpx.getPt(
                        GPX_TRKPT,
                        String(gpsInfo->longitude, 6),
                        String(gpsInfo->latitude, 6)
                    );

    Recorder_BufferedWrite(recorder, gpxStr.c_str());
    // 纯内存写入：追加到 6KB 缓冲区（< 2us），落盘与 sync 已由 Recorder_PeriodicTask 后台统一处理
}

static void Recorder_RecStop(Recorder_t* recorder);

static void Recorder_RecStart(Recorder_t* recorder, uint16_t time)
{
    if (recorder->active)
    {
        LV_LOG_WARN("Track recorder already active, stopping previous session first");
        Recorder_RecStop(recorder);
    }

    LV_LOG_USER("Track record start");

    char filepath[128];
    int ret = Recorder_GetTimeConv(
        recorder,
        RECORDER_GPX_FILE_NAME,
        filepath, sizeof(filepath)
    );

    if (ret < 0)
    {
        LV_LOG_WARN("cant't get time");
        return;
    }

    lv_fs_res_t res = lv_fs_open(&(recorder->file), filepath, LV_FS_MODE_WR | LV_FS_MODE_RD);

    if (res == LV_FS_RES_OK)
    {
        LV_LOG_USER("Track file %s open success", filepath);

        GPX* gpx = &(recorder->gpx);

        recorder->writeBufLen = 0;
        recorder->lastSyncTick = lv_tick_get();

        gpx->setMetaName(RECORDER_GPX_META_NAME);
        gpx->setMetaDesc(RECORDER_GPX_META_DESC);
        gpx->setName(filepath);
        gpx->setDesc("");

        Recorder_BufferedWrite(recorder, gpx->getOpen().c_str());
        Recorder_BufferedWrite(recorder, gpx->getMetaData().c_str());
        Recorder_BufferedWrite(recorder, gpx->getTrakOpen().c_str());
        Recorder_BufferedWrite(recorder, gpx->getInfo().c_str());
        Recorder_BufferedWrite(recorder, gpx->getTrakSegOpen().c_str());

        recorder->active = true;
    }
    else
    {
        LV_LOG_ERROR("Track file open error!");
    }
}

static void Recorder_RecStop(Recorder_t* recorder)
{
    recorder->active = false;
    GPX* gpx = &(recorder->gpx);
    lv_fs_file_t* file_p = &(recorder->file);

    Recorder_BufferedWrite(recorder, gpx->getTrakSegClose().c_str());
    Recorder_BufferedWrite(recorder, gpx->getTrakClose().c_str());
    Recorder_BufferedWrite(recorder, gpx->getClose().c_str());

    // 停止录制是唯一真正"不能再拖"的时刻——不管缓冲区里还剩多少没写、
    // 上次 sync 是多久之前，这里必须把剩下的内容全部落盘并真正 sync()，
    // 不然缓冲区里攒着的最后一批轨迹点会随着文件关闭而丢失。
    Recorder_FlushBuffer(recorder, true);

    SdFile* sdFile = (SdFile*)(file_p->file_d);
    if (sdFile != NULL)
    {
        sdFile->sync();
    }

    lv_fs_close(file_p);

    LV_LOG_USER("Track record end");
}

static int onNotify(Recorder_t* recorder, Recorder_Info_t* info)
{
    switch (info->cmd)
    {
    case RECORDER_CMD_START:
        Recorder_RecStart(recorder, info->time);
        break;
    case RECORDER_CMD_PAUSE:
        recorder->active = false;
        // 暂停可能会持续很久，把缓冲区里已经攒的内容先落盘（不用像
        // RecStop 那样强制 sync()，只是不让数据一直悬在内存里）。
        Recorder_FlushBuffer(recorder);
        LV_LOG_USER("Track record pause");
        break;
    case RECORDER_CMD_CONTINUE:
        LV_LOG_USER("Track record continue");
        recorder->active = true;
        break;
    case RECORDER_CMD_STOP:
        Recorder_RecStop(recorder);
        break;
    }

    TrackFilter_Info_t tfInfo;
    DATA_PROC_INIT_STRUCT(tfInfo);
    tfInfo.cmd = (TrackFilter_Cmd_t)info->cmd;

    return recorder->account->Notify("TrackFilter", &tfInfo, sizeof(tfInfo));
}

static int onEvent(Account* account, Account::EventParam_t* param)
{
    Account::ResCode_t res = Account::RES_UNKNOW;
    Recorder_t* recorder = (Recorder_t*)account->UserData;;

    switch (param->event)
    {
    case Account::EVENT_PUB_PUBLISH:
        if (param->size == sizeof(HAL::GPS_Info_t))
        {
            if (recorder->active)
            {
                Recorder_RecPoint(recorder, (HAL::GPS_Info_t*)param->data_p);
            }
            res = Account::RES_OK;
        }
        else
        {
            res = Account::RES_SIZE_MISMATCH;
        }
        break;

    case Account::EVENT_SUB_PULL:
        if (param->size == sizeof(Recorder_Info_t))
        {
            memcpy(param->data_p, &(recorder->recInfo), param->size);
        }
        else
        {
            res = Account::RES_SIZE_MISMATCH;
        }
        break;

    case Account::EVENT_NOTIFY:
        if (param->size == sizeof(Recorder_Info_t))
        {
            onNotify(recorder, (Recorder_Info_t*)param->data_p);
            res = Account::RES_OK;
        }
        else
        {
            res = Account::RES_SIZE_MISMATCH;
        }
        break;

    default:
        break;
    }

    return res;
}

static Recorder_t* s_pRecorderInstance = nullptr;

void DataProc::Recorder_PeriodicTask()
{
    if (s_pRecorderInstance == nullptr || !s_pRecorderInstance->active)
    {
        return;
    }

    // 后台平滑按 512 字节整扇区对齐刷新缓冲区
    if (s_pRecorderInstance->writeBufLen >= SD_SECTOR_SIZE)
    {
        Recorder_FlushBuffer(s_pRecorderInstance, false);
    }

    // 后台 30 秒定时集中 sync()
    uint32_t now = lv_tick_get();
    if (lv_tick_elaps(s_pRecorderInstance->lastSyncTick) >= RECORDER_SYNC_INTERVAL_MS)
    {
        if (s_pRecorderInstance->writeBufLen > 0)
        {
            Recorder_FlushBuffer(s_pRecorderInstance, true);
        }

        SdFile* sdFile = (SdFile*)(s_pRecorderInstance->file.file_d);
        if (sdFile != NULL)
        {
            bool syncResult = sdFile->sync();
            if (syncResult)
            {
                LV_LOG_USER("Track file synced successfully");
            }
            else
            {
                LV_LOG_WARN("Track file sync failed");
            }
        }
        s_pRecorderInstance->lastSyncTick = now;
    }
}

DATA_PROC_INIT_DEF(Recorder)
{
    static Recorder_t recorder;
    memset(&recorder.recInfo, 0, sizeof(recorder.recInfo));
    memset(&recorder.file, 0, sizeof(recorder.file));
    recorder.active = false;
    recorder.account = account;
    recorder.writeBufLen = 0;
    recorder.lastSyncTick = lv_tick_get();
    account->UserData = &recorder;
    s_pRecorderInstance = &recorder;

    account->Subscribe("GPS");
    account->Subscribe("Clock");
    account->Subscribe("TrackFilter");
    account->SetEventCallback(onEvent);
}
