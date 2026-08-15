/*
 * TileBundleFS.cpp
 *
 * See TileBundleFS.h for background. Bundle file format (must match
 * tile_bundle.py exactly):
 *   4 bytes   magic "TBND"
 *   2 bytes   blockSize
 *   4 bytes   blockX
 *   4 bytes   blockY
 *   index table: blockSize*blockSize entries, 8 bytes each
 *       4 bytes offset (into the data section; 0xFFFFFFFF = tile absent)
 *       4 bytes length
 *       entry for (localX, localY) is at index localY*blockSize+localX
 *   data section: concatenated tile bytes
 *
 * Features a 96KB static SRAM Tile LRU Cache for zero SD card I/O reads.
 */
#include "TileBundleFS.h"
#include "Common/HAL/HAL.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define BUNDLE_HEADER_SIZE 14
#define ABSENT_OFFSET 0xFFFFFFFFu

#if defined(__GNUC__) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
#  define ALIGN_WORD4 __attribute__((aligned(4)))
#else
#  define ALIGN_WORD4
#endif

// Legacy TileBundleFS pool deactivated to prevent dual 96KB allocation
#define TILE_CACHE_TOTAL_POOL_SIZE (1 * 1024)
#define TILE_CACHE_MAX_ENTRIES     1
#define MAX_CACHEABLE_TILE_SIZE    (512)

typedef struct
{
    char path[64];
    uint32_t pool_offset;
    uint32_t length;
    uint32_t last_used_tick;
    bool valid;
} TileCacheEntry_t;

static uint8_t s_tile_cache_pool[TILE_CACHE_TOTAL_POOL_SIZE] ALIGN_WORD4;
static TileCacheEntry_t s_cache_entries[TILE_CACHE_MAX_ENTRIES];
static uint32_t s_cache_used_bytes = 0;
static uint32_t s_cache_hits = 0;
static uint32_t s_cache_misses = 0;

typedef struct
{
    uint32_t tile_start;       // Absolute byte offset within bundle file
    uint32_t tile_length;      // Tile byte length
    uint32_t cursor;           // Read position, 0..tile_length
    const uint8_t* cache_data; // Non-NULL if read from 96KB SRAM pool
} TileBundleFile_t;

// --- Shared, persistent bundle file handle ---
static lv_fs_file_t s_shared_bundle_file;
static char s_shared_bundle_path[96] = "";
static bool s_shared_bundle_valid = false;

// --- LRU Cache Internal Functions ---

static void tile_cache_compact(void)
{
    uint32_t write_offset = 0;
    for (int i = 0; i < TILE_CACHE_MAX_ENTRIES; i++)
    {
        if (s_cache_entries[i].valid)
        {
            if (s_cache_entries[i].pool_offset != write_offset)
            {
                memmove(&s_tile_cache_pool[write_offset],
                        &s_tile_cache_pool[s_cache_entries[i].pool_offset],
                        s_cache_entries[i].length);
                s_cache_entries[i].pool_offset = write_offset;
            }
            write_offset += s_cache_entries[i].length;
        }
    }
    s_cache_used_bytes = write_offset;
}

static void tile_cache_evict_one(void)
{
    int oldest_idx = -1;
    uint32_t oldest_tick = 0xFFFFFFFFu;

    for (int i = 0; i < TILE_CACHE_MAX_ENTRIES; i++)
    {
        if (s_cache_entries[i].valid && s_cache_entries[i].last_used_tick < oldest_tick)
        {
            oldest_tick = s_cache_entries[i].last_used_tick;
            oldest_idx = i;
        }
    }

    if (oldest_idx >= 0)
    {
        s_cache_entries[oldest_idx].valid = false;
        tile_cache_compact();
    }
}

