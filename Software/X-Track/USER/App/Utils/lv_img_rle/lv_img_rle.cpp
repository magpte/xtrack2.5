/*
 * lv_img_rle.cpp
 *
 * Combined tile-bundle reader + RLE2 decoder. Previously split across
 * TileBundleFS.cpp (an lv_fs virtual driver) and this file (LVGL widget).
 * Those two talked through LVGL's generic filesystem layer, which imposed
 * per-draw overhead on every tile:
 *
 *   1. lv_fs_open("B:/MAP/…") → drive-letter dispatch → bundle_fs_open
 *      re-parsed the path with strrchr/atoi/snprintf from scratch.
 *   2. lv_rle_draw called lv_fs_read(…, pair, 2, …) once per RLE run —
 *      inside the hot loop — walking the full callback chain for 2 bytes.
 *
 * This file removes both costs:
 *   1. Path parsing and bundle index lookup happen once per draw, as direct
 *      C function calls with no lv_fs driver dispatch.
 *   2. The run-stream is read through a 256-byte refill buffer, so the hot
 *      decode loop makes O(tile_bytes / 256) lv_fs_read calls instead of
 *      O(number_of_runs).
 *
 * Everything else is preserved:
 *   - Shared bundle file handle: the same .tbnd file stays open across
 *     adjacent tiles and repeated draws of the same tile, avoiding FAT
 *     directory traversals (important with 40K+ tile files on the SD card).
 *   - Metadata cache: header, palette, and checkpoint table for the most
 *     recently drawn tile are cached, skipping those reads on every
 *     repeat draw within one refresh burst.
 *   - Checkpoint seek: only the rows needed for the current clip area are
 *     decoded, not the full tile from row 0.
 *
 * -------------------------------------------------------------------------
 * Bundle file format  (must match tile_bundle.py exactly)
 * -------------------------------------------------------------------------
 *   [0..3]   magic "TBND"
 *   [4..5]   blockSize  (uint16 LE)
 *   [6..9]   blockX     (uint32 LE, informational)
 *   [10..13] blockY     (uint32 LE, informational)
 *   index table: blockSize × blockSize entries, 8 bytes each
 *     [0..3] tile data offset into data section (0xFFFFFFFF = absent)
 *     [4..7] tile data length in bytes
 *     entry for (localX, localY) is at index localY*blockSize + localX
 *   data section: concatenated RLE2 tile byte streams
 *
 * -------------------------------------------------------------------------
 * RLE2 tile format  (must match tile_rle_encode.py exactly)
 * -------------------------------------------------------------------------
 *   [0..3]   magic "RLE2"
 *   [4..5]   width              (uint16 LE)
 *   [6..7]   height             (uint16 LE)
 *   [8..9]   paletteCount       (uint16 LE)
 *   [10..11] checkpointInterval (uint16 LE)
 *   [12..13] checkpointCount    (uint16 LE)
 *   palette:      paletteCount × 2 bytes (RGB565 LE)
 *   checkpoints:  checkpointCount × 4 bytes (uint32 LE offsets into run stream)
 *   run stream:   (run_len: uint8, palette_idx: uint8) pairs
 */

#include "lv_img_rle.h"
#include <string.h>
#include <stdlib.h>

#define MY_CLASS &lv_img_rle_class

// ---- Bundle constants (must match tile_bundle.py) -----------------------
#define BUNDLE_MAGIC          "TBND"
#define BUNDLE_HEADER_SIZE    14u
#define BUNDLE_BLOCK_SIZE     100
#define ABSENT_OFFSET         0xFFFFFFFFu

// ---- RLE2 constants (must match tile_rle_encode.py) ---------------------
#define RLE_MAGIC             "RLE2"
#define RLE_HEADER_SIZE       14u
#define RLE_MAX_PALETTE       256
#define RLE_MAX_CHECKPOINTS   32
// Tile dimensions: must equal the actual encoded tile size.
// A mismatch is caught at load_meta() and logged as a warning.
#define RLE_MAX_TILE_WIDTH    256

// ---- LVGL drive letter for the SD card ----------------------------------
// Must match SD_LETTER in lv_port_fs_sdfat.cpp.
#define TILE_SD_DRIVE_LETTER  '/'

// ---- Run-stream read buffer size ----------------------------------------
// Each refill issues one lv_fs_read for up to this many bytes, replacing
// O(runs) per-pair reads with O(tile_bytes / RLE_READ_BUF_SIZE) reads.
// 256 = 128 (run_len, idx) pairs per SD read.  Power of two, fits on stack.
#define RLE_READ_BUF_SIZE     256u

