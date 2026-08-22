/*
 * lz4_decompress.h
 *
 * Ultra-lightweight LZ4 decompressor optimized for ARM Cortex-M4.
 * Part of X-Track 2.5 Map Tile Engine.
 */
#ifndef __LZ4_DECOMPRESS_H
#define __LZ4_DECOMPRESS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decompress known-valid LZ4 block with known uncompressed size.
 * Fast path (no input bounds check in inner loop).
 * 
 * @param src Pointer to compressed byte stream
 * @param dest Output buffer (must be at least originalSize bytes)
 * @param originalSize Exact uncompressed size (e.g. 8192 bytes for 32-row chunk)
 * @return int Number of bytes read from src, or negative on error
 */
int LZ4_decompress_fast(const char* src, char* dest, int originalSize);

/**
 * @brief Decompress LZ4 block safely with bounded input & output limits.
 * 
 * @param src Pointer to compressed byte stream
 * @param dest Output buffer
 * @param compressedSize Size of compressed input in bytes
 * @param maxDecompressedSize Maximum capacity of output buffer
 * @return int Number of decompressed bytes written to dest, or negative on error
 */
int LZ4_decompress_safe(const char* src, char* dest, int compressedSize, int maxDecompressedSize);

#ifdef __cplusplus
}
#endif

#endif /* __LZ4_DECOMPRESS_H */
