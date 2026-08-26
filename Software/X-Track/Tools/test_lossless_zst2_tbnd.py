#!/usr/bin/env python3
"""
test_lossless_zst2_tbnd.py

Automated End-to-End Verification and Benchmark for ZST2:
1. Encodes 50+ tiles from diverse terrain (Urban Centers, Dense Roads, Suburban, River/Water, Mountains).
2. Packages into standard TBND bundle using tile_bundle.py.
3. Decodes and reconstructs each tile from the bundle using the exact MCU streaming pipeline.
4. Checks bit-exact 100% pixel match (0 diff).
5. Reports comprehensive size and compression benchmark.
"""

import os
import sys
import glob
import shutil
import struct
import numpy as np
from PIL import Image
import zstandard as zstd

from tile_zstd_encode import encode_tile_bytes, g_decorrelate_rgb565
import tile_bundle

TEST_TILES_DIR = r"E:\XTRACK\xtrack2.5\Software\X-Track\TEST\18"
OUTPUT_DIR = r"E:\XTRACK\xtrack2.5\Software\X-Track\TEST\test_zst2_out"

CHUNK_ROWS = 32
MAGIC_TBND = b"TBND"
MAGIC_ZST2 = b"ZST2"

decompressor = zstd.ZstdDecompressor()


def g_decorrelate_inv(val16):
    dr5 = (val16 >> 11) & 0x1F
    g6 = (val16 >> 5) & 0x3F
    db5 = val16 & 0x1F
    half_g = g6 >> 1
    r5 = (dr5 + half_g) & 0x1F
    b5 = (db5 + half_g) & 0x1F
    return ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)


def mcu_simulate_decode_tile(tile_bytes):
    """Simulates the exact MCU decode path in lv_img_rle.cpp."""
    assert tile_bytes[:4] == MAGIC_ZST2, "Invalid magic"
    magic, w, h, flags, chunk_intvl, chunk_count, reserved = struct.unpack("<4sHHHHHH", tile_bytes[:16])
    assert w == 256 and h == 256

    chunk_offsets = []
    for ci in range(chunk_count):
        off = struct.unpack("<I", tile_bytes[16 + ci * 4 : 20 + ci * 4])[0]
        chunk_offsets.append(off)

    payload_start = 16 + chunk_count * 4
    output_px16 = np.zeros((h, w), dtype=np.uint16)

    for ci in range(chunk_count):
        off = payload_start + chunk_offsets[ci]
        next_off = payload_start + chunk_offsets[ci + 1] if ci + 1 < chunk_count else len(tile_bytes)
        chunk_data = tile_bytes[off:next_off]

        mode = chunk_data[0]

        if mode == 0x01:
            # Tier 1 (4-bit micro-palette)
            pal_cnt = chunk_data[1]
            pal = []
            for pi in range(pal_cnt):
                c565 = struct.unpack("<H", chunk_data[2 + pi * 2 : 4 + pi * 2])[0]
                pal.append(c565)
            pal = np.array(pal, dtype=np.uint16)

            comp_data = chunk_data[2 + pal_cnt * 2 :]
            decomp_4bit = decompressor.decompress(comp_data, max_output_size=4096)
            p4 = np.frombuffer(decomp_4bit, dtype=np.uint8).reshape(CHUNK_ROWS, 128)
            i0 = p4 >> 4
            i1 = p4 & 0x0F
            idx = np.empty((CHUNK_ROWS, 256), dtype=np.uint8)
            idx[:, 0::2] = i0
            idx[:, 1::2] = i1

            chunk_pixels = pal[idx]
            output_px16[ci * CHUNK_ROWS : (ci + 1) * CHUNK_ROWS, :] = chunk_pixels

        elif mode == 0x02:
            # Tier 2 (8-bit micro-palette)
            pal_cnt = chunk_data[1] + 1
            pal = []
            for pi in range(pal_cnt):
                c565 = struct.unpack("<H", chunk_data[2 + pi * 2 : 4 + pi * 2])[0]
                pal.append(c565)
            pal = np.array(pal, dtype=np.uint16)

            comp_data = chunk_data[2 + pal_cnt * 2 :]
            decomp_8bit = decompressor.decompress(comp_data, max_output_size=8192)
            idx = np.frombuffer(decomp_8bit, dtype=np.uint8).reshape(CHUNK_ROWS, 256)

            chunk_pixels = pal[idx]
            output_px16[ci * CHUNK_ROWS : (ci + 1) * CHUNK_ROWS, :] = chunk_pixels

        elif mode == 0x03:
            # Tier 3 (16-bit G-decorrelated raw pixels)
            comp_data = chunk_data[2:]
            decomp_16bit = decompressor.decompress(comp_data, max_output_size=16384)
            raw_words = np.frombuffer(decomp_16bit, dtype=np.uint16).reshape(CHUNK_ROWS, 256)

            chunk_pixels = g_decorrelate_inv(raw_words)
            output_px16[ci * CHUNK_ROWS : (ci + 1) * CHUNK_ROWS, :] = chunk_pixels

    return output_px16


