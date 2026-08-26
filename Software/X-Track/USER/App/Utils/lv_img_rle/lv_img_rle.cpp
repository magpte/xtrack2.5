/*
 * lv_img_rle.cpp
 *
 * High-Performance Micro-Chunk On-Demand Streaming Engine for ZST2 Lossless Tiles.
 * Part of X-Track 2.5 Map System.
 *
 * Features:
 *   1. Adaptive ZST2 (3-Tier Micro-Palette + G-Color Decorrelation + Zstd-1/FSE):
 *      - Tier 1: 4-bit packed micro-palette (≤16 colors, 100% lossless).
 *      - Tier 2: 8-bit compact micro-palette (17~256 colors, 100% lossless).
 *      - Tier 3: 16-bit Green-decorrelation linear stream (>256 colors, 100% lossless).
 *   2. Micro-Chunk On-Demand Streaming: reads and decodes only the required 32-row chunk (1~4KB) from SD.
 *   3. Zero Heap Allocations: 100% static memory (Zero malloc / free).
 *   4. Ultra-Low Static RAM: 16 descriptors + 16KB scratchpad + 16KB chunk buffer + 24KB Zstd DCtx.
 *   5. High-Throughput Rendering: FastMemcpy for Tier 3, 4-pixel parallel 32-bit burst lookup for Tier 1/2.
 */

#include "lv_img_rle.h"
#include "zstd_decompress.h"
extern "C" {
#include "zstd_decompress.c"
}
#include "Config/Config.h"
#include "Common/HAL/HAL.h"
#include "HAL/FastMemcpy.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#define MY_CLASS &lv_img_rle_class

// ---- Bundle constants (must match tile_bundle.py) -----------------------
#define BUNDLE_MAGIC              "TBND"
#define BUNDLE_HEADER_SIZE        14u
#define BUNDLE_BLOCK_SIZE         100
#define ABSENT_OFFSET             0xFFFFFFFFu

// ---- ZST2 Tile constants (must match tile_zstd_encode.py) ---------------
#define ZST_MAGIC                 "ZST2"
#define TILE_HEADER_SIZE          16u
#define TILE_MAX_PALETTE          256
#define TILE_MAX_CHUNKS           8
#define TILE_CHUNK_ROWS           32
#define TILE_SIZE                 256
#define TILE_MAX_DESCRIPTORS      16

// ---- LVGL drive letter for SD card --------------------------------------
#define TILE_SD_DRIVE_LETTER      '/'

#define TILE_SCRATCH_BUF_SIZE     (TILE_SIZE * TILE_CHUNK_ROWS * 2) // 16384 bytes (supports 16-bit RGB565 chunks)
#define TILE_CHUNK_FETCH_BUF_SIZE (TILE_SCRATCH_BUF_SIZE + 64)      // Max compressed chunk size

// =========================================================================
// Diagnostic Logging (Outputs to SD Card /system.log & Serial console)
// =========================================================================
#define map_log(fmt, ...) HAL::SysLog_Write("[MAP] " fmt, ##__VA_ARGS__)

// =========================================================================
// Shared bundle file handle
// =========================================================================
static lv_fs_file_t s_bundle_file;
static char         s_bundle_path[96] = "";
static bool         s_bundle_valid    = false;

static bool tile_read_bytes(uint32_t abs_pos, void* buf, uint32_t len, uint32_t* br)
{
    return (lv_fs_seek(&s_bundle_file, abs_pos, LV_FS_SEEK_SET) == LV_FS_RES_OK &&
            lv_fs_read(&s_bundle_file, buf, len, br) == LV_FS_RES_OK);
}

// =========================================================================
// Tile Descriptors (Metadata & Layout)
// =========================================================================
typedef struct {
    char        path[64];
    char        bundle_path[96];
    uint32_t    last_access_tick;

    uint32_t    tile_start;
    uint32_t    tile_length;
    uint32_t    payload_start;
    uint32_t    payload_length;

    uint16_t    width;
    uint16_t    height;
    uint16_t    chunk_interval;
    uint16_t    chunk_count;

    uint32_t    chunk_offsets[TILE_MAX_CHUNKS]; // offsets from payload start to each chunk

    bool        valid;
} TileDesc_t;

static TileDesc_t s_descriptors[TILE_MAX_DESCRIPTORS];
static uint8_t    s_comp_chunk_buf[TILE_CHUNK_FETCH_BUF_SIZE] __attribute__((aligned(8))); // 16.5 KB static buffer
static uint8_t    s_scratch_buf[TILE_SCRATCH_BUF_SIZE] __attribute__((aligned(8)));        // 16 KB static uncompressed pixel buffer

