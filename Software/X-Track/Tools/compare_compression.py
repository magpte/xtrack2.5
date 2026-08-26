#!/usr/bin/env python3
"""
compare_compression.py

Comprehensive Comparison Benchmark Tool for X-Track 2.5 Map System:
Evaluates and compares:
  1. Raw RGB565 (128 KB per tile)
  2. Adaptive 3-Tier + G-Decorrelation + LZ4HC (Level 12)
  3. Adaptive 3-Tier + G-Decorrelation + Zstd (Level 1)
  4. Adaptive 3-Tier + G-Decorrelation + Zstd (Level 3)
  5. Adaptive 3-Tier + G-Decorrelation + Zstd (Level 9)

Usage:
    python compare_compression.py <tile_folder_or_images> [--max-tiles N]
"""

import os
import sys
import glob
import time
import struct
import numpy as np
from PIL import Image
import lz4.block
import zstandard as zstd

from tile_zstd_encode import g_decorrelate_rgb565, load_source_image

TILE_SIZE = 256
CHUNK_ROWS = 32
CHUNK_COUNT = 8


def encode_chunk_lz4hc(chunk_px, level=12):
    flat_px = chunk_px.flatten()
    unique_colors, inverse_indices = np.unique(flat_px, return_inverse=True)
    color_count = len(unique_colors)

    if color_count <= 16:
        tier = 1
        pal_bytes = unique_colors.astype("<u2").tobytes()
        i0 = inverse_indices[0::2].astype(np.uint8)
        i1 = inverse_indices[1::2].astype(np.uint8)
        packed_4bit = ((i0 << 4) | (i1 & 0x0F)).tobytes()
        comp = lz4.block.compress(packed_4bit, mode="high_compression", compression=level, store_size=False)
        chunk_data = b"\x01" + bytes([color_count]) + pal_bytes + comp
    elif color_count <= 256:
        tier = 2
        pal_bytes = unique_colors.astype("<u2").tobytes()
        indices_8bit = inverse_indices.astype(np.uint8).tobytes()
        comp = lz4.block.compress(indices_8bit, mode="high_compression", compression=level, store_size=False)
        chunk_data = b"\x02" + bytes([color_count - 1]) + pal_bytes + comp
    else:
        tier = 3
        gdec_words = g_decorrelate_rgb565(chunk_px).astype("<u2").tobytes()
        comp = lz4.block.compress(gdec_words, mode="high_compression", compression=level, store_size=False)
        chunk_data = b"\x03\x00" + comp

    return tier, chunk_data


def encode_chunk_zstd(chunk_px, compressor):
    flat_px = chunk_px.flatten()
    unique_colors, inverse_indices = np.unique(flat_px, return_inverse=True)
    color_count = len(unique_colors)

    if color_count <= 16:
        tier = 1
        pal_bytes = unique_colors.astype("<u2").tobytes()
        i0 = inverse_indices[0::2].astype(np.uint8)
        i1 = inverse_indices[1::2].astype(np.uint8)
        packed_4bit = ((i0 << 4) | (i1 & 0x0F)).tobytes()
        comp = compressor.compress(packed_4bit)
        chunk_data = b"\x01" + bytes([color_count]) + pal_bytes + comp
    elif color_count <= 256:
        tier = 2
        pal_bytes = unique_colors.astype("<u2").tobytes()
        indices_8bit = inverse_indices.astype(np.uint8).tobytes()
        comp = compressor.compress(indices_8bit)
        chunk_data = b"\x02" + bytes([color_count - 1]) + pal_bytes + comp
    else:
        tier = 3
        gdec_words = g_decorrelate_rgb565(chunk_px).astype("<u2").tobytes()
        comp = compressor.compress(gdec_words)
        chunk_data = b"\x03\x00" + comp

    return tier, chunk_data


