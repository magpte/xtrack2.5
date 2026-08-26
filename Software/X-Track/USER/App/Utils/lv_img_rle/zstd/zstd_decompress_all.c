/*
 * zstd_decompress_all.c
 *
 * Single-compilation-unit amalgamation of Zstandard decompressor for embedded systems.
 * ARM Cortex-M4F: hardware CLZ/CTZ intrinsics enabled (ZSTD_NO_INTRINSICS NOT set).
 */
#define ZSTD_STATIC_LINKING_ONLY 1

#include "common/entropy_common.c"
#include "common/error_private.c"
#include "common/fse_decompress.c"
#include "common/zstd_common.c"
#include "common/xxhash.c"
#include "decompress/huf_decompress.c"
#include "decompress/zstd_ddict.c"
#include "decompress/zstd_decompress.c"
#include "decompress/zstd_decompress_block.c"