// MRU decompression cache: eliminates redundant SD reads & decompression
static struct {
    const TileDesc_t* desc;
    int               chunk_idx;
    uint8_t           chunk_tier;                         // Tier mode (1=4bit, 2=8bit, 3=16bit G-dec)
    lv_color_t        micro_palette[TILE_MAX_PALETTE];    // Pre-baked micro-palette for active chunk
} s_mru_chunk = { NULL, -1, 0 };

// =========================================================================
// Cache Management API
// =========================================================================

void lv_img_rle_cache_init()
{
    memset(s_descriptors, 0, sizeof(s_descriptors));
    s_mru_chunk.desc = NULL;
    s_mru_chunk.chunk_idx = -1;
    s_mru_chunk.chunk_tier = 0;
    zstd_decompress_init();
    map_log("Cache init: Micro-chunk Streaming (ZST2 Engine, 16 Descs, 16KB Scratch, MRU Opt)");
}

void lv_img_rle_cache_deinit()
{
    s_mru_chunk.desc = NULL;
    s_mru_chunk.chunk_idx = -1;
    s_mru_chunk.chunk_tier = 0;

    for (int i = 0; i < TILE_MAX_DESCRIPTORS; i++)
    {
        s_descriptors[i].valid = false;
        s_descriptors[i].path[0] = '\0';
    }

    if (s_bundle_valid)
    {
        lv_fs_close(&s_bundle_file);
        s_bundle_valid = false;
        s_bundle_path[0] = '\0';
    }
}

static inline lv_color_t rgb565_to_lv_color(uint16_t c)
{
#if LV_COLOR_16_SWAP == 1
    uint16_t swapped = (uint16_t)((c << 8) | (c >> 8));
    return *(lv_color_t*)&swapped;
#else
    return *(lv_color_t*)&c;
#endif
}

// Find descriptor by tile path
static TileDesc_t* cache_find(const char* path)
{
    uint32_t now = lv_tick_get();
    for (int i = 0; i < TILE_MAX_DESCRIPTORS; i++)
    {
        if (s_descriptors[i].valid && strcmp(s_descriptors[i].path, path) == 0)
        {
            s_descriptors[i].last_access_tick = now;
            return &s_descriptors[i];
        }
    }
    return NULL;
}

// Allocate or evict LRU descriptor slot
static TileDesc_t* get_or_allocate_desc(const char* path)
{
    // 1. Check if already exists
    for (int i = 0; i < TILE_MAX_DESCRIPTORS; i++)
    {
        if (s_descriptors[i].valid && strcmp(s_descriptors[i].path, path) == 0)
        {
            s_descriptors[i].last_access_tick = lv_tick_get();
            return &s_descriptors[i];
        }
    }

    // 2. Find empty slot
    for (int i = 0; i < TILE_MAX_DESCRIPTORS; i++)
    {
        if (!s_descriptors[i].valid)
        {
            memset(&s_descriptors[i], 0, sizeof(TileDesc_t));
            strncpy(s_descriptors[i].path, path, sizeof(s_descriptors[i].path) - 1);
            s_descriptors[i].last_access_tick = lv_tick_get();
            return &s_descriptors[i];
        }
    }

    // 3. Evict oldest LRU descriptor
    uint32_t oldest_tick = 0xFFFFFFFFu;
    int evict_idx = 0;
    for (int i = 0; i < TILE_MAX_DESCRIPTORS; i++)
    {
        if (s_descriptors[i].last_access_tick < oldest_tick)
        {
            oldest_tick = s_descriptors[i].last_access_tick;
            evict_idx = i;
        }
    }

    if (s_mru_chunk.desc == &s_descriptors[evict_idx])
    {
        s_mru_chunk.desc = NULL;
        s_mru_chunk.chunk_idx = -1;
    }

    memset(&s_descriptors[evict_idx], 0, sizeof(TileDesc_t));
    strncpy(s_descriptors[evict_idx].path, path, sizeof(s_descriptors[evict_idx].path) - 1);
    s_descriptors[evict_idx].last_access_tick = lv_tick_get();
    return &s_descriptors[evict_idx];
}

// =========================================================================
// Path Parsing and Bundle Lookup
// =========================================================================