// =========================================================================
// Shared bundle file handle
// =========================================================================
// One .tbnd file is kept open between draw calls. Adjacent tiles very often
// share the same bundle, so the common path is a strcmp-and-reuse rather
// than a FAT directory traversal for every single tile.
//
// CONCURRENCY NOTE (same as the old TileBundleFS):
// Only ONE virtual tile may be in the process of being read at any time.
// LVGL's draw events are serialised (one draw context at a time) so this
// assumption holds today. Do not add a second concurrent caller without
// revisiting the shared-handle design.
// =========================================================================
static lv_fs_file_t s_bundle_file;
static char         s_bundle_path[96] = "";
static bool         s_bundle_valid    = false;

// =========================================================================
// Tile metadata cache
// =========================================================================
// Caches header fields, palette, and checkpoint table for the most recently
// drawn tile path. The same tile is redrawn multiple times per refresh burst
// whenever overlapping panels (SportInfo, zoom indicator, active track line)
// each invalidate their own clip region that happens to fall on this tile.
// Caching the metadata (but NOT pixel data) skips 3 sequential SD reads on
// every repeat draw.  tile_start/tile_length are included so the cache hit
// path never needs to re-read the bundle index table.
// =========================================================================
typedef struct {
    char     path[80];
    bool     valid;

    uint32_t tile_start;          // absolute byte offset of this tile in the bundle file
    uint32_t tile_length;         // byte length of the full RLE2 stream for this tile

    uint16_t width;
    uint16_t height;
    uint16_t paletteCount;
    uint16_t checkpointInterval;
    uint16_t checkpointCount;
    uint32_t run_stream_start;    // byte offset from tile_start to first run pair

    uint16_t palette[RLE_MAX_PALETTE];
    uint32_t checkpoints[RLE_MAX_CHECKPOINTS];
} TileMeta_t;

static TileMeta_t s_meta = { "", false, 0, 0, 0, 0, 0, 0, 0, 0, {0}, {0} };

// =========================================================================
// Pixel-level cache (palette-index form)
// =========================================================================
// Caches the FULLY DECODED tile as one palette-index byte per pixel
// (RLE_MAX_TILE_WIDTH * RLE_MAX_TILE_WIDTH = 65536 bytes = 64KB per slot
// at the default 256x256 tile size). This is deliberately NOT expanded to
// RGB565 (which would be 128KB/slot) -- the extra rgb565 lookup on a cache
// hit costs one array read per pixel, which is negligible next to the SD
// read + run-stream decode it replaces.
//
// WHY THIS EXISTS: the map view keeps several tiles resident
// (LiveMap.cpp: view is tiled into a ~2x3 grid of 256px tiles = 6 tiles),
// and things drawn ON TOP of the map -- the direction arrow, the active
// track line, the zoom/sport-info overlays -- move independently of the
// map itself. Every time one of them moves, LVGL invalidates and redraws
// the map tile(s) underneath, which without this cache means a full
// re-decode of that tile from the run stream, even though the map hasn't
// actually scrolled. This is the single most common redraw pattern during
// active navigation (it fires roughly once per GPS update, i.e. every
// CONFIG_GPS_REFR_PERIOD), and it always hits the SAME one or two tiles
// (whichever the arrow currently sits over) until the view actually
// crosses a tile boundary. Caching those tiles' decoded pixels turns a
// full RLE decode into a flat memory copy for every one of those redraws.
//
// RAM BUDGET -- READ BEFORE CHANGING RLE_PIXEL_CACHE_SLOTS:
// Target MCU (AT32F403ACGU7, per this project's .sct) has 224KB total
// SRAM shared by LVGL's own buffers, fonts, GPS/track buffers, and every
// other subsystem. A 6-slot cache (one per visible tile) would need
// 6*64KB = 384KB -- more than the entire chip's RAM -- so this can only
// ever cover the "same tile(s) redrawn repeatedly" case above, not "every
// visible tile stays cached forever while panning". Each additional slot
// costs another 64KB; do not raise this without first confirming real
// spare heap via HAL::Memory_DumpInfo() (already called periodically in
// HAL.cpp) with the map page open.
//
// LIFETIME: the buffers are NOT static/always-resident. LiveMap calls
// lv_img_rle_cache_init() when the map page appears and
// lv_img_rle_cache_deinit() when it disappears (see LiveMap.cpp), so the
// 64KB/slot is only reserved while the map is actually on screen, freeing
// it for other pages the rest of the time. Allocation failure (e.g. not
// enough free heap right now) is handled gracefully: caching is simply
// skipped and every draw falls back to the pre-cache decode path, so a
// tight-RAM build still works correctly, just without this speedup.
// =========================================================================
#define RLE_PIXEL_CACHE_SLOTS   1

