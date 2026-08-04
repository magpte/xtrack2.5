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
#define RECORDER_WRITE_BUF_SIZE     8192
#define RECORDER_SYNC_INTERVAL_MS   60000

typedef struct
{
    GPX gpx;
    Recorder_Info_t recInfo;
    lv_fs_file_t file;
    bool active;
    Account* account;

    char writeBuf[RECORDER_WRITE_BUF_SIZE];
    uint32_t writeBufLen;      // 缓冲区里已经攒了多少字节，还没写文件
    uint32_t lastSyncTick;     // 上一次真正 sync() 落盘的时刻（lv_tick_get()）
} Recorder_t;

// 把缓冲区里已经攒的内容真正写进文件（一次 lv_fs_write，不是每个点一次）。
// 只在这里才会真正触碰文件系统的写入接口。
static lv_fs_res_t Recorder_FlushBuffer(Recorder_t* recorder)
{
    if (recorder->writeBufLen == 0)
    {
        return LV_FS_RES_OK;  // 没什么好写的，不做无意义的空写入
    }

    lv_fs_res_t res = lv_fs_write(
                           &(recorder->file),
                           recorder->writeBuf,
                           recorder->writeBufLen,
                           NULL
                       );

    recorder->writeBufLen = 0;
    return res;
}

// 把一段字符串追加进缓冲区；如果这段内容本身就装不下（或者加进去会
// 超出缓冲区容量），先把缓冲区里已有的内容落盘腾地方，再重新尝试。
// 单次字符串长度理论上不会超过缓冲区容量（GPX 的开头/结尾标签、单个
// trkpt 都远小于 1KB），如果真的遇到异常长的字符串，直接绕过缓冲区
// 直写，不丢数据、只是退化成跟原来一样的直接写入。
static lv_fs_res_t Recorder_BufferedWrite(Recorder_t* recorder, const char* str)
{
    uint32_t len = (uint32_t)strlen(str);

    if (len >= RECORDER_WRITE_BUF_SIZE)
    {
        Recorder_FlushBuffer(recorder);
        return lv_fs_write(&(recorder->file), str, len, NULL);
    }

    if (recorder->writeBufLen + len > RECORDER_WRITE_BUF_SIZE)
    {
        lv_fs_res_t res = Recorder_FlushBuffer(recorder);
        if (res != LV_FS_RES_OK)
        {
            return res;
        }
    }

    memcpy(recorder->writeBuf + recorder->writeBufLen, str, len);
    recorder->writeBufLen += len;
    return LV_FS_RES_OK;
}

// 落盘之后，视情况决定要不要真正 sync()——按时间判断，不是按点数/
// 次数，原因见上面 RECORDER_SYNC_INTERVAL_MS 的注释。
static void Recorder_MaybeSync(Recorder_t* recorder)
{
    uint32_t now = lv_tick_get();
    if (lv_tick_elaps(recorder->lastSyncTick) < RECORDER_SYNC_INTERVAL_MS)
    {
        return;
    }

    SdFile* sdFile = (SdFile*)(recorder->file.file_d);
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
    recorder->lastSyncTick = now;
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
    Recorder_MaybeSync(recorder);
}

static void Recorder_RecStart(Recorder_t* recorder, uint16_t time)
{
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
    Recorder_FlushBuffer(recorder);

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

    account->Subscribe("GPS");
    account->Subscribe("Clock");
    account->Subscribe("TrackFilter");
    account->SetEventCallback(onEvent);
}
