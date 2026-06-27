/*
 * lv_img_rle.cpp
 *
 * See lv_img_rle.h for background. The structure (constructor/destructor/
 * event handler, src string ownership) is copied directly from
 * lv_img_png.cpp so it drops into the same place in LiveMapView.cpp.
 * Only the decode routine (lv_rle_draw) is different.
 */
#include "lv_img_rle.h"
#include "Common/HAL/HAL.h"
#include <string.h>
#include <stdio.h>

#define MY_CLASS &lv_img_rle_class

#define RLE_MAGIC        "RLE1"
#define RLE_HEADER_SIZE  10
#define RLE_MAX_PALETTE  256

// Match this to your actual tile width (256 for the standard OSM-style
// tile scheme this project uses). Encoding a wider tile than this will
// be rejected at decode time rather than overflowing a buffer.
#define RLE_MAX_TILE_WIDTH  256

typedef struct {
    const lv_area_t* src_area;
    const lv_area_t* disp_area;
    lv_color_t* dest_buf;
} lv_img_rle_draw_dsc_t;

static void lv_img_rle_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void lv_img_rle_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void lv_img_rle_event(const lv_obj_class_t* class_p, lv_event_t* e);
static lv_res_t lv_rle_draw(const char* src, lv_img_rle_draw_dsc_t* dsc);
static inline lv_color_t rgb565_to_lv_color(uint16_t c);

const lv_obj_class_t lv_img_rle_class =
{
    .base_class = &lv_obj_class,
    .constructor_cb = lv_img_rle_constructor,
    .destructor_cb = lv_img_rle_destructor,
    .event_cb = lv_img_rle_event,
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

static void lv_img_rle_event(const lv_obj_class_t* class_p, lv_event_t* e)
{
    LV_UNUSED(class_p);

    lv_res_t res;
    lv_event_code_t code = lv_event_get_code(e);

    if (code != LV_EVENT_DRAW_MAIN_BEGIN)
    {
        res = lv_obj_event_base(MY_CLASS, e);
        if (res != LV_RES_OK) return;
    }

    if (code == LV_EVENT_DRAW_MAIN_BEGIN)
    {
        lv_obj_t* obj = lv_event_get_current_target(e);
        lv_img_rle_t* img = (lv_img_rle_t*)obj;

        if (img->src == NULL)
        {
            return;
        }

        const lv_draw_ctx_t* draw_ctx = (const lv_draw_ctx_t*)lv_event_get_param(e);  
  
        // 使用 draw_ctx->buf_area 作为 buffer 的实际覆盖区域（partial rendering 安全）  
        lv_coord_t buf_stride = lv_area_get_width(draw_ctx->buf_area);  
        lv_color_t* buf_ptr = (lv_color_t*)draw_ctx->buf;  
  
        lv_area_t src_area;  
        src_area = *draw_ctx->clip_area;  
        lv_area_move(&src_area, -obj->coords.x1, -obj->coords.y1);  
  
        lv_img_rle_draw_dsc_t dsc;  
        dsc.dest_buf = buf_ptr  
                     + (draw_ctx->clip_area->y1 - draw_ctx->buf_area->y1) * buf_stride  
                     + (draw_ctx->clip_area->x1 - draw_ctx->buf_area->x1);  
        dsc.src_area = &src_area;  
        dsc.disp_area = draw_ctx->buf_area;  // stride 由 buf_area 的宽度决定

        lv_rle_draw(img->src, &dsc);
    }
}

static inline lv_color_t rgb565_to_lv_color(uint16_t c)
{
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >> 5)  & 0x3F;
    uint8_t b = c & 0x1F;
    // Re-expand to 8-bit per channel; lv_color_make() packs it back down
    // to whatever LV_COLOR_DEPTH is actually configured, so this is
    // correct regardless of color depth/swap settings.
    return lv_color_make(r << 3, g << 2, b << 3);
}

