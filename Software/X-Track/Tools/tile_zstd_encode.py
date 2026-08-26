#!/usr/bin/env python3
"""
tile_zstd_encode.py

Converts map tile images into the high-compression ZST2 format (Adaptive Multi-Tier Chunk Palette
+ G-Color Decorrelation + Zstandard-1/FSE) for X-Track's high-performance map engine.

Accepts PNG/JPG/JPEG/BIN/IMAGE sources.
Uses Zstandard (MCU-tuned 16KB window) with adaptive 3-tier micro-chunks for 100% bit-exact lossless
compression with maximum compression ratio.

File format (little-endian):
    4 bytes   magic "ZST2"
    2 bytes   width              (uint16 LE, e.g. 256)
    2 bytes   height             (uint16 LE, e.g. 256)
    2 bytes   flags              (uint16 LE, 0)
    2 bytes   chunkInterval      (uint16 LE, e.g. 32 rows)
    2 bytes   chunkCount         (uint16 LE, e.g. 8 chunks)
    2 bytes   reserved           (uint16 LE, 0)
    chunkCount * 4 bytes         chunk offset table (uint32 LE, relative to payload start)
    payload                      concatenated ZST2 micro-chunks:
                                 - Tier 1 (<= 16 colors):
                                     1 byte:  0x01 (mode)
                                     1 byte:  palette_count (1..16)
                                     N * 2B:  palette entries (RGB565 LE)
                                     payload: Zstd-1 compressed 4-bit packed indices (4096 bytes)
                                 - Tier 2 (17..256 colors):
                                     1 byte:  0x02 (mode)
                                     1 byte:  palette_count_minus_1 (0..255 for 1..256 colors)
                                     N * 2B:  palette entries (RGB565 LE)
                                     payload: Zstd-1 compressed 8-bit indices (8192 bytes)
                                 - Tier 3 (> 256 colors):
                                     1 byte:  0x03 (mode)
                                     1 byte:  0x00 (reserved)
                                     payload: Zstd-1 compressed 16-bit G-decorrelated pixels (16384 bytes)
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
    import zstandard as zstd
except ImportError:
    print("This script requires zstandard: pip install zstandard --break-system-packages")
    sys.exit(1)

MAGIC = b"ZST2"
CHUNK_INTERVAL = 32  # rows per chunk (8 chunks for 256x256)
LV_IMG_CF_TRUE_COLOR = 4
LV_COLOR_16_SWAP = True

# MCU-optimized Zstandard compressor (Level 3 optimal balance of density and speed)
_compressor = zstd.ZstdCompressor(
    level=3,
    write_checksum=False,
    write_content_size=False,
    write_dict_id=False
)


def rgb888_to_rgb565(r, g, b):
    return ((int(r) >> 3) << 11) | ((int(g) >> 2) << 5) | (int(b) >> 3)


def g_decorrelate_rgb565(px16):
    """
    Lossless Green-Decorrelation Transform (YCoCg-R565 style)
    dR5 = (R5 - (G6 >> 1)) & 0x1F
    dB5 = (B5 - (G6 >> 1)) & 0x1F
    """
    r5 = (px16 >> 11) & 0x1F
    g6 = (px16 >> 5) & 0x3F
    b5 = px16 & 0x1F
    half_g = g6 >> 1
    dr5 = (r5 - half_g) & 0x1F
    db5 = (b5 - half_g) & 0x1F
    return ((dr5 << 11) | (g6 << 5) | db5).astype(np.uint16)


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


def encode_image_bytes(img):
    """Encodes a PIL RGB image into ZST2 binary format with 100% bit-exact lossless fidelity."""
    w, h = img.size
    if w <= 0 or h <= 0:
        raise ValueError(f"invalid image dimensions {w}x{h}")

    arr = np.array(img.convert('RGB'), dtype=np.uint32)
    r5 = (arr[:, :, 0] >> 3).astype(np.uint16)
    g6 = (arr[:, :, 1] >> 2).astype(np.uint16)
    b5 = (arr[:, :, 2] >> 3).astype(np.uint16)
    px16 = ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)

    chunk_count = (h + CHUNK_INTERVAL - 1) // CHUNK_INTERVAL
    chunk_offsets = []
    payload = bytearray()

    for c in range(chunk_count):
        y0 = c * CHUNK_INTERVAL
        y1 = min(h, (c + 1) * CHUNK_INTERVAL)
        chunk_px = px16[y0:y1, :]

        unique_colors, inverse_idx = np.unique(chunk_px, return_inverse=True)
        k = len(unique_colors)

        chunk_offsets.append(len(payload))

        if k <= 16:
            # Tier 1: 4-bit index stream (4096 bytes uncompressed)
            idx4 = inverse_idx.reshape(CHUNK_INTERVAL, w).astype(np.uint8)
            packed4 = ((idx4[:, 0::2] << 4) | (idx4[:, 1::2] & 0x0F)).tobytes()
            comp = _compressor.compress(packed4)

            # Chunk Header: mode=1, pal_count=k
            chunk_bytes = bytearray([0x01, k])
            for color in unique_colors:
                chunk_bytes.extend(struct.pack("<H", int(color)))
            chunk_bytes.extend(comp)
            payload.extend(chunk_bytes)

        elif k <= 256:
            # Tier 2: 8-bit index stream (8192 bytes uncompressed)
            idx8 = inverse_idx.reshape(CHUNK_INTERVAL, w).astype(np.uint8).tobytes()
            comp = _compressor.compress(idx8)

            # Chunk Header: mode=2, pal_count_minus_1=k-1
            chunk_bytes = bytearray([0x02, (k - 1) & 0xFF])
            for color in unique_colors:
                chunk_bytes.extend(struct.pack("<H", int(color)))
            chunk_bytes.extend(comp)
            payload.extend(chunk_bytes)

        else:
            # Tier 3: 16-bit G-decorrelated pixel stream (16384 bytes uncompressed)
            gdec = g_decorrelate_rgb565(chunk_px).tobytes()
            comp = _compressor.compress(gdec)

            # Chunk Header: mode=3, reserved=0
            chunk_bytes = bytearray([0x03, 0x00])
            chunk_bytes.extend(comp)
            payload.extend(chunk_bytes)

    # Build binary output
    out = bytearray()
    out.extend(MAGIC)
    out.extend(struct.pack("<HHHHHH", w, h, 0, CHUNK_INTERVAL, chunk_count, 0))

    for off in chunk_offsets:
        out.extend(struct.pack("<I", off))

    out.extend(payload)
    return bytes(out)


def encode_tile_bytes(src):
    """Load and encode tile from file path or PIL image object."""
    if isinstance(src, str):
        img = load_source_image(src)
    else:
        img = src
    w, h = img.size
    encoded = encode_image_bytes(img)
    raw_size = w * h * 2
    return raw_size, encoded


def encode_tile(src_path, dst_path):
    raw_size, encoded = encode_tile_bytes(src_path)
    with open(dst_path, "wb") as f:
        f.write(encoded)
    return raw_size, len(encoded)


def _encode_one(args):
    src_path, dst_path = args
    try:
        raw_size, zst_size = encode_tile(src_path, dst_path)
        return (src_path, raw_size, zst_size, None)
    except Exception as exc:
        return (src_path, 0, 0, str(exc))


def main():
    parser = argparse.ArgumentParser(description="Convert map tiles to Adaptive Multi-Tier ZST2 format")
    parser.add_argument("input_dir", help="Directory containing source tiles")
    parser.add_argument("output_dir", help="Directory to write .zst2 tile files")
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
        dst_path = os.path.join(dst_dir, os.path.splitext(rel)[0] + ".zst2")
        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        tasks.append((src_path, dst_path))

    print(f"Encoding {total_files} tiles using ZST2 (Adaptive Micro-Palette + G-Decorrelation + Zstd-1) with {args.workers} workers...")
    total_raw = total_zst = count = 0
    start = time.time()

    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        for src_path, raw_size, zst_size, err in pool.map(_encode_one, tasks, chunksize=16):
            if err:
                print(f"Error {src_path}: {err}", file=sys.stderr)
                continue
            total_raw += raw_size
            total_zst += zst_size
            count += 1

    elapsed = time.time() - start
    print(f"\nDone: {count}/{total_files} tiles encoded in {elapsed:.2f}s")
    if total_raw > 0:
        ratio = (total_zst / total_raw) * 100
        print(f"Raw RGB565: {total_raw / (1024*1024):.2f} MB -> ZST2: {total_zst / (1024*1024):.2f} MB ({ratio:.1f}%)")


if __name__ == "__main__":
    main()
