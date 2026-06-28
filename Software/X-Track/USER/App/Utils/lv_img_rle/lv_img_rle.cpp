/*
 * lv_img_rle.cpp
 *
 * See lv_img_rle.h for background. The structure (constructor/destructor/
 * event handler, src string ownership) is copied directly from
 * lv_img_png.cpp so it drops into the same place in LiveMapView.cpp.
 * Only the decode routine (lv_rle_draw) is different.
 *
 * Format version RLE2: adds a row-checkpoint table so a partial redraw
 * (e.g. a small overlapping panel) only needs to seek-and-decode the rows
 * it actually needs, instead of always scanning the file from the top.
 * See tile_rle_encode.py for the matching encoder.
 */
#include "lv_img_rle.h"
#include <string.h>

#define MY_CLASS &lv_img_rle_class

#define RLE_MAGIC        "RLE2"
#define RLE_HEADER_SIZE  14
#define RLE_MAX_PALETTE  256

// Must be large enough to hold height / checkpointInterval entries. With
// the encoder's default 16-row interval and a 256px tile, that's 16 --
// this leaves headroom in case the interval is ever tightened.
#define RLE_MAX_CHECKPOINTS 32

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

        // draw_ctx->buf is NOT necessarily indexed from absolute screen
        // (0,0) -- in LVGL's normal (non-full_refresh, non-direct_mode)
        // partial rendering, draw_ctx->buf_area is the sub-rectangle of
        // the screen that the current refresh pass is filling, and every
        // standard LVGL draw call (see lv_draw_sw_blend.c) computes its
        // destination offset relative to *that*, not to the screen
        // origin. The previous version of this file always indexed from
        // (0,0), which only happened to be correct during a full-screen
        // refresh (where buf_area->y1 is 0 anyway) and silently wrote to
        // the wrong place in the same buffer for any smaller partial
        // redraw -- which is most of them. That mismatch between where
        // we wrote tile pixels and where LVGL's own blending later read
        // from is what caused the various "garbled"/"blank" symptoms.
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
        dsc.disp_area = draw_ctx->buf_area;  // row stride for blitting now comes from buf_area's width

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

// Header/palette/checkpoint table for the most recently drawn tile.
// Overlapping panels (active line, SportInfo, zoom indicator) each
// invalidate their own small region, and each one triggers a fresh
// LV_EVENT_DRAW_MAIN_BEGIN for any tile beneath them -- so the *same*
// tile routinely gets redrawn several times within one refresh burst.
// This metadata never changes for a given file, so caching just the
// metadata (not pixel data) for the single most-recently-used path
// skips 3 SD-card reads (header, palette, checkpoint table) on every
// one of those repeat calls. Pixel data itself is still always read
// fresh via decode_row() below, so this can't go stale mid-edit.
typedef struct
{
    char path[80];
    bool valid;
    uint16_t width;
    uint16_t height;
    uint16_t paletteCount;
    uint16_t checkpointInterval;
    uint16_t checkpointCount;
    uint32_t run_stream_start;
    uint16_t palette[RLE_MAX_PALETTE];
    uint32_t checkpoints[RLE_MAX_CHECKPOINTS];
} lv_img_rle_meta_t;

static lv_img_rle_meta_t s_meta_cache = { "", false, 0, 0, 0, 0, 0, 0, {0}, {0} };