// Total working memory for this function: header (10B) + palette (<=512B)
// + one scanline (<=512B for a 256px tile) + a few locals, all on the
// stack. Nothing here is allocated for longer than this single call.
static lv_res_t lv_rle_draw(const char* src, lv_img_rle_draw_dsc_t* dsc)
{
    lv_fs_file_t f;
    if (lv_fs_open(&f, src, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        LV_LOG_WARN("RLE: failed to open %s", src);
        return LV_RES_INV;
    }

    uint8_t header[RLE_HEADER_SIZE];
    uint32_t br = 0;
    lv_fs_read(&f, header, sizeof(header), &br);

    if (br != sizeof(header) || memcmp(header, RLE_MAGIC, 4) != 0)
    {
        LV_LOG_WARN("RLE: bad header in %s", src);
        lv_fs_close(&f);
        return LV_RES_INV;
    }

    uint16_t width         = (uint16_t)(header[4] | (header[5] << 8));
    uint16_t height         = (uint16_t)(header[6] | (header[7] << 8));
    uint16_t paletteCount   = (uint16_t)(header[8] | (header[9] << 8));

    // --- TEMP DIAGNOSTIC LOGGING: remove once the garbling issue is found ---
    {
        char dbgLine[80];
        snprintf(dbgLine, sizeof(dbgLine), "[RLE] %s: %ux%u, palette=%u\r\n",
                 src, width, height, paletteCount);
        HAL::SD_WriteDebugLog(dbgLine);
    }
    // --- END TEMP DIAGNOSTIC LOGGING ---

    if (paletteCount == 0 || paletteCount > RLE_MAX_PALETTE)
    {
        LV_LOG_WARN("RLE: bad palette count in %s (palette=%d)", src, paletteCount);
        lv_fs_close(&f);
        return LV_RES_INV;
    }

    // Require an exact match to the configured tile size, not just "fits".
    // A mismatched width here would mean line_buf only gets filled up to
    // the file's (smaller) width each row, leaving stale stack bytes in
    // the rest of the row -- and those stale bytes could get blitted to
    // the screen if the visible clip area extends past that point.
    if (width != RLE_MAX_TILE_WIDTH || height != RLE_MAX_TILE_WIDTH)
    {
        LV_LOG_WARN("RLE: unexpected tile size in %s (%dx%d, expected %dx%d)",
                     src, width, height, RLE_MAX_TILE_WIDTH, RLE_MAX_TILE_WIDTH);
        lv_fs_close(&f);
        return LV_RES_INV;
    }

    uint16_t palette[RLE_MAX_PALETTE];
    lv_fs_read(&f, palette, (uint32_t)paletteCount * sizeof(uint16_t), &br);
    if (br != paletteCount * sizeof(uint16_t))
    {
        LV_LOG_WARN("RLE: truncated palette in %s", src);
        lv_fs_close(&f);
        return LV_RES_INV;
    }

    lv_color_t line_buf[RLE_MAX_TILE_WIDTH];
    lv_memset_00(line_buf, sizeof(line_buf));

    uint32_t total_pixels = (uint32_t)width * height;
    uint32_t pixel_idx = 0;
    int row = 0;
    int col = 0;

    lv_coord_t disp_width = lv_area_get_width(dsc->disp_area);
    lv_coord_t blit_width = lv_area_get_width(dsc->src_area);
    lv_coord_t x_offset = dsc->src_area->x1;

    uint8_t pair[2];
    while (pixel_idx < total_pixels)
    {
        if (lv_fs_read(&f, pair, sizeof(pair), &br) != LV_FS_RES_OK || br != sizeof(pair))
        {
            LV_LOG_WARN("RLE: truncated run stream in %s", src);
            break;  // bail cleanly on a corrupt/short file -- no crash
        }

        uint8_t run_len = pair[0];
        uint8_t idx = pair[1];

        if (run_len == 0 || idx >= paletteCount)
        {
            LV_LOG_WARN("RLE: corrupt run in %s", src);
            break;
        }

        lv_color_t c = rgb565_to_lv_color(palette[idx]);

        for (uint8_t i = 0; i < run_len && pixel_idx < total_pixels; i++)
        {
            line_buf[col] = c;
            col++;
            pixel_idx++;

            if (col == width)
            {
                if (row >= dsc->src_area->y1 && row <= dsc->src_area->y2)
                {
                    lv_color_t* dest = dsc->dest_buf + (row - dsc->src_area->y1) * disp_width;
                    lv_memcpy(dest, &line_buf[x_offset], blit_width * sizeof(lv_color_t));
                }
                col = 0;
                row++;
            }
        }
    }

    lv_fs_close(&f);
    return LV_RES_OK;
}