typedef struct {
    char     path[80];
    bool     valid;              // true once a full top-to-bottom decode has populated pixels
    uint16_t width;
    uint16_t height;
    uint16_t paletteCount;
    uint16_t palette[RLE_MAX_PALETTE];   // 512 B -- self-contained, independent of s_meta's
                                          // cache state so a pixel-cache hit never depends on
                                          // whether s_meta currently happens to hold this tile
    uint8_t* pixels;              // width*height palette-index bytes, or NULL if not allocated
} PixelCacheSlot_t;

static PixelCacheSlot_t s_pixelCache[RLE_PIXEL_CACHE_SLOTS];
static uint8_t          s_pixelCacheNextSlot = 0;   // simple round-robin replacement

void lv_img_rle_cache_init()
{
    for (int i = 0; i < RLE_PIXEL_CACHE_SLOTS; i++)
    {
        s_pixelCache[i].path[0] = '\0';
        s_pixelCache[i].valid   = false;

        if (s_pixelCache[i].pixels == NULL)
        {
            uint32_t bytes = (uint32_t)RLE_MAX_TILE_WIDTH * RLE_MAX_TILE_WIDTH;
            s_pixelCache[i].pixels = (uint8_t*)lv_mem_alloc(bytes);

            if (s_pixelCache[i].pixels == NULL)
            {
                // 分配失败：不是致命错误，之后所有绘制都会自动走没有像素
                // 缓存的旧路径（正常解码），只是少了这部分加速。
                LV_LOG_WARN("RLE: pixel cache slot %d alloc failed (%u bytes) -- "
                            "caching disabled for this slot, falling back to normal decode",
                            i, (unsigned)bytes);
            }
        }
    }
    s_pixelCacheNextSlot = 0;
}

void lv_img_rle_cache_deinit()
{
    for (int i = 0; i < RLE_PIXEL_CACHE_SLOTS; i++)
    {
        if (s_pixelCache[i].pixels != NULL)
        {
            lv_mem_free(s_pixelCache[i].pixels);
            s_pixelCache[i].pixels = NULL;
        }
        s_pixelCache[i].path[0] = '\0';
        s_pixelCache[i].valid   = false;
    }
}

// Find an existing valid cache slot for this path, or NULL if not cached.
static PixelCacheSlot_t* pixel_cache_find(const char* src)
{
    for (int i = 0; i < RLE_PIXEL_CACHE_SLOTS; i++)
    {
        if (s_pixelCache[i].pixels != NULL
            && s_pixelCache[i].valid
            && strcmp(s_pixelCache[i].path, src) == 0)
        {
            return &s_pixelCache[i];
        }
    }
    return NULL;
}

// Claim a slot to (re)populate for this path. Round-robin eviction -- simple
// and correctness-preserving (a stale slot is just invalidated + overwritten),
// which is all a 1-2 slot cache needs; no LRU bookkeeping overhead.
static PixelCacheSlot_t* pixel_cache_claim(const char* src)
{
    PixelCacheSlot_t* slot = NULL;

    // Advance round-robin until it lands on a slot that actually has an
    // allocated buffer -- with more than one slot and a partial allocation
    // failure at init, some slots may have pixels == NULL, and picking
    // one of those would crash the caller on the first pixel write.
    for (int tries = 0; tries < RLE_PIXEL_CACHE_SLOTS; tries++)
    {
        PixelCacheSlot_t* candidate = &s_pixelCache[s_pixelCacheNextSlot];
        s_pixelCacheNextSlot = (s_pixelCacheNextSlot + 1) % RLE_PIXEL_CACHE_SLOTS;

        if (candidate->pixels != NULL)
        {
            slot = candidate;
            break;
        }
    }

    if (slot == NULL)
    {
        return NULL;  // no allocated slots at all (alloc failed at init time)
    }

    slot->valid = false;  // mark invalid until the full decode below completes
    size_t slen = strlen(src);
    if (slen >= sizeof(slot->path)) slen = sizeof(slot->path) - 1;
    memcpy(slot->path, src, slen);
    slot->path[slen] = '\0';
    return slot;
}

