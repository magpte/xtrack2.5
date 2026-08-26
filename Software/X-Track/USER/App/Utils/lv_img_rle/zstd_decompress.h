/*
 * zstd_decompress.h
 *
 * Lightweight Embedded Zstandard/FSE Decompressor for ARM Cortex-M4F.
 * Part of X-Track 2.5 Map System (ZST2 High-Compression Engine).
 *
 * Features:
 *   - Zero dynamic heap allocations (100% static workspace).
 *   - Direct decompression into output buffer.
 *   - Supports standard Zstandard frames (windowLog <= 14, 16KB window).
 */

#ifndef __ZSTD_DECOMPRESS_H
#define __ZSTD_DECOMPRESS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize static Zstandard decompression context.
 */
void zstd_decompress_init(void);

/**
 * @brief Decompress a Zstandard frame into destination buffer without heap allocation.
 *
 * @param dst Pointer to destination buffer
 * @param dst_capacity Capacity of destination buffer in bytes
 * @param src Pointer to compressed Zstd frame data
 * @param src_size Size of compressed Zstd frame data in bytes
 * @return int Number of decompressed bytes written, or negative error code
 */
int zstd_decompress_chunk(void* dst, size_t dst_capacity, const void* src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif /* __ZSTD_DECOMPRESS_H */