static bool load_meta(lv_fs_file_t* f, const char* src, lv_img_rle_meta_t* m)
{
    if (m->valid && strcmp(m->path, src) == 0)
    {
        return true;  // same tile as last call -- skip the re-read entirely
    }

    uint8_t header[RLE_HEADER_SIZE];
    uint32_t br = 0;
    if (lv_fs_read(f, header, sizeof(header), &br) != LV_FS_RES_OK || br != sizeof(header)
        || memcmp(header, RLE_MAGIC, 4) != 0)
    {
        LV_LOG_WARN("RLE: bad header in %s", src);
        m->valid = false;
        return false;
    }

    uint16_t width              = (uint16_t)(header[4]  | (header[5]  << 8));
    uint16_t height              = (uint16_t)(header[6]  | (header[7]  << 8));
    uint16_t paletteCount        = (uint16_t)(header[8]  | (header[9]  << 8));
    uint16_t checkpointInterval  = (uint16_t)(header[10] | (header[11] << 8));
    uint16_t checkpointCount     = (uint16_t)(header[12] | (header[13] << 8));

    if (paletteCount == 0 || paletteCount > RLE_MAX_PALETTE)
    {
        LV_LOG_WARN("RLE: bad palette count in %s (palette=%d)", src, paletteCount);
        m->valid = false;
        return false;
    }

    // Require an exact match to the configured tile size, not just "fits".
    // A mismatched width here would mean the row buffer only gets filled
    // up to the file's (smaller) width each row, leaving stale stack
    // bytes in the rest of the row -- and those stale bytes could get
    // blitted to the screen if the visible clip area extends past that.
    if (width != RLE_MAX_TILE_WIDTH || height != RLE_MAX_TILE_WIDTH)
    {
        LV_LOG_WARN("RLE: unexpected tile size in %s (%dx%d, expected %dx%d)",
                     src, width, height, RLE_MAX_TILE_WIDTH, RLE_MAX_TILE_WIDTH);
        m->valid = false;
        return false;
    }

    if (checkpointInterval == 0 || checkpointCount == 0 || checkpointCount > RLE_MAX_CHECKPOINTS)
    {
        LV_LOG_WARN("RLE: bad checkpoint table in %s (interval=%d, count=%d)",
                     src, checkpointInterval, checkpointCount);
        m->valid = false;
        return false;
    }

    if (lv_fs_read(f, m->palette, (uint32_t)paletteCount * sizeof(uint16_t), &br) != LV_FS_RES_OK
        || br != paletteCount * sizeof(uint16_t))
    {
        LV_LOG_WARN("RLE: truncated palette in %s", src);
        m->valid = false;
        return false;
    }

    if (lv_fs_read(f, m->checkpoints, (uint32_t)checkpointCount * sizeof(uint32_t), &br) != LV_FS_RES_OK
        || br != checkpointCount * sizeof(uint32_t))
    {
        LV_LOG_WARN("RLE: truncated checkpoint table in %s", src);
        m->valid = false;
        return false;
    }

    m->width = width;
    m->height = height;
    m->paletteCount = paletteCount;
    m->checkpointInterval = checkpointInterval;
    m->checkpointCount = checkpointCount;
    m->run_stream_start = RLE_HEADER_SIZE
                           + (uint32_t)paletteCount * sizeof(uint16_t)
                           + (uint32_t)checkpointCount * sizeof(uint32_t);

    size_t len = strlen(src);
    if (len >= sizeof(m->path))
    {
        len = sizeof(m->path) - 1;
    }
    memcpy(m->path, src, len);
    m->path[len] = '\0';
    m->valid = true;

    return true;
}

// Total *stack* working memory for this function: one scanline
// (<=512B for a 256px tile) plus a few locals. The header/palette/
// checkpoint table live in the small persistent cache above instead of
// on the stack now, since they need to survive across calls.
static lv_res_t lv_rle_draw(const char* src, lv_img_rle_draw_dsc_t* dsc)
{
    lv_fs_file_t f;
    if (lv_fs_open(&f, src, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        LV_LOG_WARN("RLE: failed to open %s", src);
        return LV_RES_INV;
    }

    if (!load_meta(&f, src, &s_meta_cache))
    {
        lv_fs_close(&f);
        return LV_RES_INV;
    }

    uint16_t width = s_meta_cache.width;
    uint16_t height = s_meta_cache.height;
    uint16_t paletteCount = s_meta_cache.paletteCount;
    uint16_t checkpointInterval = s_meta_cache.checkpointInterval;
    uint16_t checkpointCount = s_meta_cache.checkpointCount;
    uint32_t run_stream_start = s_meta_cache.run_stream_start;
    uint16_t* palette = s_meta_cache.palette;
    uint32_t* checkpoints = s_meta_cache.checkpoints;
    uint32_t br = 0;

    // Only decode the rows we actually need: seek to the checkpoint at or
    // before the clip area's top edge, start counting rows from there, and
    // stop as soon as we pass the bottom edge -- rather than always
    // decoding the whole tile from row 0. This is what makes redrawing a
    // small overlapping region (e.g. a semi-transparent panel) cheap
    // instead of requiring a full sequential pass through the file.
    int start_checkpoint = dsc->src_area->y1 / checkpointInterval;
    if (start_checkpoint >= checkpointCount)
    {
        start_checkpoint = checkpointCount - 1;
    }
    int row = start_checkpoint * checkpointInterval;

    if (lv_fs_seek(&f, run_stream_start + checkpoints[start_checkpoint], LV_FS_SEEK_SET) != LV_FS_RES_OK)
    {
        LV_LOG_WARN("RLE: seek failed in %s", src);
        lv_fs_close(&f);
        return LV_RES_INV;
    }

    lv_color_t line_buf[RLE_MAX_TILE_WIDTH];
    lv_memset_00(line_buf, sizeof(line_buf));

    uint32_t total_pixels = (uint32_t)width * height;
    uint32_t pixel_idx = (uint32_t)row * width;
    int col = 0;

    lv_coord_t disp_width = lv_area_get_width(dsc->disp_area);
    lv_coord_t blit_width = lv_area_get_width(dsc->src_area);
    lv_coord_t x_offset = dsc->src_area->x1;

    uint8_t pair[2];
    while (pixel_idx < total_pixels && row <= dsc->src_area->y2)
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
                if (row > dsc->src_area->y2)
                {
                    break;  // already covered everything the caller needs
                }
            }
        }
    }

    lv_fs_close(&f);
    return LV_RES_OK;
}
