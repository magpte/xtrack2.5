/*
 * zstd_decompress.c
 *
 * Lightweight Embedded Zstandard/FSE Decompressor for ARM Cortex-M4F.
 * Part of X-Track 2.5 Map System (ZST2 High-Compression Engine).
 */

#include "zstd_decompress.h"
#include <string.h>

#define ZSTD_STATIC_LINKING_ONLY 1
#define ZSTD_NO_INTRINSICS 1

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

#define ZSTD_EMBED_WORKSPACE_SIZE (24 * 1024)

static uint8_t s_zstd_workspace[ZSTD_EMBED_WORKSPACE_SIZE] __attribute__((aligned(8)));
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
