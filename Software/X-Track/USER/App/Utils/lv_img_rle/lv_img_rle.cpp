/*
 * lv_img_rle.cpp
 *
 * High-Performance Micro-Chunk On-Demand Streaming Engine with Dual-Format (LZ42 & RLE2) Support.
 * Part of X-Track 2.5 Map System.
 *
 * Features:
 *   1. Micro-chunk On-Demand Streaming: reads and decodes only the required 32-row chunk (2~6KB) from SD.
 *   2. Zero memory limit: easily renders complex 50KB~64KB photographic tiles with zero risk of heap/arena overflow.
 *   3. Ultra-low static RAM: requires only ~25KB total (16 descriptors x 560B + 8KB scratchpad + 8KB chunk buffer).
 *   4. Multi-path probe: automatically resolves /MAPRB, /MAP, and zoom directory structures.
 *   5. Out-of-bounds rejection: strictly guards against invalid negative / out-of-world tile coordinates.
 *   6. Robust diagnostic logging to SD card (/MAP_LOG.TXT) and Serial console.
 */

#include "lv_img_rle.h"
#include "lz4_decompress.h"
extern "C" {
#include "lz4_decompress.c"
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
#define BUNDLE_MAGIC          "TBND"
#define BUNDLE_HEADER_SIZE    14u
#define BUNDLE_BLOCK_SIZE     100
#define ABSENT_OFFSET         0xFFFFFFFFu

// ---- Tile constants (must match tile_lz4_encode.py / rle_encode) --------
#define LZ4_MAGIC             "LZ42"
#define RLE_MAGIC             "RLE2"
#define LZ4_HEADER_SIZE       16u
#define LZ4_MAX_PALETTE       256
#define LZ4_MAX_CHUNKS        8
#define LZ4_CHUNK_ROWS        32
#define LZ4_TILE_SIZE         256
#define LZ4_MAX_DESCRIPTORS   16

// ---- LVGL drive letter for SD card --------------------------------------
#define TILE_SD_DRIVE_LETTER  '/'

#define LZ4_SCRATCH_BUF_SIZE      (LZ4_TILE_SIZE * LZ4_CHUNK_ROWS) // 8192 bytes (32 rows)
#define LZ4_CHUNK_FETCH_BUF_SIZE  (LZ4_SCRATCH_BUF_SIZE + 64)      // Max compressed chunk size

enum TileFormat_t {
    TILE_FMT_LZ42 = 0,
    TILE_FMT_RLE2 = 1,
};

// =========================================================================
// Diagnostic Logging (SD card /MAP_LOG.TXT + Serial)
// =========================================================================
static void map_log(const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    LV_LOG_USER("[MAP] %s", buf);

    HAL::Map_Log_Write(buf);
}

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
// Tile Descriptors (Metadata & Pre-baked Palette)
// =========================================================================
typedef struct {
    char        path[64];
    char        bundle_path[96];
    uint32_t    last_access_tick;

    uint32_t    tile_start;
    uint32_t    tile_length;
    uint32_t    payload_start;
    uint32_t    payload_length;

    uint8_t     format;               // TILE_FMT_LZ42 or TILE_FMT_RLE2
    uint16_t    width;
    uint16_t    height;
    uint16_t    palette_count;
    uint16_t    chunk_interval;
    uint16_t    chunk_count;

    uint32_t    chunk_offsets[LZ4_MAX_CHUNKS]; // offsets from payload start to each chunk/checkpoint
    lv_color_t  palette[LZ4_MAX_PALETTE];      // pre-baked lv_color_t

    bool        valid;
} Lz4TileDesc_t;

static Lz4TileDesc_t  s_descriptors[LZ4_MAX_DESCRIPTORS];
static uint8_t        s_comp_chunk_buf[LZ4_CHUNK_FETCH_BUF_SIZE]; // 8 KB static compressed chunk buffer
static uint8_t        s_scratch_buf[LZ4_SCRATCH_BUF_SIZE];        // 8 KB static uncompressed pixel buffer

// MRU decompression cache: eliminates redundant SD reads & LZ4 decompression
static struct {
    const Lz4TileDesc_t* desc;
    int chunk_idx;
} s_mru_chunk = { NULL, -1 };

// =========================================================================
// Cache Management API
// =========================================================================

void lv_img_rle_cache_init()
{
    memset(s_descriptors, 0, sizeof(s_descriptors));
    s_mru_chunk.desc = NULL;
    s_mru_chunk.chunk_idx = -1;
    map_log("Cache init: Micro-chunk Streaming (16 Descs, 8KB Scratch, 8KB Fetch, MRU Opt)");
}

void lv_img_rle_cache_deinit()
{
    s_mru_chunk.desc = NULL;
    s_mru_chunk.chunk_idx = -1;

    for (int i = 0; i < LZ4_MAX_DESCRIPTORS; i++)
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
static Lz4TileDesc_t* cache_find(const char* path)
{
    uint32_t now = lv_tick_get();
    for (int i = 0; i < LZ4_MAX_DESCRIPTORS; i++)
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
static Lz4TileDesc_t* get_or_allocate_desc(const char* path)
{
    // 1. Check if already exists
    for (int i = 0; i < LZ4_MAX_DESCRIPTORS; i++)
    {
        if (s_descriptors[i].valid && strcmp(s_descriptors[i].path, path) == 0)
        {
            s_descriptors[i].last_access_tick = lv_tick_get();
            return &s_descriptors[i];
        }
    }

    // 2. Find empty slot
    for (int i = 0; i < LZ4_MAX_DESCRIPTORS; i++)
    {
        if (!s_descriptors[i].valid)
        {
            memset(&s_descriptors[i], 0, sizeof(Lz4TileDesc_t));
            strncpy(s_descriptors[i].path, path, sizeof(s_descriptors[i].path) - 1);
            s_descriptors[i].last_access_tick = lv_tick_get();
            return &s_descriptors[i];
        }
    }

    // 3. Evict oldest LRU descriptor
    uint32_t oldest_tick = 0xFFFFFFFFu;
    int evict_idx = 0;
    for (int i = 0; i < LZ4_MAX_DESCRIPTORS; i++)
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

    memset(&s_descriptors[evict_idx], 0, sizeof(Lz4TileDesc_t));
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

    // Reject out-of-world coordinates (e.g. negative coords cast to UINT32_MAX)
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
    char candidate[5][96];
    int cand_count = 0;

    // 1. /:<prefix>/<level>/<blockX>_<blockY>.tbnd
    snprintf(candidate[cand_count++], sizeof(candidate[0]),
             "%c:%s/%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, prefix, (unsigned)level, (unsigned)block_x, (unsigned)block_y);

    // 2. /:MAPRB/<level>/<blockX>_<blockY>.tbnd
    if (strcmp(prefix, "MAPRB") != 0)
    {
        snprintf(candidate[cand_count++], sizeof(candidate[0]),
                 "%c:MAPRB/%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, (unsigned)level, (unsigned)block_x, (unsigned)block_y);
    }

    // 3. /:MAP/<level>/<blockX>_<blockY>.tbnd
    if (strcmp(prefix, "MAP") != 0)
    {
        snprintf(candidate[cand_count++], sizeof(candidate[0]),
                 "%c:MAP/%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, (unsigned)level, (unsigned)block_x, (unsigned)block_y);
    }

    // 4. /:<level>/<blockX>_<blockY>.tbnd
    snprintf(candidate[cand_count++], sizeof(candidate[0]),
             "%c:%u/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, (unsigned)level, (unsigned)block_x, (unsigned)block_y);

    // 5. /:<prefix>/<blockX>_<blockY>.tbnd (flat directory)
    snprintf(candidate[cand_count++], sizeof(candidate[0]),
             "%c:%s/%u_%u.tbnd", TILE_SD_DRIVE_LETTER, prefix, (unsigned)block_x, (unsigned)block_y);

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
        map_log("Failed to open bundle for tile (%u,%u) L%u [tried %s ...]",
                tile_x, tile_y, level, candidate[0]);
        return false;
    }

    uint32_t index_pos = BUNDLE_HEADER_SIZE + (local_y * BUNDLE_BLOCK_SIZE + local_x) * 8u;
    uint8_t  entry[8];
    uint32_t br = 0;

    if (lv_fs_seek(&s_bundle_file, index_pos, LV_FS_SEEK_SET) != LV_FS_RES_OK
        || lv_fs_read(&s_bundle_file, entry, sizeof(entry), &br) != LV_FS_RES_OK
        || br != sizeof(entry))
    {
        map_log("Index read error at pos %u in '%s'", index_pos, s_bundle_path);
        return false;
    }

    uint32_t offset = (uint32_t)entry[0]        | ((uint32_t)entry[1] << 8)
                    | ((uint32_t)entry[2] << 16) | ((uint32_t)entry[3] << 24);
    uint32_t length = (uint32_t)entry[4]        | ((uint32_t)entry[5] << 8)
                    | ((uint32_t)entry[6] << 16) | ((uint32_t)entry[7] << 24);

    if (offset == ABSENT_OFFSET || length == 0)
    {
        map_log("Tile (%u,%u) absent in bundle '%s' (offset=0x%08X, len=%u)",
                tile_x, tile_y, s_bundle_path, offset, length);
        return false;
    }

    uint32_t data_section_start = BUNDLE_HEADER_SIZE + (uint32_t)BUNDLE_BLOCK_SIZE * BUNDLE_BLOCK_SIZE * 8u;
    *tile_start_out  = data_section_start + offset;
    *tile_length_out = length;
    return true;
}

// Load tile metadata into descriptor
static Lz4TileDesc_t* load_tile_into_cache(const char* src, uint32_t tile_start, uint32_t tile_length)
{
    // Read header (16 bytes)
    uint8_t head[LZ4_HEADER_SIZE];
    uint32_t br = 0;
    if (!tile_read_bytes(tile_start, head, LZ4_HEADER_SIZE, &br) || br != LZ4_HEADER_SIZE)
    {
        map_log("Tile header read failed at %u for '%s'", tile_start, src);
        return NULL;
    }

    uint8_t format = TILE_FMT_LZ42;
    if (memcmp(head, LZ4_MAGIC, 4) == 0)
    {
        format = TILE_FMT_LZ42;
    }
    else if (memcmp(head, RLE_MAGIC, 4) == 0)
    {
        format = TILE_FMT_RLE2;
    }
    else
    {
        map_log("Unknown tile magic '%.4s' in '%s'", head, src);
        return NULL;
    }

    uint16_t width         = (uint16_t)(head[4]  | ((uint16_t)head[5]  << 8));
    uint16_t height        = (uint16_t)(head[6]  | ((uint16_t)head[7]  << 8));
    uint16_t palette_count = (uint16_t)(head[8]  | ((uint16_t)head[9]  << 8));
    uint16_t chunk_intvl   = (uint16_t)(head[10] | ((uint16_t)head[11] << 8));
    uint16_t chunk_count   = (uint16_t)(head[12] | ((uint16_t)head[13] << 8));

    if (palette_count == 0 || palette_count > LZ4_MAX_PALETTE || chunk_count == 0 || chunk_count > LZ4_MAX_CHUNKS)
    {
        map_log("Invalid tile meta: Pal=%u Chunks=%u", palette_count, chunk_count);
        return NULL;
    }

    uint32_t palette_bytes = (uint32_t)palette_count * 2u;
    uint32_t chunk_tbl_bytes = (uint32_t)chunk_count * 4u;
    uint32_t meta_bytes = LZ4_HEADER_SIZE + palette_bytes + chunk_tbl_bytes;

    if (tile_length <= meta_bytes)
    {
        map_log("Corrupt tile length %u <= meta_bytes %u", tile_length, meta_bytes);
        return NULL;
    }
    uint32_t payload_bytes = tile_length - meta_bytes;

    Lz4TileDesc_t* desc = get_or_allocate_desc(src);
    if (!desc)
    {
        return NULL;
    }

    desc->format          = format;
    desc->tile_start      = tile_start;
    desc->tile_length     = tile_length;
    desc->payload_start   = tile_start + meta_bytes;
    desc->payload_length  = payload_bytes;
    desc->width           = width;
    desc->height          = height;
    desc->palette_count   = palette_count;
    desc->chunk_interval  = chunk_intvl;
    desc->chunk_count     = chunk_count;

    // Read palette and pre-convert to lv_color_t
    uint8_t pal_raw[LZ4_MAX_PALETTE * 2];
    if (!tile_read_bytes(tile_start + LZ4_HEADER_SIZE, pal_raw, palette_bytes, &br) || br != palette_bytes)
    {
        map_log("Failed to read palette (%u bytes) for '%s'", palette_bytes, src);
        desc->valid = false;
        return NULL;
    }
    for (uint16_t pi = 0; pi < palette_count; pi++)
    {
        uint16_t c565 = (uint16_t)(pal_raw[pi * 2] | ((uint16_t)pal_raw[pi * 2 + 1] << 8));
        desc->palette[pi] = rgb565_to_lv_color(c565);
    }

    // Read chunk offsets table
    uint8_t tbl_raw[LZ4_MAX_CHUNKS * 4];
    if (!tile_read_bytes(tile_start + LZ4_HEADER_SIZE + palette_bytes, tbl_raw, chunk_tbl_bytes, &br)
        || br != chunk_tbl_bytes)
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
    map_log("Loaded '%s' [%s %ux%u pal=%u chunks=%u payload=%u B]",
            src, (format == TILE_FMT_LZ42 ? "LZ42" : "RLE2"),
            width, height, palette_count, chunk_count, payload_bytes);
    return desc;
}

// =========================================================================
// LVGL Widget Boilerplate & Event Handler
// =========================================================================

typedef struct {
    lv_color_t* disp_buf;
    lv_coord_t  disp_width;
    lv_coord_t  screen_x1;
    lv_coord_t  screen_y1;
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

static lv_res_t lv_lz4_draw(const char* src, lv_img_rle_draw_dsc_t* dsc);

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

        // 1. Precise boundary intersection: only redraw if this tile overlaps dirty area
        lv_area_t clip_area;
        if (!_lv_area_intersect(&clip_area, &obj->coords, draw_ctx->clip_area))
        {
            return;
        }

        // 2. Get frame buffer
        lv_disp_t* disp = _lv_refr_get_disp_refreshing();
        lv_disp_draw_buf_t* draw_buf = lv_disp_get_draw_buf(disp);
        lv_color_t* disp_buf = (lv_color_t*)draw_buf->buf_act;

        lv_area_t disp_area;
        lv_area_set(&disp_area, 0, 0, LV_HOR_RES - 1, LV_VER_RES - 1);
        lv_coord_t disp_width = lv_area_get_width(&disp_area);

        // 3. Tile-local coordinates strictly in range [0..255]
        lv_coord_t local_x1 = clip_area.x1 - obj->coords.x1;
        lv_coord_t local_y1 = clip_area.y1 - obj->coords.y1;
        lv_coord_t local_x2 = clip_area.x2 - obj->coords.x1;
        lv_coord_t local_y2 = clip_area.y2 - obj->coords.y1;

        if (local_x1 < 0) local_x1 = 0;
        if (local_y1 < 0) local_y1 = 0;
        if (local_x2 >= LZ4_TILE_SIZE) local_x2 = LZ4_TILE_SIZE - 1;
        if (local_y2 >= LZ4_TILE_SIZE) local_y2 = LZ4_TILE_SIZE - 1;
        if (local_x1 > local_x2 || local_y1 > local_y2) return;

        lv_img_rle_draw_dsc_t dsc;
        dsc.disp_buf   = disp_buf;
        dsc.disp_width = disp_width;
        dsc.screen_x1  = clip_area.x1;
        dsc.screen_y1  = clip_area.y1;
        dsc.local_x1   = local_x1;
        dsc.local_y1   = local_y1;
        dsc.local_x2   = local_x2;
        dsc.local_y2   = local_y2;
        dsc.blit_w     = local_x2 - local_x1 + 1;

        lv_lz4_draw(img->src, &dsc);
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
// Main Micro-Chunk Streaming Draw Function
// =========================================================================

static lv_res_t lv_lz4_draw(const char* src, lv_img_rle_draw_dsc_t* dsc)
{
    // Step 1: Lookup or load tile descriptor
    Lz4TileDesc_t* desc = cache_find(src);

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
    int chunk_start = dsc->local_y1 / LZ4_CHUNK_ROWS;
    int chunk_end   = dsc->local_y2 / LZ4_CHUNK_ROWS;
    if (chunk_start < 0) chunk_start = 0;
    if (chunk_end >= (int)desc->chunk_count) chunk_end = (int)desc->chunk_count - 1;

    // Step 4: Stream and decompress only the overlapping chunks
    for (int ci = chunk_start; ci <= chunk_end; ci++)
    {
        // Optimization 1: MRU Decompression Cache (skip SD read & LZ4 if chunk is already in s_scratch_buf)
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

            if (desc->format == TILE_FMT_LZ42)
            {
                int dec_bytes = LZ4_decompress_fast((const char*)s_comp_chunk_buf, (char*)s_scratch_buf, LZ4_TILE_SIZE * LZ4_CHUNK_ROWS);
                if (dec_bytes <= 0)
                {
                    map_log("LZ4 decompress failed on chunk %d", ci);
                    continue;
                }
            }
            else // TILE_FMT_RLE2
            {
                uint32_t sp = 0, dp = 0;
                uint32_t target_pixels = LZ4_TILE_SIZE * LZ4_CHUNK_ROWS;
                while (sp + 1 < chunk_comp_len && dp < target_pixels)
                {
                    uint8_t run_len = s_comp_chunk_buf[sp++];
                    uint8_t idx     = s_comp_chunk_buf[sp++];
                    for (uint8_t r = 0; r < run_len && dp < target_pixels; r++)
                    {
                        s_scratch_buf[dp++] = idx;
                    }
                }
            }

            s_mru_chunk.desc = desc;
            s_mru_chunk.chunk_idx = ci;
        }

        // Render overlapping rows within this chunk
        int chunk_row_start = ci * LZ4_CHUNK_ROWS;
        int row_min = (dsc->local_y1 > chunk_row_start) ? dsc->local_y1 : chunk_row_start;
        int row_max = (dsc->local_y2 < chunk_row_start + LZ4_CHUNK_ROWS - 1) ? dsc->local_y2 : (chunk_row_start + LZ4_CHUNK_ROWS - 1);
        const uint16_t* pal = (const uint16_t*)desc->palette;
        int blit_w = dsc->blit_w;

        for (int y = row_min; y <= row_max; y++)
        {
            int row_in_chunk = y - chunk_row_start;
            const uint8_t* src_row_indices = &s_scratch_buf[row_in_chunk * LZ4_TILE_SIZE + dsc->local_x1];
            
            // Screen Y = screen_y1 + (y - local_y1)
            int screen_y = dsc->screen_y1 + (y - dsc->local_y1);
            lv_color_t* dest_row = dsc->disp_buf + screen_y * dsc->disp_width + dsc->screen_x1;

            int x = 0;

            // Optimization 2: Handle first unaligned pixel to guarantee 32-bit word alignment
            if (((uintptr_t)&dest_row[0] & 2) && blit_w > 0)
            {
                dest_row[0] = desc->palette[src_row_indices[0]];
                x = 1;
            }

            // 32-bit dual-pixel burst writes (2 pixels per single memory store cycle)
            uint32_t* dst32 = (uint32_t*)&dest_row[x];
            for (; x + 1 < blit_w; x += 2)
            {
                uint32_t c0 = pal[src_row_indices[x]];
                uint32_t c1 = pal[src_row_indices[x + 1]];
                *dst32++ = c0 | (c1 << 16);
            }

            // Handle trailing odd pixel
            if (x < blit_w)
            {
                dest_row[x] = desc->palette[src_row_indices[x]];
            }
        }
    }

    return LV_RES_OK;
}