// =========================================================================
// Buffered run-stream reader
// =========================================================================
// Wraps s_bundle_file; tracks absolute bundle-file position and the tile's
// end boundary so reads never escape this tile's byte range.
// =========================================================================
typedef struct {
    uint32_t abs_pos;             // absolute bundle-file offset of next byte to read
    uint32_t tile_end;            // absolute bundle-file offset just past this tile
    uint8_t  buf[RLE_READ_BUF_SIZE];
    uint32_t pos;                 // next unread byte index within buf
    uint32_t filled;              // valid byte count in buf
    bool     error;
} RleReader_t;

// Refills reader buffer from s_bundle_file. Returns false on EOF or error.
static bool reader_refill(RleReader_t* r)
{
    uint32_t avail = r->tile_end - r->abs_pos;
    if (avail == 0)
    {
        r->filled = 0;
        r->pos    = 0;
        return false;
    }

    uint32_t want = (avail < RLE_READ_BUF_SIZE) ? avail : RLE_READ_BUF_SIZE;
    uint32_t br   = 0;
    if (lv_fs_read(&s_bundle_file, r->buf, want, &br) != LV_FS_RES_OK || br == 0)
    {
        r->filled = 0;
        r->pos    = 0;
        r->error  = true;
        return false;
    }

    r->abs_pos += br;
    r->filled   = br;
    r->pos      = 0;
    return true;
}

// Seeks s_bundle_file to abs_pos and initialises the reader for this tile.
static bool reader_seek(RleReader_t* r, uint32_t abs_pos, uint32_t tile_end)
{
    r->abs_pos  = abs_pos;
    r->tile_end = tile_end;
    r->pos      = 0;
    r->filled   = 0;
    r->error    = false;

    if (lv_fs_seek(&s_bundle_file, abs_pos, LV_FS_SEEK_SET) != LV_FS_RES_OK)
    {
        r->error = true;
        return false;
    }
    return true;
}

// Returns one (run_len, palette_idx) pair from the reader.
// Handles the rare split-pair case when a pair straddles a buffer boundary.
static inline bool reader_read_pair(RleReader_t* r, uint8_t* run_len, uint8_t* idx)
{
    // Fast path: both bytes already in buffer
    if (r->pos + 1 < r->filled)
    {
        *run_len = r->buf[r->pos++];
        *idx     = r->buf[r->pos++];
        return true;
    }

    // Slow path: at most one byte remains; carry it and refill
    uint8_t carry       = 0;
    bool    has_carry   = (r->pos < r->filled);
    if (has_carry)
    {
        carry = r->buf[r->pos];
    }

    if (!reader_refill(r) || r->filled == 0)
    {
        return false;
    }

    if (has_carry)
    {
        *run_len = carry;
        *idx     = r->buf[r->pos++];
    }
    else
    {
        if (r->filled < 2) return false;
        *run_len = r->buf[r->pos++];
        *idx     = r->buf[r->pos++];
    }
    return true;
}

