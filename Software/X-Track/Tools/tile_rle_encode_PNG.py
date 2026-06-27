#!/usr/bin/env python3
"""
tile_rle_encode.py

Converts map tile images (PNG/JPG/etc.) into the compact palette + run-length
tile format used by the lv_img_rle LVGL widget.

Best suited for cartographic/vector-style map tiles (flat colors, few unique
hues) -- which is the typical style for offline bike-computer maps. Aerial
or photographic tiles will compress poorly and lose quality, since they get
quantized down to <=256 colors.

File format (little-endian):
    4 bytes   magic "RLE1"
    2 bytes   width
    2 bytes   height
    2 bytes   paletteCount   (1-256)
    paletteCount * 2 bytes   palette entries, RGB565
    then repeating 2-byte pairs: (run_len 1-255, palette_index) in raster
    order until width*height pixels are covered. A run can span across a
    row boundary.

Usage:
    python tile_rle_encode.py <input_dir> <output_dir>

<input_dir> is walked recursively; the same relative path/structure is
recreated under <output_dir> with the extension changed to .rle (so a
/14/3376/5432.png tile becomes /14/3376/5432.rle).
"""
import os
import struct
import sys
import time

try:
    from PIL import Image
except ImportError:
    print("This script requires Pillow: pip install Pillow --break-system-packages")
    sys.exit(1)

MAGIC = b"RLE1"
MAX_PALETTE = 256
MAX_RUN = 255


def rgb888_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def encode_tile(src_path, dst_path):
    img = Image.open(src_path).convert("RGB")
    w, h = img.size

    # Quantize down to <=256 colors. Cartographic tiles usually have far
    # fewer than 256 distinct colors already, so this step is often
    # lossless or near-lossless for that style of tile.
    pal_img = img.convert("P", palette=Image.ADAPTIVE, colors=MAX_PALETTE)
    palette_rgb = pal_img.getpalette()[: MAX_PALETTE * 3]
    indices = list(pal_img.getdata())  # one byte per pixel, row-major

    # Remap to only the colors actually used, so paletteCount can be < 256
    # and the palette table in the file stays as small as possible.
    used = sorted(set(indices))
    remap = {old: new for new, old in enumerate(used)}
    indices = [remap[i] for i in indices]
    palette_count = len(used)

    with open(dst_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<HHH", w, h, palette_count))

        for old_idx in used:
            r = palette_rgb[old_idx * 3]
            g = palette_rgb[old_idx * 3 + 1]
            b = palette_rgb[old_idx * 3 + 2]
            f.write(struct.pack("<H", rgb888_to_rgb565(r, g, b)))

        # Run-length encode the index stream. A run never exceeds MAX_RUN;
        # longer runs are simply split into consecutive pairs.
        i = 0
        n = len(indices)
        while i < n:
            j = i + 1
            while j < n and indices[j] == indices[i] and (j - i) < MAX_RUN:
                j += 1
            run_len = j - i
            f.write(struct.pack("<BB", run_len, indices[i]))
            i = j

    raw_size = w * h * 2  # what the existing raw .bin tile would cost
    rle_size = os.path.getsize(dst_path)
    return raw_size, rle_size


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_dir> <output_dir>")
        sys.exit(1)

    src_dir, dst_dir = sys.argv[1], sys.argv[2]

    # Pre-scan so we can show "N / total" rather than just a climbing counter.
    all_src_paths = []
    for root, _dirs, files in os.walk(src_dir):
        for name in files:
            if name.lower().endswith((".png", ".jpg", ".jpeg")):
                all_src_paths.append(os.path.join(root, name))

    total_files = len(all_src_paths)
    total_raw = total_rle = 0
    count = 0
    failed = []
    worst_ratio = 0.0
    worst_path = None
    start = time.time()

    for src_path in all_src_paths:
        rel = os.path.relpath(src_path, src_dir)
        dst_path = os.path.join(dst_dir, os.path.splitext(rel)[0] + ".rle")
        os.makedirs(os.path.dirname(dst_path), exist_ok=True)

        try:
            raw_size, rle_size = encode_tile(src_path, dst_path)
        except Exception as exc:  # corrupt/unreadable tile -- skip, don't abort the batch
            failed.append((rel, str(exc)))
            continue

        total_raw += raw_size
        total_rle += rle_size
        count += 1

        ratio = rle_size / raw_size if raw_size else 0
        if ratio > worst_ratio:
            worst_ratio = ratio
            worst_path = rel

        if count % 100 == 0 or count == total_files:
            elapsed = time.time() - start
            rate = count / elapsed if elapsed > 0 else 0
            print(f"  {count}/{total_files} tiles ({rate:.0f}/s)", file=sys.stderr)

    elapsed = time.time() - start
    if count:
        print(f"\nEncoded {count}/{total_files} tiles in {elapsed:.1f}s")
        print(f"Raw (.bin-equivalent): {total_raw / 1024:.1f} KB")
        print(
            f"RLE total:             {total_rle / 1024:.1f} KB "
            f"({100 * total_rle / total_raw:.1f}% of raw)"
        )
        if worst_path:
            print(
                f"Worst-compressing tile: {worst_path} "
                f"({100 * worst_ratio:.1f}% of its raw size -- "
                f"check it isn't a photographic/satellite tile)"
            )
    else:
        print("No tiles found under", src_dir)

    if failed:
        print(f"\n{len(failed)} tile(s) failed and were skipped:")
        for rel, err in failed[:20]:
            print(f"  {rel}: {err}")
        if len(failed) > 20:
            print(f"  ... and {len(failed) - 20} more")


if __name__ == "__main__":
    main()
