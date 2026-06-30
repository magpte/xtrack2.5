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
 */
#include "TileBundleFS.h"
#include <string.h>
#include <stdlib.h>

#define BUNDLE_HEADER_SIZE 14
#define ABSENT_OFFSET 0xFFFFFFFFu

typedef struct
{
    uint32_t tile_start;       // absolute byte offset of this tile's data within the bundle file
    uint32_t tile_length;
    uint32_t cursor;           // virtual read position, 0..tile_length
} TileBundleFile_t;

// --- Shared, persistent bundle file handle ------------------------------
// IMPORTANT ASSUMPTION: this driver assumes only ONE virtual tile "file"
// is ever open at a time (i.e. the caller always fully reads and closes
// one tile before opening the next). This holds for how lv_img_rle.cpp
// actually uses it today (lv_rle_draw opens, decodes, and closes within
// a single synchronous call, and LVGL's draw events are not concurrent).
// If this driver is ever used somewhere that opens two RLE tiles at once
// before closing the first, this sharing would corrupt both reads --
// don't add a second concurrent caller without revisiting this.
//
// Why this exists: the same tile is often redrawn multiple times within
// one refresh burst (overlapping panels like SportInfo/zoom indicator/
// active line each invalidate their own small region, and each one
// triggers a redraw of whatever tile is underneath), and adjacent tiles
// share the same 100x100-tile bundle file. Without this cache, every
// single tile draw re-opens the underlying .tbnd file from scratch --
// a real FAT directory traversal -- even when it's the exact same file
// as the previous call. Keeping it open and only re-doing the cheap
// 8-byte index lookup for a new tile cuts that cost out for repeats and
// neighbors, which is the majority of real-world access patterns while
// panning/viewing a live map.
static lv_fs_file_t s_shared_bundle_file;
static char s_shared_bundle_path[96] = "";
static bool s_shared_bundle_valid = false;

// Parses ".../<level>/<tileX>/<tileY>.rle" out of `path`, computes which
// bundle block (tileX,tileY) falls into, and writes the real bundle
// file's path (same prefix, "<level>/<blockX>_<blockY>.tbnd") into
// `bundle_path_out`.
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
    // buf now holds just the prefix, e.g. "/MAP"

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

// Ensures s_shared_bundle_file is open and positioned on `bundle_path`.
// Reuses the already-open handle if it's already pointing at the same
// file (the common case); otherwise closes whatever was open and opens
// the new one.
static bool ensure_shared_bundle_open(const char* bundle_path)
{
    if (s_shared_bundle_valid && strcmp(s_shared_bundle_path, bundle_path) == 0)
    {
        return true;  // already open on the right file -- nothing to do
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

    char bundle_path[96];
    int local_x = 0, local_y = 0;
    if (!parse_virtual_path(path, bundle_path, sizeof(bundle_path), &local_x, &local_y))
    {
        LV_LOG_WARN("TileBundle: could not parse path %s", path);
        return NULL;
    }

    if (!ensure_shared_bundle_open(bundle_path))
    {
        // Bundle file itself missing -- could be a legitimately empty
        // block (no tiles there at all), or could be a path/dirPath
        // mismatch. Logging this is cheap and is the fastest way to
        // tell those two cases apart while debugging.
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
        // Tile genuinely doesn't exist in the map data -- not an error.
        lv_mem_free(f);
        return NULL;
    }

    uint32_t data_section_start = BUNDLE_HEADER_SIZE
                                   + (uint32_t)TILE_BUNDLE_BLOCK_SIZE * TILE_BUNDLE_BLOCK_SIZE * 8;
    f->tile_start = data_section_start + offset;
    f->tile_length = length;
    f->cursor = 0;

    if (lv_fs_seek(&s_shared_bundle_file, f->tile_start, LV_FS_SEEK_SET) != LV_FS_RES_OK)
    {
        LV_LOG_WARN("TileBundle: seek failed in %s", bundle_path);
        lv_mem_free(f);
        return NULL;
    }

    return f;
}

static lv_fs_res_t bundle_fs_close(lv_fs_drv_t* drv, void* file_p)
{
    LV_UNUSED(drv);
    // Deliberately does NOT close s_shared_bundle_file here -- it's kept
    // open so the next open() call can potentially reuse it (see
    // ensure_shared_bundle_open's comment above). Only the small
    // per-virtual-open state is freed.
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
    uint32_t to_read = (btr < remaining) ? btr : remaining;  // never read past this tile's own bytes

    lv_fs_res_t res = lv_fs_read(&s_shared_bundle_file, buf, to_read, br);
    if (res == LV_FS_RES_OK)
    {
        f->cursor += *br;
    }
    return res;
}

// Only LV_FS_SEEK_SET is exercised by lv_img_rle.cpp today; CUR/END are
// implemented for completeness but less thoroughly tested since nothing
// currently calls them on this driver.
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
    else  // LV_FS_SEEK_END
    {
        new_cursor = f->tile_length + pos;
    }

    if (new_cursor > f->tile_length)
    {
        new_cursor = f->tile_length;
    }
    f->cursor = new_cursor;

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