// =========================================================================
// Ensure s_bundle_file is open on bundle_path; reuse if already open on it.
// =========================================================================
static bool ensure_bundle_open(const char* bundle_path)
{
    if (s_bundle_valid && strcmp(s_bundle_path, bundle_path) == 0)
    {
        return true;  // common case: same file as last draw
    }

    if (s_bundle_valid)
    {
        lv_fs_close(&s_bundle_file);
        s_bundle_valid = false;
    }

    if (lv_fs_open(&s_bundle_file, bundle_path, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }

    strncpy(s_bundle_path, bundle_path, sizeof(s_bundle_path) - 1);
    s_bundle_path[sizeof(s_bundle_path) - 1] = '\0';
    s_bundle_valid = true;
    return true;
}

// =========================================================================
// Path parsing
// =========================================================================
// Input:  full src string, e.g. "B:/MAP/16/53354/28462.rle"
// Output: bundle_path_out = "/MAP/16/533_284.tbnd" (on the SD drive)
//         *local_x = 54, *local_y = 62
//
// Strips the drive-letter prefix (anything up to and including ':'), then
// splits the remaining "/<prefix>/<level>/<tileX>/<tileY>.ext" into parts.
// The bundle path is formed on the SD drive directly (no virtual drive
// letter prefix) because the SD driver is registered under TILE_SD_DRIVE_LETTER
// which is also the first character of every absolute path, e.g. "/MAP/...".
// =========================================================================
static bool parse_tile_path(const char* src,
                             char* bundle_path_out, size_t bundle_path_max,
                             int* local_x, int* local_y)
{
    // Strip everything up to and including ':' (the virtual drive letter).
    const char* colon = strchr(src, ':');
    const char* path  = (colon != NULL) ? (colon + 1) : src;

    char buf[96];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf))
    {
        return false;
    }
    strcpy(buf, path);

    // Work right-to-left, cutting at each slash to isolate components.
    char* slash3 = strrchr(buf, '/');           // points before "<tileY>.ext"
    if (!slash3) return false;
    *slash3 = '\0';
    const char* tileY_str = slash3 + 1;        // "28462.rle" -- atoi ignores ".rle"

    char* slash2 = strrchr(buf, '/');           // points before "<tileX>"
    if (!slash2) return false;
    *slash2 = '\0';
    const char* tileX_str = slash2 + 1;        // "53354"

    char* slash1 = strrchr(buf, '/');           // points before "<level>"
    if (!slash1) return false;
    *slash1 = '\0';
    const char* level_str = slash1 + 1;        // "16"
    // buf is now the prefix: "/MAP"

    int tile_x = atoi(tileX_str);
    int tile_y = atoi(tileY_str);
    int level  = atoi(level_str);

    int block_x = tile_x / BUNDLE_BLOCK_SIZE;
    int block_y = tile_y / BUNDLE_BLOCK_SIZE;
    *local_x    = tile_x % BUNDLE_BLOCK_SIZE;
    *local_y    = tile_y % BUNDLE_BLOCK_SIZE;

    int written = snprintf(bundle_path_out, bundle_path_max,
                           "%s/%d/%d_%d.tbnd",
                           buf, level, block_x, block_y);
    return (written > 0 && (size_t)written < bundle_path_max);
}

// =========================================================================
// Bundle index lookup
// =========================================================================
// Parses src → opens the right .tbnd → reads the 8-byte index entry →
// returns tile_start and tile_length for the RLE2 stream in the bundle.
// Returns false if the tile is absent (not an error) or on I/O failure.
// =========================================================================
static bool open_tile(const char* src,
                      uint32_t* tile_start_out,
                      uint32_t* tile_length_out)
{
    char bundle_path[96];
    int  local_x = 0, local_y = 0;
    if (!parse_tile_path(src, bundle_path, sizeof(bundle_path), &local_x, &local_y))
    {
        LV_LOG_WARN("RLE: cannot parse path '%s'", src);
        return false;
    }

    if (!ensure_bundle_open(bundle_path))
    {
        // Either the bundle block genuinely doesn't exist for this area of
        // the map, or there is a map-directory configuration mismatch.
        LV_LOG_WARN("RLE: cannot open bundle '%s' (from '%s')", bundle_path, src);
        return false;
    }

    // Index entry for this tile is at a fixed offset within the bundle header.
    uint32_t index_pos = BUNDLE_HEADER_SIZE
                         + (uint32_t)(local_y * BUNDLE_BLOCK_SIZE + local_x) * 8u;

    uint8_t  entry[8];
    uint32_t br = 0;
    if (lv_fs_seek(&s_bundle_file, index_pos, LV_FS_SEEK_SET) != LV_FS_RES_OK
        || lv_fs_read(&s_bundle_file, entry, sizeof(entry), &br) != LV_FS_RES_OK
        || br != sizeof(entry))
    {
        LV_LOG_WARN("RLE: index read failed in '%s'", bundle_path);
        return false;
    }

    uint32_t offset = (uint32_t)entry[0]        | ((uint32_t)entry[1] << 8)
                    | ((uint32_t)entry[2] << 16) | ((uint32_t)entry[3] << 24);
    uint32_t length = (uint32_t)entry[4]        | ((uint32_t)entry[5] << 8)
                    | ((uint32_t)entry[6] << 16) | ((uint32_t)entry[7] << 24);

    if (offset == ABSENT_OFFSET || length == 0)
    {
        return false;  // tile absent -- not an error, caller returns LV_RES_OK
    }

    uint32_t data_section_start = BUNDLE_HEADER_SIZE
                                   + (uint32_t)BUNDLE_BLOCK_SIZE * BUNDLE_BLOCK_SIZE * 8u;
    *tile_start_out  = data_section_start + offset;
    *tile_length_out = length;
    return true;
}

