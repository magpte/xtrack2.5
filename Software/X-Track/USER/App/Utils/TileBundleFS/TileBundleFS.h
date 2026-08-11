/*
 * TileBundleFS.h
 *
 * Registers an LVGL filesystem driver (drive letter 'B') that makes
 * tiles packed into .tbnd bundle files (see tile_bundle.py) appear as
 * individual files to any code that just calls lv_fs_open()/read()/
 * seek()/close() -- in particular, lv_img_rle.cpp needs *zero* changes
 * to use bundled tiles instead of one-file-per-tile.
 *
 * Path convention: give this driver exactly the same virtual path you'd
 * have used for the real per-tile file, just with drive letter 'B'
 * instead of the SD card's own letter, e.g.:
 *     B:/MAP/16/53354/28462.rle
 * This driver parses out level/tileX/tileY, computes which bundle block
 * that falls into, opens the real bundle file on the SD card's own
 * drive, looks up the tile's (offset, length) in the bundle's index
 * table (a direct O(1) array lookup, no search), and presents just that
 * tile's byte range as if it were its own file.
 *
 * BLOCK_SIZE here must match the --block-size the PC-side tile_bundle.py
 * was run with (default 100 on both sides).
 */
#ifndef TILE_BUNDLE_FS_H
#define TILE_BUNDLE_FS_H

#include "lvgl/lvgl.h"

#define TILE_BUNDLE_BLOCK_SIZE 100
#define TILE_BUNDLE_DRIVE_LETTER 'B'

// Call once, after lv_port_fs_init() has run (this driver opens files
// through the standard lv_fs_open(), so the underlying SD driver must
// already be registered).
void TileBundleFS_Init(void);

// 96KB Tile LRU Cache diagnostic and control functions
void TileBundleFS_GetCacheStats(uint32_t* hits, uint32_t* misses, uint32_t* used_bytes);
void TileBundleFS_ClearCache(void);

#endif
