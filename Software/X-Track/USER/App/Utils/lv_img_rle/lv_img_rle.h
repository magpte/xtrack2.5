/*
 * lv_img_rle.h
 *
 * Minimal LVGL image widget that decodes the custom palette + RLE tile
 * format produced by tile_rle_encode.py. Structured the same way as the
 * existing lv_img_png widget so it can be swapped in via the same
 * TILE_IMG_CREATE / TILE_IMG_SET_SRC macros in LiveMapView.cpp.
 *
 * Unlike lv_img_png (which carries PNGdec's ~47 KB static decoder
 * instance), this decoder needs only a small, stack-allocated working
 * set per draw call -- no permanently reserved RAM, no zlib window.
 */
#ifndef LV_IMG_RLE_H
#define LV_IMG_RLE_H

#include "lvgl/lvgl.h"

typedef struct {
    lv_obj_t obj;
    char* src;
} lv_img_rle_t;

extern const lv_obj_class_t lv_img_rle_class;

lv_obj_t* lv_img_rle_create(lv_obj_t* parent);

void lv_img_rle_set_src(lv_obj_t* obj, const char* src);

#endif
