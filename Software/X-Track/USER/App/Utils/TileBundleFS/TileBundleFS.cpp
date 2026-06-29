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
    lv_fs_file_t bundle_file;  // the real, underlying bundle file
    uint32_t tile_start;       // absolute byte offset of this tile's data within the bundle file
    uint32_t tile_length;
    uint32_t cursor;           // virtual read position, 0..tile_length
} TileBundleFile_t;

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

    TileBundleFile_t* f = (TileBundleFile_t*)lv_mem_alloc(sizeof(TileBundleFile_t));
    if (f == NULL)
    {
        LV_LOG_ERROR("TileBundle: out of memory");
        return NULL;
    }

    if (lv_fs_open(&f->bundle_file, bundle_path, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        // Bundle file itself missing -- could be a legitimately empty
        // block (no tiles there at all), or could be a path/dirPath
        // mismatch. Logging this is cheap and is the fastest way to
        // tell those two cases apart while debugging.
        LV_LOG_WARN("TileBundle: could not open bundle file %s (from virtual path %s)",
                     bundle_path, path);
        lv_mem_free(f);
        return NULL;
    }

    uint32_t index_pos = BUNDLE_HEADER_SIZE
                          + (uint32_t)(local_y * TILE_BUNDLE_BLOCK_SIZE + local_x) * 8;

    uint8_t entry[8];
    uint32_t br = 0;
    if (lv_fs_seek(&f->bundle_file, index_pos, LV_FS_SEEK_SET) != LV_FS_RES_OK
        || lv_fs_read(&f->bundle_file, entry, sizeof(entry), &br) != LV_FS_RES_OK
        || br != sizeof(entry))
    {
        LV_LOG_WARN("TileBundle: index read failed in %s", bundle_path);
        lv_fs_close(&f->bundle_file);
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
        lv_fs_close(&f->bundle_file);
        lv_mem_free(f);
        return NULL;
    }

    uint32_t data_section_start = BUNDLE_HEADER_SIZE
                                   + (uint32_t)TILE_BUNDLE_BLOCK_SIZE * TILE_BUNDLE_BLOCK_SIZE * 8;
    f->tile_start = data_section_start + offset;
    f->tile_length = length;
    f->cursor = 0;

    if (lv_fs_seek(&f->bundle_file, f->tile_start, LV_FS_SEEK_SET) != LV_FS_RES_OK)
    {
        lv_fs_close(&f->bundle_file);
        lv_mem_free(f);
        return NULL;
    }

    return f;
}

static lv_fs_res_t bundle_fs_close(lv_fs_drv_t* drv, void* file_p)
{
    LV_UNUSED(drv);
    TileBundleFile_t* f = (TileBundleFile_t*)file_p;
    if (f)
    {
        lv_fs_close(&f->bundle_file);
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

    lv_fs_res_t res = lv_fs_read(&f->bundle_file, buf, to_read, br);
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

    return lv_fs_seek(&f->bundle_file, f->tile_start + f->cursor, LV_FS_SEEK_SET);
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