static bool parse_tile_path(const char* src,
                            char* prefix_out, size_t prefix_len,
                            uint32_t* level_out,
                            uint32_t* tile_x_out,
                            uint32_t* tile_y_out)
{
    if (src == NULL) return false;

    const char* p = strchr(src, ':');
    p = (p != NULL) ? (p + 1) : src;
    while (*p == '/' || *p == '\\') p++;

    // Format: <prefix>/<level>/<tileX>/<tileY>.<ext>
    const char* slash3 = strrchr(p, '/');
    if (!slash3) slash3 = strrchr(p, '\\');
    if (!slash3) return false;

    char* endptr = NULL;
    unsigned long tile_y = strtoul(slash3 + 1, &endptr, 10);
    if (endptr == slash3 + 1) return false;

    const char* p2 = slash3 - 1;
    while (p2 > p && *p2 != '/' && *p2 != '\\') p2--;
    if (p2 == p) return false;
    unsigned long tile_x = strtoul(p2 + 1, &endptr, 10);
    if (endptr == p2 + 1) return false;

    const char* p3 = p2 - 1;
    while (p3 > p && *p3 != '/' && *p3 != '\\') p3--;
    const char* level_str = (*p3 == '/' || *p3 == '\\') ? (p3 + 1) : p3;
    unsigned long level = strtoul(level_str, &endptr, 10);
    if (endptr == level_str) return false;

    // Reject out-of-world coordinates
    if (level > 24) return false;
    uint32_t max_tiles = (level >= 31) ? 0xFFFFFFFFu : (1u << level);
    if (tile_x >= max_tiles || tile_y >= max_tiles)
    {
        return false;
    }

    // Extract directory prefix (e.g. "MAPRB" or "MAP")
    if (prefix_out && prefix_len > 0)
    {
        size_t plen = (p3 > p) ? (size_t)(p3 - p) : 0;
        if (plen >= prefix_len) plen = prefix_len - 1;
        if (plen > 0)
        {
            memcpy(prefix_out, p, plen);
            prefix_out[plen] = '\0';
        }
        else
        {
            strncpy(prefix_out, "MAPRB", prefix_len - 1);
            prefix_out[prefix_len - 1] = '\0';
        }
    }

    *level_out  = (uint32_t)level;
    *tile_x_out = (uint32_t)tile_x;
    *tile_y_out = (uint32_t)tile_y;
    return true;
}

