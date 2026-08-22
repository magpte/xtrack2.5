#!/usr/bin/env python3
"""
tile_lz4_encode.py

Converts map tile images into the ultra-compact 8-bit palette + 32-row Chunked LZ4HC format
for X-Track's high-performance map engine.

Accepts PNG/JPG/JPEG/BIN sources.
Uses LZ4HC (High Compression) for optimal compression ratio while keeping MCU decompression
ultra-fast (<0.1ms per 32-row chunk).

File format (little-endian):
    4 bytes   magic "LZ42"
    2 bytes   width              (uint16 LE, e.g. 256)
    2 bytes   height             (uint16 LE, e.g. 256)
    2 bytes   paletteCount       (uint16 LE, 1-256)
    2 bytes   chunkInterval      (uint16 LE, e.g. 32 rows)
    2 bytes   chunkCount         (uint16 LE, e.g. 8 chunks)
    2 bytes   reserved           (uint16 LE, 0)
    paletteCount * 2 bytes       palette entries, RGB565 LE
    chunkCount * 4 bytes         chunk offset table (uint32 LE, relative to payload start)
    payload                      concatenated LZ4 compressed chunks
"""

import argparse
import os
import struct
import sys
import time
from concurrent.futures import ProcessPoolExecutor

try:
    from PIL import Image
except ImportError:
    print("This script requires Pillow: pip install Pillow --break-system-packages")
    sys.exit(1)

try:
    import numpy as np
except ImportError:
    print("This script requires numpy: pip install numpy --break-system-packages")
    sys.exit(1)

try:
    import lz4.block
except ImportError:
    print("This script requires lz4: pip install lz4 --break-system-packages")
    sys.exit(1)

MAGIC = b"LZ42"
MAX_PALETTE = 256
CHUNK_INTERVAL = 32  # rows per chunk (8 chunks for 256x256)
LV_IMG_CF_TRUE_COLOR = 4
LV_COLOR_16_SWAP = True


def rgb888_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def load_bin_tile(path):
    """Read an LVGL raw .bin tile (4-byte header + RGB565 pixels) and return PIL RGB Image."""
    with open(path, "rb") as f:
        header_bytes = f.read(4)
        if len(header_bytes) != 4:
            raise ValueError("file too short for an LVGL image header")

        header_val = struct.unpack("<I", header_bytes)[0]
        cf = header_val & 0x1F
        w = (header_val >> 10) & 0x7FF
        h = (header_val >> 21) & 0x7FF

        if cf != LV_IMG_CF_TRUE_COLOR:
            raise ValueError(f"unsupported color format cf={cf}")
        if w == 0 or h == 0:
            raise ValueError(f"bad dimensions in header: {w}x{h}")

        pixel_bytes = f.read(w * h * 2)
        if len(pixel_bytes) != w * h * 2:
            raise ValueError(f"truncated pixel data: {len(pixel_bytes)} bytes")

    raw = np.frombuffer(pixel_bytes, dtype="<u2").astype(np.uint32)
    if LV_COLOR_16_SWAP:
        raw = ((raw & 0xFF) << 8) | (raw >> 8)

    r5 = (raw >> 11) & 0x1F
    g6 = (raw >> 5) & 0x3F
    b5 = raw & 0x1F

    r8 = ((r5 << 3) | (r5 >> 2)).astype(np.uint8)
    g8 = ((g6 << 2) | (g6 >> 4)).astype(np.uint8)
    b8 = ((b5 << 3) | (b5 >> 2)).astype(np.uint8)

    rgb = np.stack([r8, g8, b8], axis=-1).reshape(h, w, 3)
    return Image.fromarray(rgb, "RGB")


def load_source_image(path):
    """Load a tile image from any supported source format."""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".bin":
        return load_bin_tile(path)
    img = Image.open(path)
    if img.mode != "RGB":
        img = img.convert("RGB")
    return img


