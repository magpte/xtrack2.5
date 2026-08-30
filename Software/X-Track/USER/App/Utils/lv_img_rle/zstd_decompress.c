/*
 * zstd_decompress.c
 *
 * Lightweight Embedded Zstandard/FSE Decompressor for ARM Cortex-M4F.
 * Part of X-Track 2.5 Map System (ZST2 High-Compression Engine).
 */

#include "zstd_decompress.h"
#include <string.h>

/* Z4: 明示的にヒープ（malloc）パスを封鎖する。ZSTD_initStaticDCtx を使う
 * 限り実行時に malloc は呼ばれないが、ソースレベルで宣言することで
 * fallback パスへの意図しない到達をコンパイル時に排除する。 */
#define ZSTD_HEAPMODE 0

#ifndef ZSTD_STATIC_LINKING_ONLY
#define ZSTD_STATIC_LINKING_ONLY
#endif
/* Z2: タイル chunk は最大 16KB (windowLog=14) のため、
 * それより大きい window を要求するフレームは早期拒否する。
 * デフォルト値 27 (128MB) はこのユースケースに不要。 */
#define ZSTD_MAXWINDOWSIZE_DEFAULT (1 << 14)  /* 16 KB */
#define ZSTD_DECODER_INTERNAL_BUFFER 64
#define HUF_FORCE_DECOMPRESS_X1 1

#include "zstd/zstd.h"
#include "zstd/common/entropy_common.c"
#include "zstd/common/error_private.c"
#include "zstd/common/fse_decompress.c"
#include "zstd/common/zstd_common.c"
#include "zstd/common/xxhash.c"
#include "zstd/decompress/huf_decompress.c"
#include "zstd/decompress/zstd_ddict.c"
#include "zstd/decompress/zstd_decompress.c"
#include "zstd/decompress/zstd_decompress_block.c"

#define ZSTD_EMBED_WORKSPACE_SIZE (32 * 1024)

/* Z_SRAM2: s_zstd_workspace 显式放入 SRAM2（见 Objects/X-Track.sct RW_IRAM2）。
 * 原因：以 32KB workspace 时，其末地址 0x200500F8 越过 AT32F435 SRAM1/SRAM2 边界
 * 0x20050000，导致 AHB 总线矩阵 EDMA（SPI1 TX）与 CPU 之间产生仲裁延迟，
 * 造成非 LiveMap 页面（DIALPLATE / SystemInfos）掉帧。
 * 将 workspace 整体移至 SRAM2（0x20050000~0x2005FFFF，64KB），
 * SRAM1 完整保留给 LVGL 帧缓冲和内存池，问题彻底消除。
 * ZST2 workspace 仅在 lv_zst2_draw 解压 chunk 时读写，非 LiveMap 期间零访问，
 * 放入 SRAM2 对地图渲染性能无影响。 */
static uint8_t s_zstd_workspace[ZSTD_EMBED_WORKSPACE_SIZE]
    __attribute__((aligned(8), section(".sram2_bss")));
static ZSTD_DCtx* s_zstd_dctx = NULL;

void zstd_decompress_init(void)
{
    if (s_zstd_dctx == NULL)
    {
        s_zstd_dctx = ZSTD_initStaticDCtx(s_zstd_workspace, sizeof(s_zstd_workspace));
    }
}

int zstd_decompress_chunk(void* dst, size_t dst_capacity, const void* src, size_t src_size)
{
    if (s_zstd_dctx == NULL)
    {
        zstd_decompress_init();
    }
    if (s_zstd_dctx == NULL)
    {
        return -1;
    }

    size_t res = ZSTD_decompressDCtx(s_zstd_dctx, dst, dst_capacity, src, src_size);
    if (ZSTD_isError(res))
    {
        return -2;
    }
    return (int)res;
}