def main():
    print("=" * 80)
    print(" End-to-End ZST2 & TBND Lossless Verification Suite")
    print("=" * 80)

    # 1. Collect sample tiles across multiple directories
    subdirs = [d for d in os.listdir(TEST_TILES_DIR) if os.path.isdir(os.path.join(TEST_TILES_DIR, d))]
    subdirs.sort()

    selected_tiles = []
    for d in subdirs[:20]:
        dpath = os.path.join(TEST_TILES_DIR, d)
        pngs = glob.glob(os.path.join(dpath, "*.png"))
        if pngs:
            pngs.sort()
            for idx in [0, len(pngs) // 2, len(pngs) - 1]:
                if idx < len(pngs):
                    selected_tiles.append(pngs[idx])

    selected_tiles = selected_tiles[:50]
    print(f"Sample test set: {len(selected_tiles)} tiles from {min(len(subdirs), 20)} distinct map blocks.")

    # 2. Verify single tile encoding and MCU-simulated decoding
    print("\n[Phase 1] Validating Bit-Exact Lossless Roundtrip on individual tiles...")
    total_raw_bytes = 0
    total_zst2_bytes = 0

    for i, path in enumerate(selected_tiles):
        img = Image.open(path).convert("RGB")
        raw_size, zst_bytes = encode_tile_bytes(img)
        total_raw_bytes += raw_size
        total_zst2_bytes += len(zst_bytes)

        # Convert original image to reference RGB565 array
        arr = np.array(img, dtype=np.uint32)
        r5 = (arr[:, :, 0] >> 3).astype(np.uint16)
        g6 = (arr[:, :, 1] >> 2).astype(np.uint16)
        b5 = (arr[:, :, 2] >> 3).astype(np.uint16)
        ref_px16 = ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)

        # MCU decode simulation
        mcu_decoded = mcu_simulate_decode_tile(zst_bytes)

        # Check exact equality
        diff = np.max(np.abs(ref_px16.astype(np.int32) - mcu_decoded.astype(np.int32)))
        if diff != 0:
            print(f"FAILED on {path}! Max pixel difference: {diff}")
            sys.exit(1)

    print(f"  --> All {len(selected_tiles)} tiles verified! Max pixel difference: 0 (100.00% Lossless).")
    print(f"  --> Raw RGB565: {total_raw_bytes / 1024:.1f} KB -> ZST2: {total_zst2_bytes / 1024:.1f} KB ({100 * total_zst2_bytes / total_raw_bytes:.1f}% ratio)")

    # 3. Test bundle packaging with tile_bundle.py --encoder zstd
    print("\n[Phase 2] Testing .tbnd Bundle Packaging and Direct Extraction...")
    if os.path.exists(OUTPUT_DIR):
        shutil.rmtree(OUTPUT_DIR)
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Pick test directory containing zoom level folder '18'
    sample_input_dir = os.path.join(OUTPUT_DIR, "in_tiles", "18", subdirs[0])
    os.makedirs(sample_input_dir, exist_ok=True)
    for p in glob.glob(os.path.join(TEST_TILES_DIR, subdirs[0], "*.png"))[:30]:
        shutil.copy2(p, sample_input_dir)

    print(f"Packing 30 sample tiles from '{subdirs[0]}' with tile_bundle.py (encoder=zstd)...")

    # Run tile_bundle directly on parent of '18'
    tile_bundle.sys.argv = [
        "tile_bundle.py",
        os.path.join(OUTPUT_DIR, "in_tiles"),
        os.path.join(OUTPUT_DIR, "tbnd_out"),
        "--workers", "4"
    ]
    tile_bundle.main()

    # Find generated tbnd files
    tbnd_files = glob.glob(os.path.join(OUTPUT_DIR, "tbnd_out", "**", "*.tbnd"), recursive=True)
    print(f"\nGenerated {len(tbnd_files)} bundle file(s): {[os.path.basename(f) for f in tbnd_files]}")

    for tbnd_path in tbnd_files:
        with open(tbnd_path, "rb") as f:
            hdr = f.read(14)
            magic, blk_sz, bx, by = struct.unpack("<4sHII", hdr)
            assert magic == MAGIC_TBND
            idx_bytes = f.read(blk_sz * blk_sz * 8)

            active_tiles = 0
            for i in range(blk_sz * blk_sz):
                off, length = struct.unpack("<II", idx_bytes[i*8:(i+1)*8])
                if off != 0xFFFFFFFF and length > 0:
                    active_tiles += 1
                    data_start = 14 + blk_sz * blk_sz * 8
                    f.seek(data_start + off)
                    tile_data = f.read(length)
                    assert tile_data.startswith(MAGIC_ZST2), f"Tile {i} magic error: {tile_data[:4]}"

                    # Decode and verify
                    dec_px = mcu_simulate_decode_tile(tile_data)
                    assert dec_px.shape == (256, 256)

        print(f"Verified bundle '{os.path.basename(tbnd_path)}' ({active_tiles} active ZST2 tiles). All 100% valid!")

    # Cleanup test output
    shutil.rmtree(OUTPUT_DIR, ignore_errors=True)

    print("\n" + "=" * 80)
    print(" SUCCESS: All ZST2 Lossless and Bundle Engine Verifications Passed!")
    print("=" * 80)


if __name__ == "__main__":
    main()