static bool ensure_bundle_open(const char* bundle_path)
{
    if (bundle_path == NULL || bundle_path[0] == '\0') return false;

    if (s_bundle_valid && strcmp(s_bundle_path, bundle_path) == 0)
    {
        return true;
    }

    if (s_bundle_valid)
    {
        lv_fs_close(&s_bundle_file);
        s_bundle_valid = false;
        s_bundle_path[0] = '\0';
    }

    if (lv_fs_open(&s_bundle_file, bundle_path, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }

    size_t len = strlen(bundle_path);
    if (len >= sizeof(s_bundle_path)) len = sizeof(s_bundle_path) - 1;
    memcpy(s_bundle_path, bundle_path, len);
    s_bundle_path[len] = '\0';
    s_bundle_valid = true;
    return true;
}

static bool open_tile(const char* src, uint32_t* tile_start_out, uint32_t* tile_length_out)
{
    char prefix[32] = "MAPRB";
    uint32_t level = 0, tile_x = 0, tile_y = 0;
    if (!parse_tile_path(src, prefix, sizeof(prefix), &level, &tile_x, &tile_y))
    {
        return false;
    }

    uint32_t block_x = tile_x / BUNDLE_BLOCK_SIZE;
    uint32_t block_y = tile_y / BUNDLE_BLOCK_SIZE;
    uint32_t local_x = tile_x % BUNDLE_BLOCK_SIZE;
    uint32_t local_y = tile_y % BUNDLE_BLOCK_SIZE;

    // Candidate bundle paths to probe on SD card
    char candidate[8][96];
    int cand_count = 0;

    // 1. /:<prefix>/<level>/<blockX>_<blockY>.tbnd and /<prefix>/...
    snprintf(candidate[cand_count++], sizeof(candidate[0]),
             "%c:%s/%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, prefix, (unsigned)level, (unsigned)block_x, (unsigned)block_y);
    snprintf(candidate[cand_count++], sizeof(candidate[0]),
             "/%s/%u/%u_%u.tbnd", prefix, (unsigned)level, (unsigned)block_x, (unsigned)block_y);

    // 2. /:MAP/<level>/<blockX>_<blockY>.tbnd and /MAP/...
    if (strcmp(prefix, "MAP") != 0)
    {
        snprintf(candidate[cand_count++], sizeof(candidate[0]),
                 "%c:MAP/%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, (unsigned)level, (unsigned)block_x, (unsigned)block_y);
        snprintf(candidate[cand_count++], sizeof(candidate[0]),
                 "/MAP/%u/%u_%u.tbnd", (unsigned)level, (unsigned)block_x, (unsigned)block_y);
    }

    // 3. /:MAPRB/<level>/<blockX>_<blockY>.tbnd and /MAPRB/...
    if (strcmp(prefix, "MAPRB") != 0)
    {
        snprintf(candidate[cand_count++], sizeof(candidate[0]),
                 "%c:MAPRB/%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, (unsigned)level, (unsigned)block_x, (unsigned)block_y);
        snprintf(candidate[cand_count++], sizeof(candidate[0]),
                 "/MAPRB/%u/%u_%u.tbnd", (unsigned)level, (unsigned)block_x, (unsigned)block_y);
    }

    // 4. /:<level>/<blockX>_<blockY>.tbnd and /<level>/...
    snprintf(candidate[cand_count++], sizeof(candidate[0]),
             "%c:%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, (unsigned)level, (unsigned)block_x, (unsigned)block_y);
    snprintf(candidate[cand_count++], sizeof(candidate[0]),
             "/%u/%u_%u.tbnd", (unsigned)level, (unsigned)block_x, (unsigned)block_y);

    bool opened = false;
    for (int i = 0; i < cand_count; i++)
    {
        if (ensure_bundle_open(candidate[i]))
        {
            opened = true;
            break;
        }
    }

    if (!opened)
    {
        map_log("open_tile: Failed to find bundle for src='%s' (tried %s, etc.)", src, candidate[0]);
        return false;
    }

    // Read Bundle Header
    uint8_t hdr[BUNDLE_HEADER_SIZE];
    uint32_t br = 0;
    if (!tile_read_bytes(0, hdr, BUNDLE_HEADER_SIZE, &br) || br != BUNDLE_HEADER_SIZE)
    {
        map_log("open_tile: Failed to read header for bundle '%s'", s_bundle_path);
        return false;
    }

    if (memcmp(hdr, BUNDLE_MAGIC, 4) != 0)
    {
        map_log("open_tile: Invalid bundle magic in '%s'", s_bundle_path);
        return false;
    }

    uint16_t blk_size = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
    if (blk_size != BUNDLE_BLOCK_SIZE)
    {
        map_log("open_tile: Bundle blk_size=%u != %u in '%s'", blk_size, BUNDLE_BLOCK_SIZE, s_bundle_path);
        return false;
    }

    // Read 8-byte (offset, length) from bundle index table
    uint32_t tile_idx = local_y * (uint32_t)blk_size + local_x;
    uint32_t idx_pos = BUNDLE_HEADER_SIZE + tile_idx * 8u;

    uint8_t entry[8];
    if (!tile_read_bytes(idx_pos, entry, 8, &br) || br != 8)
    {
        return false;
    }

    uint32_t rel_offset = (uint32_t)entry[0]
                        | ((uint32_t)entry[1] << 8)
                        | ((uint32_t)entry[2] << 16)
                        | ((uint32_t)entry[3] << 24);

    uint32_t length     = (uint32_t)entry[4]
                        | ((uint32_t)entry[5] << 8)
                        | ((uint32_t)entry[6] << 16)
                        | ((uint32_t)entry[7] << 24);

    if (rel_offset == ABSENT_OFFSET || length == 0)
    {
        // Normal case: tile is not covered in this bundle block
        return false;
    }

    uint32_t data_section_start = BUNDLE_HEADER_SIZE + (uint32_t)blk_size * (uint32_t)blk_size * 8u;
    *tile_start_out  = data_section_start + rel_offset;
    *tile_length_out = length;
    return true;
}

// =========================================================================
// Tile Header Loading and Descriptor Population
// =========================================================================

static TileDesc_t* load_tile_into_cache(const char* src, uint32_t tile_start, uint32_t tile_length)
{
    if (tile_length < TILE_HEADER_SIZE)
    {
        map_log("Tile '%s' length %u too short for header", src, tile_length);
        return NULL;
    }

    uint8_t head[TILE_HEADER_SIZE];
    uint32_t br = 0;
    if (!tile_read_bytes(tile_start, head, TILE_HEADER_SIZE, &br) || br != TILE_HEADER_SIZE)
    {
        map_log("Failed to read header for '%s'", src);
        return NULL;
    }

    if (memcmp(head, ZST_MAGIC, 4) != 0)
    {
        map_log("Unknown tile magic '%.4s' in '%s'", head, src);
        return NULL;
    }

    uint16_t width       = (uint16_t)(head[4]  | ((uint16_t)head[5]  << 8));
    uint16_t height      = (uint16_t)(head[6]  | ((uint16_t)head[7]  << 8));
    uint16_t chunk_intvl = (uint16_t)(head[10] | ((uint16_t)head[11] << 8));
    uint16_t chunk_count = (uint16_t)(head[12] | ((uint16_t)head[13] << 8));

    if (chunk_count == 0 || chunk_count > TILE_MAX_CHUNKS)
    {
        map_log("Invalid tile meta: Chunks=%u", chunk_count);
        return NULL;
    }

    uint32_t chunk_tbl_bytes = (uint32_t)chunk_count * 4u;
    uint32_t meta_bytes = TILE_HEADER_SIZE + chunk_tbl_bytes;

    if (tile_length <= meta_bytes)
    {
        map_log("Corrupt tile length %u <= meta_bytes %u", tile_length, meta_bytes);
        return NULL;
    }
    uint32_t payload_bytes = tile_length - meta_bytes;

    TileDesc_t* desc = get_or_allocate_desc(src);
    if (!desc)
    {
        return NULL;
    }

    desc->tile_start      = tile_start;
    desc->tile_length     = tile_length;
    desc->payload_start   = tile_start + meta_bytes;
    desc->payload_length  = payload_bytes;
    desc->width           = width;
    desc->height          = height;
    desc->chunk_interval  = chunk_intvl;
    desc->chunk_count     = chunk_count;

    // Read chunk offsets table directly after 16B header
    uint8_t tbl_raw[TILE_MAX_CHUNKS * 4];
    if (!tile_read_bytes(tile_start + TILE_HEADER_SIZE, tbl_raw, chunk_tbl_bytes, &br) || br != chunk_tbl_bytes)
    {
        map_log("Failed to read chunk table (%u bytes) for '%s'", chunk_tbl_bytes, src);
        desc->valid = false;
        return NULL;
    }
    for (uint16_t ci = 0; ci < chunk_count; ci++)
    {
        desc->chunk_offsets[ci] = (uint32_t)tbl_raw[ci * 4]
                                | ((uint32_t)tbl_raw[ci * 4 + 1] << 8)
                                | ((uint32_t)tbl_raw[ci * 4 + 2] << 16)
                                | ((uint32_t)tbl_raw[ci * 4 + 3] << 24);
    }

    strncpy(desc->bundle_path, s_bundle_path, sizeof(desc->bundle_path) - 1);

    desc->valid = true;
    map_log("Loaded '%s' [ZST2 %ux%u chunks=%u payload=%u B]",
            src, width, height, chunk_count, payload_bytes);
    return desc;
}

// =========================================================================
// LVGL Widget Boilerplate & Event Handler
// =========================================================================

typedef struct {
    lv_color_t* dest_buf;       // Pointer to draw_ctx->buf
    lv_coord_t  buf_width;      // lv_area_get_width(draw_ctx->buf_area)
    lv_coord_t  buf_x1;         // draw_ctx->buf_area->x1
    lv_coord_t  buf_y1;         // draw_ctx->buf_area->y1
    lv_coord_t  screen_x1;      // clip_area.x1
    lv_coord_t  screen_y1;      // clip_area.y1
    lv_coord_t  local_x1;
    lv_coord_t  local_y1;
    lv_coord_t  local_x2;
    lv_coord_t  local_y2;
    lv_coord_t  blit_w;
} lv_img_rle_draw_dsc_t;

static void lv_img_rle_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
    LV_UNUSED(class_p);
    lv_img_rle_t* img = (lv_img_rle_t*)obj;
    img->src = NULL;
}

static void lv_img_rle_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
    LV_UNUSED(class_p);
    lv_img_rle_t* img = (lv_img_rle_t*)obj;
    if (img->src)
    {
        lv_mem_free(img->src);
        img->src = NULL;
    }
}

static lv_res_t lv_zst2_draw(const char* src, lv_img_rle_draw_dsc_t* dsc);

static void lv_img_rle_event(const lv_obj_class_t* class_p, lv_event_t* e)
{
    LV_UNUSED(class_p);
    lv_event_code_t code = lv_event_get_code(e);

    if (code != LV_EVENT_DRAW_MAIN_BEGIN)
    {
        lv_res_t res = lv_obj_event_base(MY_CLASS, e);
        if (res != LV_RES_OK) return;
    }

    if (code == LV_EVENT_DRAW_MAIN_BEGIN)
    {
        lv_obj_t* obj = lv_event_get_current_target(e);
        lv_img_rle_t* img = (lv_img_rle_t*)obj;
        if (img->src == NULL) return;

        const lv_draw_ctx_t* draw_ctx = (const lv_draw_ctx_t*)lv_event_get_param(e);
        if (!draw_ctx || !draw_ctx->buf || !draw_ctx->buf_area || !draw_ctx->clip_area) return;

        // 1. Precise boundary intersection: only redraw if this tile overlaps dirty area
        lv_area_t clip_area;
        if (!_lv_area_intersect(&clip_area, &obj->coords, draw_ctx->clip_area))
        {
            return;
        }

        // 2. Tile-local coordinates strictly in range [0..255]
        lv_coord_t local_x1 = clip_area.x1 - obj->coords.x1;
        lv_coord_t local_y1 = clip_area.y1 - obj->coords.y1;
        lv_coord_t local_x2 = clip_area.x2 - obj->coords.x1;
        lv_coord_t local_y2 = clip_area.y2 - obj->coords.y1;

        if (local_x1 < 0) local_x1 = 0;
        if (local_y1 < 0) local_y1 = 0;
        if (local_x2 >= TILE_SIZE) local_x2 = TILE_SIZE - 1;
        if (local_y2 >= TILE_SIZE) local_y2 = TILE_SIZE - 1;
        if (local_x1 > local_x2 || local_y1 > local_y2) return;

        lv_img_rle_draw_dsc_t dsc;
        dsc.dest_buf   = (lv_color_t*)draw_ctx->buf;
        dsc.buf_width  = lv_area_get_width(draw_ctx->buf_area);
        dsc.buf_x1     = draw_ctx->buf_area->x1;
        dsc.buf_y1     = draw_ctx->buf_area->y1;
        dsc.screen_x1  = clip_area.x1;
        dsc.screen_y1  = clip_area.y1;
        dsc.local_x1   = local_x1;
        dsc.local_y1   = local_y1;
        dsc.local_x2   = local_x2;
        dsc.local_y2   = local_y2;
        dsc.blit_w     = local_x2 - local_x1 + 1;

        lv_zst2_draw(img->src, &dsc);
    }
}

const lv_obj_class_t lv_img_rle_class =
{
    .base_class    = &lv_obj_class,
    .constructor_cb = lv_img_rle_constructor,
    .destructor_cb  = lv_img_rle_destructor,
    .event_cb      = lv_img_rle_event,
    .instance_size = sizeof(lv_img_rle_t),
};

lv_obj_t* lv_img_rle_create(lv_obj_t* parent)
{
    lv_obj_t* obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

void lv_img_rle_set_src(lv_obj_t* obj, const char* src)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_img_rle_t* img = (lv_img_rle_t*)obj;

    if (src)
    {
        size_t len = strlen(src) + 1;
        img->src = (char*)lv_mem_realloc(img->src, len);
        strcpy(img->src, src);
    }
    else
    {
        if (img->src)
        {
            lv_mem_free(img->src);
            img->src = NULL;
        }
    }
    lv_obj_invalidate(obj);
}

// =========================================================================
// Main ZST2 Micro-Chunk Streaming Draw Function
// =========================================================================

static lv_res_t lv_zst2_draw(const char* src, lv_img_rle_draw_dsc_t* dsc)
{
    // Step 1: Lookup or load tile descriptor
    TileDesc_t* desc = cache_find(src);

    if (!desc)
    {
        uint32_t tile_start = 0, tile_length = 0;
        if (!open_tile(src, &tile_start, &tile_length))
        {
            return LV_RES_OK; // Absent tile
        }

        desc = load_tile_into_cache(src, tile_start, tile_length);
        if (!desc)
        {
            return LV_RES_INV;
        }
    }

    // Step 2: Ensure descriptor's bundle file is open
    if (!ensure_bundle_open(desc->bundle_path))
    {
        return LV_RES_INV;
    }

    // Step 3: Determine which 32-row chunks overlap local_y1 .. local_y2
    int chunk_start = dsc->local_y1 / TILE_CHUNK_ROWS;
    int chunk_end   = dsc->local_y2 / TILE_CHUNK_ROWS;
    if (chunk_start < 0) chunk_start = 0;
    if (chunk_end >= (int)desc->chunk_count) chunk_end = (int)desc->chunk_count - 1;

    // Step 4: Stream and decompress only the overlapping chunks
    for (int ci = chunk_start; ci <= chunk_end; ci++)
    {
        // Optimization 1: MRU Decompression Cache (skip SD read & decompress if chunk is already cached)
        if (s_mru_chunk.desc != desc || s_mru_chunk.chunk_idx != ci)
        {
            uint32_t chunk_comp_offset = desc->chunk_offsets[ci];
            uint32_t chunk_abs_pos = desc->payload_start + chunk_comp_offset;
            uint32_t chunk_comp_len = (ci + 1 < desc->chunk_count)
                                      ? (desc->chunk_offsets[ci + 1] - chunk_comp_offset)
                                      : (desc->payload_length - chunk_comp_offset);

            if (chunk_comp_len > sizeof(s_comp_chunk_buf))
            {
                chunk_comp_len = sizeof(s_comp_chunk_buf);
            }

            uint32_t br = 0;
            if (!tile_read_bytes(chunk_abs_pos, s_comp_chunk_buf, chunk_comp_len, &br) || br != chunk_comp_len)
            {
                map_log("Chunk %d read failed (%u bytes at %u) in '%s'", ci, chunk_comp_len, chunk_abs_pos, src);
                continue;
            }

            uint8_t mode = s_comp_chunk_buf[0];
            s_mru_chunk.chunk_tier = mode;

            if (mode == 0x01)
            {
                // Tier 1: 4-bit packed micro-palette (≤16 colors)
                uint8_t pal_cnt = s_comp_chunk_buf[1];
                for (uint8_t pi = 0; pi < pal_cnt; pi++)
                {
                    uint16_t c565 = (uint16_t)(s_comp_chunk_buf[2 + pi * 2] | ((uint16_t)s_comp_chunk_buf[2 + pi * 2 + 1] << 8));
                    s_mru_chunk.micro_palette[pi] = rgb565_to_lv_color(c565);
                }

                size_t hdr_len = 2 + (size_t)pal_cnt * 2;
                // Decompress 4096 bytes of 4-bit indices into upper half of s_scratch_buf
                int dec_bytes = zstd_decompress_chunk(s_scratch_buf + 8192, 4096,
                                                      s_comp_chunk_buf + hdr_len, chunk_comp_len - hdr_len);
                if (dec_bytes > 0)
                {
                    // Unpack 4-bit indices into s_scratch_buf[0..8191]
                    const uint8_t* p4 = &s_scratch_buf[8192];
                    for (int i = 0; i < 4096; i++)
                    {
                        uint8_t b = p4[i];
                        s_scratch_buf[i * 2]     = b >> 4;
                        s_scratch_buf[i * 2 + 1] = b & 0x0F;
                    }
                }
                else
                {
                    map_log("ZSTD Tier1 decompress failed on chunk %d", ci);
                    continue;
                }
            }
            else if (mode == 0x02)
            {
                // Tier 2: 8-bit micro-palette (17..256 colors)
                uint16_t pal_cnt = (uint16_t)s_comp_chunk_buf[1] + 1;
                for (uint16_t pi = 0; pi < pal_cnt; pi++)
                {
                    uint16_t c565 = (uint16_t)(s_comp_chunk_buf[2 + pi * 2] | ((uint16_t)s_comp_chunk_buf[2 + pi * 2 + 1] << 8));
                    s_mru_chunk.micro_palette[pi] = rgb565_to_lv_color(c565);
                }

                size_t hdr_len = 2 + (size_t)pal_cnt * 2;
                int dec_bytes = zstd_decompress_chunk(s_scratch_buf, 8192,
                                                      s_comp_chunk_buf + hdr_len, chunk_comp_len - hdr_len);
                if (dec_bytes <= 0)
                {
                    map_log("ZSTD Tier2 decompress failed on chunk %d", ci);
                    continue;
                }
            }
            else if (mode == 0x03)
            {
                // Tier 3: 16-bit G-decorrelated raw pixels (>256 colors)
                size_t hdr_len = 2;
                int dec_bytes = zstd_decompress_chunk(s_scratch_buf, 16384,
                                                      s_comp_chunk_buf + hdr_len, chunk_comp_len - hdr_len);
                if (dec_bytes > 0)
                {
                    // In-place Green-Decorrelation Inverse Transform
                    uint16_t* px16 = (uint16_t*)s_scratch_buf;
                    for (int i = 0; i < TILE_SIZE * TILE_CHUNK_ROWS; i++)
                    {
                        uint32_t val = px16[i];
                        uint32_t dr5 = (val >> 11) & 0x1F;
                        uint32_t g6  = (val >> 5) & 0x3F;
                        uint32_t db5 = val & 0x1F;
                        uint32_t half_g = g6 >> 1;
                        uint32_t r5 = (dr5 + half_g) & 0x1F;
                        uint32_t b5 = (db5 + half_g) & 0x1F;
                        uint16_t c565 = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
#if LV_COLOR_16_SWAP == 1
                        px16[i] = (uint16_t)((c565 << 8) | (c565 >> 8));
#else
                        px16[i] = c565;
#endif
                    }
                }
                else
                {
                    map_log("ZSTD Tier3 decompress failed on chunk %d", ci);
                    continue;
                }
            }

            s_mru_chunk.desc = desc;
            s_mru_chunk.chunk_idx = ci;
        }

        // Render overlapping rows within this chunk
        int chunk_row_start = ci * TILE_CHUNK_ROWS;
        int row_min = (dsc->local_y1 > chunk_row_start) ? dsc->local_y1 : chunk_row_start;
        int row_max = (dsc->local_y2 < chunk_row_start + TILE_CHUNK_ROWS - 1) ? dsc->local_y2 : (chunk_row_start + TILE_CHUNK_ROWS - 1);
        int blit_w = dsc->blit_w;

        // Path A: Tier 3 Direct 16-bit RGB565 memory copy
        if (s_mru_chunk.chunk_tier == 0x03)
        {
            for (int y = row_min; y <= row_max; y++)
            {
                int row_in_chunk = y - chunk_row_start;
                const lv_color_t* src_row_pixels = (const lv_color_t*)&s_scratch_buf[(row_in_chunk * TILE_SIZE + dsc->local_x1) * 2];
                int screen_y = dsc->screen_y1 + (y - dsc->local_y1);
                lv_color_t* dest_row = dsc->dest_buf + (screen_y - dsc->buf_y1) * dsc->buf_width + (dsc->screen_x1 - dsc->buf_x1);

                arm_fast_memcpy(dest_row, src_row_pixels, blit_w * sizeof(lv_color_t));
            }
        }
        else
        {
            // Path B: Micro-Palette lookup (Tier 1 & Tier 2)
            const uint16_t* pal = (const uint16_t*)s_mru_chunk.micro_palette;

            for (int y = row_min; y <= row_max; y++)
            {
                int row_in_chunk = y - chunk_row_start;
                const uint8_t* src_row_indices = &s_scratch_buf[row_in_chunk * TILE_SIZE + dsc->local_x1];

                int screen_y = dsc->screen_y1 + (y - dsc->local_y1);
                lv_color_t* dest_row = dsc->dest_buf + (screen_y - dsc->buf_y1) * dsc->buf_width + (dsc->screen_x1 - dsc->buf_x1);

                int x = 0;

                // Optimization 2: Handle first unaligned pixel to guarantee 32-bit word alignment
                if (((uintptr_t)&dest_row[0] & 2) && blit_w > 0)
                {
                    dest_row[0] = *(lv_color_t*)&pal[src_row_indices[0]];
                    x = 1;
                }

                // 4-pixel parallel lookup and 2x 32-bit burst writes
                uint32_t* dst32 = (uint32_t*)&dest_row[x];
                for (; x + 3 < blit_w; x += 4)
                {
                    uint32_t c0 = pal[src_row_indices[x]];
                    uint32_t c1 = pal[src_row_indices[x + 1]];
                    uint32_t c2 = pal[src_row_indices[x + 2]];
                    uint32_t c3 = pal[src_row_indices[x + 3]];
                    dst32[0] = c0 | (c1 << 16);
                    dst32[1] = c2 | (c3 << 16);
                    dst32 += 2;
                }

                // 2-pixel remainder
                for (; x + 1 < blit_w; x += 2)
                {
                    uint32_t c0 = pal[src_row_indices[x]];
                    uint32_t c1 = pal[src_row_indices[x + 1]];
                    *dst32++ = c0 | (c1 << 16);
                }

                // Handle trailing odd pixel
                if (x < blit_w)
                {
                    dest_row[x] = *(lv_color_t*)&pal[src_row_indices[x]];
                }
            }
        }
    }

    return LV_RES_OK;
}