static int tile_cache_find(const char* path)
{
    for (int i = 0; i < TILE_CACHE_MAX_ENTRIES; i++)
    {
        if (s_cache_entries[i].valid && strcmp(s_cache_entries[i].path, path) == 0)
        {
            s_cache_entries[i].last_used_tick = lv_tick_get();
            s_cache_hits++;
            uint32_t total = s_cache_hits + s_cache_misses;
            if (total % 10 == 0)
            {
                char log_buf[128];
                snprintf(log_buf, sizeof(log_buf),
                         "[TileLRU] HIT! hits=%u, misses=%u, ratio=%u%%, pool_used=%uKB\r\n",
                         (unsigned)s_cache_hits, (unsigned)s_cache_misses,
                         (unsigned)((s_cache_hits * 100) / total), (unsigned)(s_cache_used_bytes / 1024));
                LV_LOG_USER("%s", log_buf);
            }
            return i;
        }
    }
    s_cache_misses++;
    uint32_t total = s_cache_hits + s_cache_misses;
    if (total % 10 == 0)
    {
        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf),
                 "[TileLRU] MISS! hits=%u, misses=%u, ratio=%u%%, pool_used=%uKB\r\n",
                 (unsigned)s_cache_hits, (unsigned)s_cache_misses,
                 (unsigned)((s_cache_hits * 100) / total), (unsigned)(s_cache_used_bytes / 1024));
        LV_LOG_USER("%s", log_buf);
    }
    return -1;
}

static uint8_t* tile_cache_alloc_slot(const char* path, uint32_t length, int* out_slot_idx)
{
    if (length == 0 || length > MAX_CACHEABLE_TILE_SIZE || length > TILE_CACHE_TOTAL_POOL_SIZE)
    {
        return NULL;
    }

    // Evict oldest LRU entries until enough room is available
    while (s_cache_used_bytes + length > TILE_CACHE_TOTAL_POOL_SIZE)
    {
        tile_cache_evict_one();
    }

    int slot_idx = -1;
    for (int i = 0; i < TILE_CACHE_MAX_ENTRIES; i++)
    {
        if (!s_cache_entries[i].valid)
        {
            slot_idx = i;
            break;
        }
    }

    if (slot_idx < 0)
    {
        tile_cache_evict_one();
        for (int i = 0; i < TILE_CACHE_MAX_ENTRIES; i++)
        {
            if (!s_cache_entries[i].valid)
            {
                slot_idx = i;
                break;
            }
        }
    }

    if (slot_idx < 0)
    {
        return NULL;
    }

    TileCacheEntry_t* entry = &s_cache_entries[slot_idx];
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
    entry->pool_offset = s_cache_used_bytes;
    entry->length = length;
    entry->last_used_tick = lv_tick_get();
    entry->valid = true;

    s_cache_used_bytes += length;
    *out_slot_idx = slot_idx;

    return &s_tile_cache_pool[entry->pool_offset];
}

void TileBundleFS_GetCacheStats(uint32_t* hits, uint32_t* misses, uint32_t* used_bytes)
{
    if (hits)       *hits = s_cache_hits;
    if (misses)     *misses = s_cache_misses;
    if (used_bytes) *used_bytes = s_cache_used_bytes;
}

void TileBundleFS_ClearCache(void)
{
    memset(s_cache_entries, 0, sizeof(s_cache_entries));
    s_cache_used_bytes = 0;
}

// Parses ".../<level>/<tileX>/<tileY>.rle" out of `path`, computes which
// bundle block (tileX,tileY) falls into, and writes the real bundle
// file's path (same prefix, "<level>/<blockX>_<blockY>.tbnd") into `bundle_path_out`.
static bool parse_virtual_path(const char* path, char* bundle_path_out, size_t bundle_path_max,
                                int* local_x, int* local_y)
{
    char buf[96];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf))
    {
        return false;
    }
    strcpy(buf, path);

    char* slash3 = strrchr(buf, '/');  // just before "<tileY>.rle"
    if (!slash3)
    {
        return false;
    }
    *slash3 = '\0';
    char* tileY_str = slash3 + 1;

    char* slash2 = strrchr(buf, '/');  // just before "<tileX>"
    if (!slash2)
    {
        return false;
    }
    *slash2 = '\0';
    char* tileX_str = slash2 + 1;

    char* slash1 = strrchr(buf, '/');  // just before "<level>"
    if (!slash1)
    {
        return false;
    }
    *slash1 = '\0';
    char* level_str = slash1 + 1;

    int tile_x = atoi(tileX_str);
    int tile_y = atoi(tileY_str);  // atoi stops at the ".rle" suffix on its own
    int level = atoi(level_str);

    int block_x = tile_x / TILE_BUNDLE_BLOCK_SIZE;
    int block_y = tile_y / TILE_BUNDLE_BLOCK_SIZE;
    *local_x = tile_x % TILE_BUNDLE_BLOCK_SIZE;
    *local_y = tile_y % TILE_BUNDLE_BLOCK_SIZE;

    int written = snprintf(bundle_path_out, bundle_path_max, "%s/%d/%d_%d.tbnd",
                            buf, level, block_x, block_y);
    return written > 0 && (size_t)written < bundle_path_max;
}