def encode_image_bytes(img, lz4_compression_level=9):
    """Encodes a PIL RGB image into LZ42 binary format."""
    w, h = img.size
    if w <= 0 or h <= 0:
        raise ValueError(f"invalid image dimensions {w}x{h}")

    chunk_count = (h + CHUNK_INTERVAL - 1) // CHUNK_INTERVAL

    # Adaptive palette quantization (up to 256 colors)
    quantized = img.quantize(colors=MAX_PALETTE, method=Image.MEDIANCUT, dither=Image.NONE)
    palette_rgb = quantized.getpalette()  # [R0, G0, B0, R1, G1, B1, ...]

    pixels = np.array(quantized, dtype=np.uint8)
    used = np.unique(pixels)
    palette_count = len(used)

    if palette_count > MAX_PALETTE:
        raise ValueError(f"quantization produced {palette_count} colors (max {MAX_PALETTE})")

    # Remap to compact palette indices [0..palette_count-1]
    remap = np.zeros(MAX_PALETTE, dtype=np.uint8)
    for new_idx, old_idx in enumerate(used):
        remap[old_idx] = new_idx
    remapped_pixels = remap[pixels]

    # Compress each chunk of CHUNK_INTERVAL rows independently
    chunk_offsets = []
    compressed_payload = bytearray()

    for c in range(chunk_count):
        y_start = c * CHUNK_INTERVAL
        y_end = min(h, (c + 1) * CHUNK_INTERVAL)
        chunk_raw = remapped_pixels[y_start:y_end, :].tobytes()

        # High compression LZ4 (LZ4HC)
        comp = lz4.block.compress(
            chunk_raw,
            mode='high_compression',
            compression=lz4_compression_level,
            store_size=False
        )

        chunk_offsets.append(len(compressed_payload))
        compressed_payload.extend(comp)

    # Build binary output
    out = bytearray()
    out.extend(MAGIC)
    out.extend(struct.pack("<HHHHHH", w, h, palette_count, CHUNK_INTERVAL, chunk_count, 0))

    # Palette table (RGB565 LE)
    for old_idx in used.tolist():
        r = palette_rgb[old_idx * 3]
        g = palette_rgb[old_idx * 3 + 1]
        b = palette_rgb[old_idx * 3 + 2]
        out.extend(struct.pack("<H", rgb888_to_rgb565(r, g, b)))

    # Chunk offsets table
    for offset in chunk_offsets:
        out.extend(struct.pack("<I", offset))

    # Payload
    out.extend(compressed_payload)
    return bytes(out)


def encode_tile_bytes(src, lz4_compression_level=9):
    """Load and encode tile from file or PIL image object."""
    if isinstance(src, str):
        img = load_source_image(src)
    else:
        img = src
    w, h = img.size
    encoded = encode_image_bytes(img, lz4_compression_level=lz4_compression_level)
    raw_size = w * h * 2
    return raw_size, encoded


def encode_tile(src_path, dst_path, lz4_compression_level=9):
    raw_size, encoded = encode_tile_bytes(src_path, lz4_compression_level)
    with open(dst_path, "wb") as f:
        f.write(encoded)
    return raw_size, len(encoded)


def _encode_one(args):
    src_path, dst_path, level = args
    try:
        raw_size, lz4_size = encode_tile(src_path, dst_path, level)
        return (src_path, raw_size, lz4_size, None)
    except Exception as exc:
        return (src_path, 0, 0, str(exc))


def main():
    parser = argparse.ArgumentParser(description="Convert map tiles to Chunked LZ4HC format")
    parser.add_argument("input_dir", help="Directory containing source tiles")
    parser.add_argument("output_dir", help="Directory to write .lz4 tile files")
    parser.add_argument("--compression", type=int, default=9, help="LZ4HC compression level (1-12, default 9)")
    parser.add_argument("--workers", type=int, default=os.cpu_count(), help="Parallel worker count")
    args = parser.parse_args()

    src_dir, dst_dir = args.input_dir, args.output_dir
    all_src_paths = []
    for root, _dirs, files in os.walk(src_dir):
        for name in files:
            if name.lower().endswith((".png", ".jpg", ".jpeg", ".bin", ".image")):
                all_src_paths.append(os.path.join(root, name))

    total_files = len(all_src_paths)
    if total_files == 0:
        print(f"No tiles found in {src_dir}")
        return

    tasks = []
    for src_path in all_src_paths:
        rel = os.path.relpath(src_path, src_dir)
        dst_path = os.path.join(dst_dir, os.path.splitext(rel)[0] + ".lz4")
        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        tasks.append((src_path, dst_path, args.compression))

    print(f"Encoding {total_files} tiles using LZ4HC (level {args.compression}) with {args.workers} workers...")
    total_raw = total_lz4 = count = 0
    start = time.time()

    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        for src_path, raw_size, lz4_size, err in pool.map(_encode_one, tasks, chunksize=16):
            if err:
                print(f"Error {src_path}: {err}", file=sys.stderr)
                continue
            total_raw += raw_size
            total_lz4 += lz4_size
            count += 1

    elapsed = time.time() - start
    print(f"\nDone: {count}/{total_files} tiles encoded in {elapsed:.2f}s")
    if total_raw > 0:
        ratio = (total_lz4 / total_raw) * 100
        print(f"Raw RGB565: {total_raw / (1024*1024):.2f} MB -> LZ4HC: {total_lz4 / (1024*1024):.2f} MB ({ratio:.1f}%)")


if __name__ == "__main__":
    main()
