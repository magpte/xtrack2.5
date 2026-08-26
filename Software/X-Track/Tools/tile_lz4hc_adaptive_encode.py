#!/usr/bin/env python3
"""
tile_lz4hc_adaptive_encode.py

Encodes a 256x256 map tile image into Adaptive 3-Tier Micro-Chunk format
compressed with LZ4HC (High Compression level 12).

Scheme:
  - 100% Bit-Exact RGB565 Lossless.
  - Per-chunk adaptive multi-tier palette & color decorrelation:
      * Tier 1 (unique colors <= 16): 4-bit packed indices + LZ4HC-12.
      * Tier 2 (17 <= unique colors <= 256): 8-bit indices + LZ4HC-12.
      * Tier 3 (unique colors > 256): 16-bit G-decorrelated raw words + LZ4HC-12.

Header:
  Offset  0: 4-byte magic "LZ4A" (or "LZ42")
  Offset  4: uint16 width (256)
  Offset  6: uint16 height (256)
  Offset  8: uint16 flags (0)
  Offset 10: uint16 chunk_interval (32)
  Offset 12: uint16 chunk_count (8)
  Offset 14: uint16 reserved (0)
  Offset 16: 8 x uint32 chunk offsets (32 bytes)
  Offset 48: 8 micro-chunk payloads
"""

import os
import sys
import struct
import numpy as np
from PIL import Image
import lz4.block

TILE_SIZE = 256
CHUNK_ROWS = 32
CHUNK_COUNT = TILE_SIZE // CHUNK_ROWS  # 8
MAGIC_LZ4A = b"LZ4A"
HEADER_SIZE = 16
CHUNK_TABLE_SIZE = CHUNK_COUNT * 4  # 32 bytes


def g_decorrelate_rgb565(px16):
    """Applies lossless Green-decorrelation transform:

    dr = (r - (g >> 1)) & 0x1F
    db = (b - (g >> 1)) & 0x1F
    Output word: (dr << 11) | (g << 5) | db
    """
    r5 = (px16 >> 11) & 0x1F
    g6 = (px16 >> 5) & 0x3F
    b5 = px16 & 0x1F
    half_g = g6 >> 1
    dr5 = (r5.astype(np.int32) - half_g.astype(np.int32)) & 0x1F
    db5 = (b5.astype(np.int32) - half_g.astype(np.int32)) & 0x1F
    return ((dr5 << 11) | (g6 << 5) | db5).astype(np.uint16)


def load_source_image(src):
    """Loads image from PIL Image or filepath and returns RGB PIL Image."""
    if isinstance(src, Image.Image):
        return src.convert("RGB")
    if isinstance(src, str):
        if not os.path.exists(src):
            raise FileNotFoundError(f"Tile image not found: {src}")
        return Image.open(src).convert("RGB")
    raise ValueError(f"Unsupported image input type: {type(src)}")


def encode_tile_bytes(img_or_path, lz4_compression_level=12):
    """Encodes a 256x256 image into an Adaptive LZ4HC tile byte stream.

    Returns:
        (raw_rgb565_bytes_size, encoded_tile_bytes)
    """
    img = load_source_image(img_or_path)
    if img.size != (TILE_SIZE, TILE_SIZE):
        img = img.resize((TILE_SIZE, TILE_SIZE), Image.Resampling.NEAREST)

    arr = np.array(img, dtype=np.uint32)
    # Convert 24-bit RGB to 16-bit RGB565 array
    r5 = (arr[:, :, 0] >> 3).astype(np.uint16)
    g6 = (arr[:, :, 1] >> 2).astype(np.uint16)
    b5 = (arr[:, :, 2] >> 3).astype(np.uint16)
    px16 = ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)

    raw_rgb565_size = TILE_SIZE * TILE_SIZE * 2  # 131,072 bytes (128 KB)

    chunk_payloads = []
    chunk_offsets = []
    current_payload_offset = 0

    for ci in range(CHUNK_COUNT):
        chunk_px = px16[ci * CHUNK_ROWS : (ci + 1) * CHUNK_ROWS, :]
        flat_px = chunk_px.flatten()

        # Find unique RGB565 colors in this 32-row strip
        unique_colors, inverse_indices = np.unique(flat_px, return_inverse=True)
        color_count = len(unique_colors)

        if color_count <= 16:
            # === Tier 1: 4-bit Micro-Palette (<= 16 colors) ===
            mode_byte = b"\x01"
            pal_cnt_byte = bytes([color_count])
            pal_bytes = unique_colors.astype("<u2").tobytes()

            # Pack two 4-bit indices into one byte (8192 indices -> 4096 bytes)
            i0 = inverse_indices[0::2].astype(np.uint8)
            i1 = inverse_indices[1::2].astype(np.uint8)
            packed_4bit = ((i0 << 4) | (i1 & 0x0F)).tobytes()

            comp_stream = lz4.block.compress(
                packed_4bit,
                mode="high_compression",
                compression=lz4_compression_level,
                store_size=False,
            )
            chunk_data = mode_byte + pal_cnt_byte + pal_bytes + comp_stream

        elif color_count <= 256:
            # === Tier 2: 8-bit Micro-Palette (17 ~ 256 colors) ===
            mode_byte = b"\x02"
            pal_cnt_byte = bytes([color_count - 1])
            pal_bytes = unique_colors.astype("<u2").tobytes()
            indices_8bit = inverse_indices.astype(np.uint8).tobytes()

            comp_stream = lz4.block.compress(
                indices_8bit,
                mode="high_compression",
                compression=lz4_compression_level,
                store_size=False,
            )
            chunk_data = mode_byte + pal_cnt_byte + pal_bytes + comp_stream

        else:
            # === Tier 3: 16-bit Green-Decorrelated Raw Words (> 256 colors) ===
            mode_byte = b"\x03"
            reserved_byte = b"\x00"
            gdec_words = g_decorrelate_rgb565(chunk_px).astype("<u2").tobytes()

            comp_stream = lz4.block.compress(
                gdec_words,
                mode="high_compression",
                compression=lz4_compression_level,
                store_size=False,
            )
            chunk_data = mode_byte + reserved_byte + comp_stream

        chunk_offsets.append(current_payload_offset)
        chunk_payloads.append(chunk_data)
        current_payload_offset += len(chunk_data)

    # Construct 16-byte Tile Header
    header = struct.pack(
        "<4sHHHHHH",
        MAGIC_LZ4A,
        TILE_SIZE,
        TILE_SIZE,
        0,           # flags
        CHUNK_ROWS,  # chunk_interval
        CHUNK_COUNT, # chunk_count
        0            # reserved
    )

    # Construct 32-byte Chunk Offset Table
    offset_table = b"".join(struct.pack("<I", off) for off in chunk_offsets)

    encoded_tile = header + offset_table + b"".join(chunk_payloads)
    return raw_rgb565_size, encoded_tile


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input_tile.png> <output_tile.lz4a>")
        sys.exit(1)

    in_file = sys.argv[1]
    out_file = sys.argv[2]

    raw_sz, data = encode_tile_bytes(in_file)
    with open(out_file, "wb") as f:
        f.write(data)

    print(f"Encoded '{in_file}' -> '{out_file}': {raw_sz/1024:.1f} KB -> {len(data)/1024:.1f} KB ({100*len(data)/raw_sz:.1f}%)")


if __name__ == "__main__":
    main()