static bool ensure_shared_bundle_open(const char* bundle_path)
{
    if (s_shared_bundle_valid && strcmp(s_shared_bundle_path, bundle_path) == 0)
    {
        return true;
    }

    if (s_shared_bundle_valid)
    {
        lv_fs_close(&s_shared_bundle_file);
        s_shared_bundle_valid = false;
    }

    if (lv_fs_open(&s_shared_bundle_file, bundle_path, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }

    strncpy(s_shared_bundle_path, bundle_path, sizeof(s_shared_bundle_path) - 1);
    s_shared_bundle_path[sizeof(s_shared_bundle_path) - 1] = '\0';
    s_shared_bundle_valid = true;
    return true;
}

static void* bundle_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);

    if (mode != LV_FS_MODE_RD)
    {
        LV_LOG_WARN("TileBundle: write mode not supported (%s)", path);
        return NULL;
    }

    // 1. Check 96KB Tile LRU Cache Hit
    int cache_idx = tile_cache_find(path);
    if (cache_idx >= 0)
    {
        TileBundleFile_t* f = (TileBundleFile_t*)lv_mem_alloc(sizeof(TileBundleFile_t));
        if (f == NULL)
        {
            LV_LOG_ERROR("TileBundle: out of memory");
            return NULL;
        }
        f->tile_start = 0;
        f->tile_length = s_cache_entries[cache_idx].length;
        f->cursor = 0;
        f->cache_data = &s_tile_cache_pool[s_cache_entries[cache_idx].pool_offset];
        return f;
    }

    // 2. Cache Miss: Open from SD Card Bundle File
    char bundle_path[96];
    int local_x = 0, local_y = 0;
    if (!parse_virtual_path(path, bundle_path, sizeof(bundle_path), &local_x, &local_y))
    {
        LV_LOG_WARN("TileBundle: could not parse path %s", path);
        return NULL;
    }

    if (!ensure_shared_bundle_open(bundle_path))
    {
        LV_LOG_WARN("TileBundle: could not open bundle file %s (from virtual path %s)", bundle_path, path);
        return NULL;
    }

    TileBundleFile_t* f = (TileBundleFile_t*)lv_mem_alloc(sizeof(TileBundleFile_t));
    if (f == NULL)
    {
        LV_LOG_ERROR("TileBundle: out of memory");
        return NULL;
    }

    uint32_t index_pos = BUNDLE_HEADER_SIZE
                          + (uint32_t)(local_y * TILE_BUNDLE_BLOCK_SIZE + local_x) * 8;

    uint8_t entry[8];
    uint32_t br = 0;
    if (lv_fs_seek(&s_shared_bundle_file, index_pos, LV_FS_SEEK_SET) != LV_FS_RES_OK
        || lv_fs_read(&s_shared_bundle_file, entry, sizeof(entry), &br) != LV_FS_RES_OK
        || br != sizeof(entry))
    {
        LV_LOG_WARN("TileBundle: index read failed in %s", bundle_path);
        lv_mem_free(f);
        return NULL;
    }

    uint32_t offset = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8)
                       | ((uint32_t)entry[2] << 16) | ((uint32_t)entry[3] << 24);
    uint32_t length = (uint32_t)entry[4] | ((uint32_t)entry[5] << 8)
                       | ((uint32_t)entry[6] << 16) | ((uint32_t)entry[7] << 24);

    if (offset == ABSENT_OFFSET || length == 0)
    {
        lv_mem_free(f);
        return NULL;
    }

    uint32_t data_section_start = BUNDLE_HEADER_SIZE
                                   + (uint32_t)TILE_BUNDLE_BLOCK_SIZE * TILE_BUNDLE_BLOCK_SIZE * 8;
    f->tile_start = data_section_start + offset;
    f->tile_length = length;
    f->cursor = 0;
    f->cache_data = NULL;

    if (lv_fs_seek(&s_shared_bundle_file, f->tile_start, LV_FS_SEEK_SET) != LV_FS_RES_OK)
    {
        LV_LOG_WARN("TileBundle: seek failed in %s", bundle_path);
        lv_mem_free(f);
        return NULL;
    }

    // Try storing newly loaded tile into 96KB LRU Cache
    int slot_idx = -1;
    uint8_t* cached_buf = tile_cache_alloc_slot(path, length, &slot_idx);
    if (cached_buf != NULL && slot_idx >= 0)
    {
        uint32_t read_bytes = 0;
        if (lv_fs_read(&s_shared_bundle_file, cached_buf, length, &read_bytes) == LV_FS_RES_OK
            && read_bytes == length)
        {
            f->cache_data = cached_buf;
        }
        else
        {
            s_cache_entries[slot_idx].valid = false;
            tile_cache_compact();
            lv_fs_seek(&s_shared_bundle_file, f->tile_start, LV_FS_SEEK_SET);
        }
    }

    return f;
}