// =========================================================================
// RLE2 metadata load (with cache)
// =========================================================================
// Reads (or reuses from s_meta) the header, palette, and checkpoint table.
// s_bundle_file must be seeked to tile_start before calling this on a
// cache miss; on a cache hit nothing is read from the file.
// =========================================================================
static bool load_meta(const char* src, uint32_t tile_start, uint32_t tile_length)
{
    // Cache hit: same path → same tile → metadata hasn't changed
    if (s_meta.valid && strcmp(s_meta.path, src) == 0)
    {
        return true;
    }

    // Cache miss: read header
    uint8_t  header[RLE_HEADER_SIZE];
    uint32_t br = 0;
    if (lv_fs_read(&s_bundle_file, header, sizeof(header), &br) != LV_FS_RES_OK
        || br != sizeof(header)
        || memcmp(header, RLE_MAGIC, 4) != 0)
    {
        LV_LOG_WARN("RLE: bad RLE2 header in '%s'", src);
        s_meta.valid = false;
        return false;
    }

    uint16_t width              = (uint16_t)(header[4]  | ((uint16_t)header[5]  << 8));
    uint16_t height             = (uint16_t)(header[6]  | ((uint16_t)header[7]  << 8));
    uint16_t paletteCount       = (uint16_t)(header[8]  | ((uint16_t)header[9]  << 8));
    uint16_t checkpointInterval = (uint16_t)(header[10] | ((uint16_t)header[11] << 8));
    uint16_t checkpointCount    = (uint16_t)(header[12] | ((uint16_t)header[13] << 8));

    if (paletteCount == 0 || paletteCount > RLE_MAX_PALETTE)
    {
        LV_LOG_WARN("RLE: bad palette count %d in '%s'", paletteCount, src);
        s_meta.valid = false;
        return false;
    }

    // Require exact match on tile dimensions to catch encoder/decoder
    // mismatches early rather than silently writing garbage pixels.
    if (width != RLE_MAX_TILE_WIDTH || height != RLE_MAX_TILE_WIDTH)
    {
        LV_LOG_WARN("RLE: unexpected tile size %dx%d in '%s' (expected %dx%d)",
                    width, height, src, RLE_MAX_TILE_WIDTH, RLE_MAX_TILE_WIDTH);
        s_meta.valid = false;
        return false;
    }

    if (checkpointInterval == 0
        || checkpointCount == 0
        || checkpointCount > RLE_MAX_CHECKPOINTS)
    {
        LV_LOG_WARN("RLE: bad checkpoint info (interval=%d count=%d) in '%s'",
                    checkpointInterval, checkpointCount, src);
        s_meta.valid = false;
        return false;
    }

    if (lv_fs_read(&s_bundle_file, s_meta.palette,
                   (uint32_t)paletteCount * sizeof(uint16_t), &br) != LV_FS_RES_OK
        || br != (uint32_t)paletteCount * sizeof(uint16_t))
    {
        LV_LOG_WARN("RLE: truncated palette in '%s'", src);
        s_meta.valid = false;
        return false;
    }

    if (lv_fs_read(&s_bundle_file, s_meta.checkpoints,
                   (uint32_t)checkpointCount * sizeof(uint32_t), &br) != LV_FS_RES_OK
        || br != (uint32_t)checkpointCount * sizeof(uint32_t))
    {
        LV_LOG_WARN("RLE: truncated checkpoint table in '%s'", src);
        s_meta.valid = false;
        return false;
    }

    s_meta.tile_start         = tile_start;
    s_meta.tile_length        = tile_length;
    s_meta.width              = width;
    s_meta.height             = height;
    s_meta.paletteCount       = paletteCount;
    s_meta.checkpointInterval = checkpointInterval;
    s_meta.checkpointCount    = checkpointCount;
    s_meta.run_stream_start   = RLE_HEADER_SIZE
                                 + (uint32_t)paletteCount   * sizeof(uint16_t)
                                 + (uint32_t)checkpointCount * sizeof(uint32_t);

    size_t slen = strlen(src);
    if (slen >= sizeof(s_meta.path)) slen = sizeof(s_meta.path) - 1;
    memcpy(s_meta.path, src, slen);
    s_meta.path[slen] = '\0';
    s_meta.valid = true;
    return true;
}

// =========================================================================
// LVGL widget boilerplate
// =========================================================================

typedef struct {
    const lv_area_t* src_area;
    const lv_area_t* disp_area;
    lv_color_t*      dest_buf;
} lv_img_rle_draw_dsc_t;

static void    lv_img_rle_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void    lv_img_rle_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void    lv_img_rle_event(const lv_obj_class_t* class_p, lv_event_t* e);
static lv_res_t lv_rle_draw(const char* src, lv_img_rle_draw_dsc_t* dsc);
static inline lv_color_t rgb565_to_lv_color(uint16_t c);

