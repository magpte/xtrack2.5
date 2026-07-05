/*
 * lv_img_rle.h
 *
 * LVGL widget that decodes palette+RLE2 map tiles directly from .tbnd
 * bundle files on the SD card. Previously this widget was paired with a
 * separate TileBundleFS module (an lv_fs virtual driver) to bridge the
 * two. That bridge is gone: bundle index lookup and RLE decoding are now
 * one direct call path inside lv_img_rle.cpp.
 *
 * What this means for the rest of the project
 * -------------------------------------------
 *  - Public API is unchanged: lv_img_rle_create / lv_img_rle_set_src work
 *    exactly as before. LiveMapView.cpp and LiveMap.cpp need no edits.
 *  - TileBundleFS_Init() no longer exists. Remove its #include and call
 *    from lv_port.cpp (see lv_port.cpp in this patch set).
 *  - TileBundleFS.h and TileBundleFS.cpp can be deleted.
 *
 * Path format accepted by lv_img_rle_set_src():
 *   <drive>:/<prefix>/<level>/<tileX>/<tileY>.rle
 * e.g.  B:/MAP/16/53354/28462.rle
 * The drive-letter prefix (anything up to and including ':') is stripped
 * before parsing. The real .tbnd bundle file is opened directly on the SD
 * card drive (TILE_SD_DRIVE_LETTER in lv_img_rle.cpp; must match
 * SD_LETTER in lv_port_fs_sdfat.cpp).
 *
 * Pixel cache (new)
 * ------------------
 * lv_img_rle_cache_init() allocates a small pool of fully-decoded-tile
 * buffers (RLE_PIXEL_CACHE_SLOTS in lv_img_rle.cpp, 64KB each at the
 * default 256x256 tile size) so that redrawing a tile that's already been
 * decoded -- e.g. because something drawn on top of it moved -- is a
 * memory copy instead of a full RLE decode from the SD card.
 *
 * REQUIRED CALLER INTEGRATION: call lv_img_rle_cache_init() when the map
 * page becomes visible and lv_img_rle_cache_deinit() when it's hidden
 * (see LiveMap.cpp's onViewDidAppear/onViewWillDisappear), so the RAM is
 * only reserved while the map is actually on screen. If you don't call
 * lv_img_rle_cache_init() at all, the widget still works exactly as
 * before -- caching is simply inactive.
 */
#ifndef LV_IMG_RLE_H
#define LV_IMG_RLE_H

#include "lvgl/lvgl.h"

typedef struct {
    lv_obj_t obj;
    char*    src;
} lv_img_rle_t;

extern const lv_obj_class_t lv_img_rle_class;

lv_obj_t* lv_img_rle_create(lv_obj_t* parent);

void lv_img_rle_set_src(lv_obj_t* obj, const char* src);

// Allocate / free the pixel cache. See lv_img_rle.cpp for the RAM budget
// discussion (RLE_PIXEL_CACHE_SLOTS) before changing slot count. Safe to
// call cache_init() more than once (re-entrant no-op for already-allocated
// slots) and safe to never call it at all (caching then stays off).
void lv_img_rle_cache_init();
void lv_img_rle_cache_deinit();

#endif /* LV_IMG_RLE_H */