static lv_fs_res_t bundle_fs_close(lv_fs_drv_t* drv, void* file_p)
{
    LV_UNUSED(drv);
    TileBundleFile_t* f = (TileBundleFile_t*)file_p;
    if (f)
    {
        lv_mem_free(f);
    }
    return LV_FS_RES_OK;
}

static lv_fs_res_t bundle_fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
    LV_UNUSED(drv);
    TileBundleFile_t* f = (TileBundleFile_t*)file_p;

    uint32_t remaining = f->tile_length - f->cursor;
    uint32_t to_read = (btr < remaining) ? btr : remaining;

    if (f->cache_data != NULL)
    {
        // 96KB SRAM Cache Hit: Fast Memory Copy
        memcpy(buf, f->cache_data + f->cursor, to_read);
        *br = to_read;
        f->cursor += to_read;
        return LV_FS_RES_OK;
    }

    // Uncached Read from SD Card
    lv_fs_res_t res = lv_fs_read(&s_shared_bundle_file, buf, to_read, br);
    if (res == LV_FS_RES_OK)
    {
        f->cursor += *br;
    }
    return res;
}

static lv_fs_res_t bundle_fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    TileBundleFile_t* f = (TileBundleFile_t*)file_p;

    uint32_t new_cursor;
    if (whence == LV_FS_SEEK_SET)
    {
        new_cursor = pos;
    }
    else if (whence == LV_FS_SEEK_CUR)
    {
        new_cursor = f->cursor + pos;
    }
    else
    {
        new_cursor = f->tile_length + pos;
    }

    if (new_cursor > f->tile_length)
    {
        new_cursor = f->tile_length;
    }
    f->cursor = new_cursor;

    if (f->cache_data != NULL)
    {
        return LV_FS_RES_OK;
    }

    return lv_fs_seek(&s_shared_bundle_file, f->tile_start + f->cursor, LV_FS_SEEK_SET);
}

static lv_fs_res_t bundle_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p)
{
    LV_UNUSED(drv);
    TileBundleFile_t* f = (TileBundleFile_t*)file_p;
    *pos_p = f->cursor;
    return LV_FS_RES_OK;
}

void TileBundleFS_Init(void)
{
    TileBundleFS_ClearCache();

    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = TILE_BUNDLE_DRIVE_LETTER;
    drv.open_cb = bundle_fs_open;
    drv.close_cb = bundle_fs_close;
    drv.read_cb = bundle_fs_read;
    drv.seek_cb = bundle_fs_seek;
    drv.tell_cb = bundle_fs_tell;
    lv_fs_drv_register(&drv);
}