const lv_obj_class_t lv_img_rle_class =
{
    .base_class    = &lv_obj_class,
    .constructor_cb = lv_img_rle_constructor,
    .destructor_cb  = lv_img_rle_destructor,
    .event_cb       = lv_img_rle_event,
    .instance_size  = sizeof(lv_img_rle_t),
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

static void lv_img_rle_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
    LV_UNUSED(class_p);
    ((lv_img_rle_t*)obj)->src = NULL;
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
        lv_obj_t*      obj = lv_event_get_current_target(e);
        lv_img_rle_t*  img = (lv_img_rle_t*)obj;
        if (img->src == NULL) return;

        const lv_draw_ctx_t* draw_ctx = (const lv_draw_ctx_t*)lv_event_get_param(e);
        lv_coord_t buf_stride = lv_area_get_width(draw_ctx->buf_area);

        lv_area_t src_area = *draw_ctx->clip_area;
        lv_area_move(&src_area, -obj->coords.x1, -obj->coords.y1);

        lv_img_rle_draw_dsc_t dsc;
        dsc.dest_buf = (lv_color_t*)draw_ctx->buf
                       + (draw_ctx->clip_area->y1 - draw_ctx->buf_area->y1) * buf_stride
                       + (draw_ctx->clip_area->x1 - draw_ctx->buf_area->x1);
        dsc.src_area  = &src_area;
        dsc.disp_area = draw_ctx->buf_area;

        lv_rle_draw(img->src, &dsc);
    }
}

// =========================================================================
// rgb565_to_lv_color
// =========================================================================
static inline lv_color_t rgb565_to_lv_color(uint16_t c)
{
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >> 5)  & 0x3F;
    uint8_t b =  c        & 0x1F;
    return lv_color_make(r << 3, g << 2, b << 3);
}