def benchmark_directory(input_dir, max_tiles=100):
    all_files = []
    for root, _, files in os.walk(input_dir):
        for f in files:
            if f.lower().endswith((".png", ".jpg", ".jpeg", ".image")):
                all_files.append(os.path.join(root, f))

    if not all_files:
        print(f"No image files found in {input_dir}")
        return

    # Sample tiles
    if len(all_files) > max_tiles:
        step = max(1, len(all_files) // max_tiles)
        selected_files = all_files[::step][:max_tiles]
    else:
        selected_files = all_files

    print(f"Loaded {len(selected_files)} sample tiles for comparison benchmark...")

    c_zstd1 = zstd.ZstdCompressor(level=1, write_checksum=False, write_content_size=False, write_dict_id=False)
    c_zstd3 = zstd.ZstdCompressor(level=3, write_checksum=False, write_content_size=False, write_dict_id=False)
    c_zstd9 = zstd.ZstdCompressor(level=9, write_checksum=False, write_content_size=False, write_dict_id=False)

    total_raw = 0
    total_lz4hc = 0
    total_zstd1 = 0
    total_zstd3 = 0
    total_zstd9 = 0

    tier_counts = {1: 0, 2: 0, 3: 0}

    t0 = time.time()

    for idx, fpath in enumerate(selected_files):
        img = load_source_image(fpath)
        if img.size != (TILE_SIZE, TILE_SIZE):
            img = img.resize((TILE_SIZE, TILE_SIZE), Image.Resampling.NEAREST)

        arr = np.array(img, dtype=np.uint32)
        r5 = (arr[:, :, 0] >> 3).astype(np.uint16)
        g6 = (arr[:, :, 1] >> 2).astype(np.uint16)
        b5 = (arr[:, :, 2] >> 3).astype(np.uint16)
        px16 = ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)

        total_raw += TILE_SIZE * TILE_SIZE * 2

        # 48 bytes metadata header (16B header + 32B chunk offsets)
        tile_lz4hc = 48
        tile_zstd1 = 48
        tile_zstd3 = 48
        tile_zstd9 = 48

        for ci in range(CHUNK_COUNT):
            chunk_px = px16[ci * CHUNK_ROWS : (ci + 1) * CHUNK_ROWS, :]

            tier, d_lz4hc = encode_chunk_lz4hc(chunk_px, level=12)
            tier_counts[tier] += 1
            tile_lz4hc += len(d_lz4hc)

            _, d_zstd1 = encode_chunk_zstd(chunk_px, c_zstd1)
            tile_zstd1 += len(d_zstd1)

            _, d_zstd3 = encode_chunk_zstd(chunk_px, c_zstd3)
            tile_zstd3 += len(d_zstd3)

            _, d_zstd9 = encode_chunk_zstd(chunk_px, c_zstd9)
            tile_zstd9 += len(d_zstd9)

        total_lz4hc += tile_lz4hc
        total_zstd1 += tile_zstd1
        total_zstd3 += tile_zstd3
        total_zstd9 += tile_zstd9

    total_chunks = len(selected_files) * CHUNK_COUNT

    print("\n" + "=" * 80)
    print(" COMPREHENSIVE COMPRESSION BENCHMARK REPORT (100% BIT-EXACT LOSSLESS RGB565)")
    print("=" * 80)
    print(f"Sample Tiles Tested : {len(selected_files)} tiles ({total_chunks} micro-chunks)")
    print(f"Chunk Tier Breakdown:")
    print(f"  - Tier 1 (<= 16 colors)    : {tier_counts[1]} chunks ({100*tier_counts[1]/total_chunks:.1f}%) [4-bit packed micro-palette]")
    print(f"  - Tier 2 (17~256 colors)   : {tier_counts[2]} chunks ({100*tier_counts[2]/total_chunks:.1f}%) [8-bit compact micro-palette]")
    print(f"  - Tier 3 (> 256 colors)    : {tier_counts[3]} chunks ({100*tier_counts[3]/total_chunks:.1f}%) [16-bit G-decorrelation]")
    print("-" * 80)

    print(f"{'Compression Scheme':<36} | {'Total Size':<12} | {'Avg/Tile':<10} | {'Ratio vs Raw':<12} | {'Saving vs LZ4HC'}")
    print("-" * 80)

    print(f"{'Raw Uncompressed RGB565 (.bin)':<36} | {total_raw/1024/1024:>9.2f} MB | {total_raw/len(selected_files)/1024:>7.1f} KB | {100.0:>10.1f}% | {'---'}")
    print(f"{'Adaptive 3-Tier + LZ4HC-12':<36} | {total_lz4hc/1024/1024:>9.2f} MB | {total_lz4hc/len(selected_files)/1024:>7.1f} KB | {100*total_lz4hc/total_raw:>10.1f}% | {'Baseline (0.0%)'}")
    print(f"{'Adaptive 3-Tier + Zstd-1 (FSE)':<36} | {total_zstd1/1024/1024:>9.2f} MB | {total_zstd1/len(selected_files)/1024:>7.1f} KB | {100*total_zstd1/total_raw:>10.1f}% | {100*(1 - total_zstd1/total_lz4hc):>+10.1f}%")
    print(f"{'Adaptive 3-Tier + Zstd-3':<36} | {total_zstd3/1024/1024:>9.2f} MB | {total_zstd3/len(selected_files)/1024:>7.1f} KB | {100*total_zstd3/total_raw:>10.1f}% | {100*(1 - total_zstd3/total_lz4hc):>+10.1f}%")
    print(f"{'Adaptive 3-Tier + Zstd-9':<36} | {total_zstd9/1024/1024:>9.2f} MB | {total_zstd9/len(selected_files)/1024:>7.1f} KB | {100*total_zstd9/total_raw:>10.1f}% | {100*(1 - total_zstd9/total_lz4hc):>+10.1f}%")
    print("=" * 80)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python compare_compression.py <tile_folder> [max_tiles]")
        sys.exit(1)

    dir_path = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    benchmark_directory(dir_path, limit)