// =========================================================================
// lv_rle_draw  --  the hot path
// =========================================================================
// Stack budget: line_buf (512 B for 256px lv_color_t) + RleReader_t (264 B)
// + locals ~ 800 B total.  Fine for Cortex-M4 with 224 KB RAM.
// =========================================================================
static lv_res_t lv_rle_draw(const char* src, lv_img_rle_draw_dsc_t* dsc)
{
    // ---- Fast path: pixel cache hit --------------------------------------
    // No bundle open, no SD read, no run-stream decode at all -- just a
    // palette-index -> RGB565 lookup and a row copy. This is what makes
    // repeat redraws of a tile (arrow/track-line/overlay moving on top of
    // an unchanged map) cheap.
    PixelCacheSlot_t* cached = pixel_cache_find(src);
    if (cached != NULL)
    {
        lv_coord_t disp_width = lv_area_get_width(dsc->disp_area);
        lv_coord_t blit_width = lv_area_get_width(dsc->src_area);
        lv_coord_t x_offset   = dsc->src_area->x1;
        lv_color_t line_buf[RLE_MAX_TILE_WIDTH];

        for (int row = dsc->src_area->y1; row <= dsc->src_area->y2; row++)
        {
            if (row < 0 || row >= cached->height)
            {
                continue;
            }

            const uint8_t* srcRow = &cached->pixels[(uint32_t)row * cached->width];
            for (lv_coord_t col = 0; col < blit_width; col++)
            {
                line_buf[col] = rgb565_to_lv_color(cached->palette[srcRow[x_offset + col]]);
            }

            lv_color_t* dest = dsc->dest_buf + (row - dsc->src_area->y1) * disp_width;
            lv_memcpy(dest, line_buf, (uint32_t)blit_width * sizeof(lv_color_t));
        }

        return LV_RES_OK;
    }

    // ---- Cache miss: normal path -----------------------------------------

    // ---- Step 1: bundle index lookup ------------------------------------
    uint32_t tile_start  = 0;
    uint32_t tile_length = 0;
    if (!open_tile(src, &tile_start, &tile_length))
    {
        // Tile absent from map data or bad path -- silent, not an error.
        return LV_RES_OK;
    }

    // ---- Step 2: metadata (header + palette + checkpoints) --------------
    // On a cache miss, the bundle file must be seeked to tile_start first
    // so load_meta() can read sequentially.  On a cache hit, the seek is
    // skipped entirely.
    if (!s_meta.valid || strcmp(s_meta.path, src) != 0)
    {
        if (lv_fs_seek(&s_bundle_file, tile_start, LV_FS_SEEK_SET) != LV_FS_RES_OK)
        {
            LV_LOG_WARN("RLE: seek to tile_start failed for '%s'", src);
            return LV_RES_INV;
        }
    }
    if (!load_meta(src, tile_start, tile_length))
    {
        return LV_RES_INV;
    }

    // ---- Step 3: claim a pixel-cache slot for this tile (may be null if -
    // caching is disabled / allocation failed at init -- that's fine, the
    // rest of this function works exactly as it did before the cache existed
    // in that case).
    PixelCacheSlot_t* slot = pixel_cache_claim(src);
    if (slot != NULL)
    {
        slot->width        = s_meta.width;
        slot->height       = s_meta.height;
        slot->paletteCount = s_meta.paletteCount;
        memcpy(slot->palette, s_meta.palette,
               (size_t)s_meta.paletteCount * sizeof(uint16_t));
    }

    // ---- Step 4: pick decode start row -----------------------------------
    // Populating the cache needs the FULL tile (row 0 through height-1) so
    // that any future clip region -- not just today's -- is a hit. Without
    // a slot to populate, keep the original optimization of starting from
    // the checkpoint nearest the current clip's top edge.
    int start_cp;
    if (slot != NULL)
    {
        start_cp = 0;
    }
    else
    {
        start_cp = dsc->src_area->y1 / (int)s_meta.checkpointInterval;
        if (start_cp >= (int)s_meta.checkpointCount)
        {
            start_cp = (int)s_meta.checkpointCount - 1;
        }
    }
    int row = start_cp * (int)s_meta.checkpointInterval;

    // ---- Step 5: seek to run stream and initialise buffered reader ------
    uint32_t stream_abs = s_meta.tile_start
                          + s_meta.run_stream_start
                          + s_meta.checkpoints[start_cp];
    uint32_t tile_end   = s_meta.tile_start + s_meta.tile_length;

    RleReader_t reader;
    if (!reader_seek(&reader, stream_abs, tile_end))
    {
        LV_LOG_WARN("RLE: checkpoint seek failed for '%s'", src);
        return LV_RES_INV;
    }

    // ---- Step 6: decode ---------------------------------------------------
    lv_color_t line_buf[RLE_MAX_TILE_WIDTH];

    uint16_t        width        = s_meta.width;
    uint16_t        height       = s_meta.height;
    uint16_t        paletteCount = s_meta.paletteCount;
    const uint16_t* palette      = s_meta.palette;

    uint32_t total_pixels = (uint32_t)width * height;
    uint32_t pixel_idx    = (uint32_t)row * width;
    int      col          = 0;

    lv_coord_t disp_width = lv_area_get_width(dsc->disp_area);
    lv_coord_t blit_width = lv_area_get_width(dsc->src_area);
    lv_coord_t x_offset   = dsc->src_area->x1;

    while (pixel_idx < total_pixels)
    {
        uint8_t run_len = 0, idx = 0;
        if (!reader_read_pair(&reader, &run_len, &idx))
        {
            LV_LOG_WARN("RLE: truncated run stream in '%s'", src);
            break;
        }

        if (run_len == 0 || idx >= paletteCount)
        {
            LV_LOG_WARN("RLE: corrupt run (len=%d idx=%d) in '%s'", run_len, idx, src);
            break;
        }

        lv_color_t c = rgb565_to_lv_color(palette[idx]);

        for (uint8_t i = 0; i < run_len && pixel_idx < total_pixels; i++)
        {
            line_buf[col++] = c;

            if (slot != NULL)
            {
                slot->pixels[pixel_idx] = idx;
            }

            pixel_idx++;

            if (col == width)
            {
                if (row >= dsc->src_area->y1 && row <= dsc->src_area->y2)
                {
                    lv_color_t* dest = dsc->dest_buf
                                       + (row - dsc->src_area->y1) * disp_width;
                    lv_memcpy(dest, &line_buf[x_offset],
                              (uint32_t)blit_width * sizeof(lv_color_t));
                }
                col = 0;
                row++;

                // Without a cache slot to populate, stop as soon as we've
                // covered the rows the caller actually asked for (original
                // behavior). With a slot, keep going to the tile's bottom
                // so the cache is fully usable for future, different clips.
                if (slot == NULL && row > dsc->src_area->y2)
                {
                    break;
                }
            }
        }
    }

    if (slot != NULL && pixel_idx >= total_pixels)
    {
        slot->valid = true;
    }

    return LV_RES_OK;
}
