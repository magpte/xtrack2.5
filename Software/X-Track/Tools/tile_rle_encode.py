#!/usr/bin/env python3
"""
tile_rle_encode.py

Converts map tile images into the compact palette + run-length tile format
used by the lv_img_rle LVGL widget. Accepts PNG/JPG sources, AND the
project's existing raw .bin tiles (LVGL's own 4-byte-header raw image
format) -- so you can re-encode tiles you've already converted once,
without needing to go back to the original downloaded images.

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
/14/3376/5432.png or /14/3376/5432.bin tile becomes /14/3376/5432.rle).
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

MAGIC = b"RLE2"
MAX_PALETTE = 256
MAX_RUN = 255
CHECKPOINT_INTERVAL = 16  # rows between seek checkpoints

# --- .bin source support -----------------------------------------------
# X-Track's raw tile format is LVGL's own file-image format: a 4-byte
# packed header (lv_img_header_t: cf:5, always_zero:3, reserved:2, w:11,
# h:11) followed by raw pixel data. LVGL's built-in decoder only accepts
# this for files literally named "*.bin" (lv_img_decoder.c checks the
# extension), which is why the existing tiles use that extension.
LV_IMG_CF_TRUE_COLOR = 4

# Must match LV_COLOR_16_SWAP in lv_conf.h. This project has it set to 1
# (common for SPI displays), meaning each pixel's two bytes are swapped
# relative to plain little-endian RGB565. If colors come out wrong
# (classic symptom: red/blue look swapped), flip this to False.
LV_COLOR_16_SWAP = True


def rgb888_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def load_bin_tile(path):
    """Read an LVGL raw .bin tile (4-byte header + RGB565 pixels) and
    return it as a PIL RGB Image, so it can feed the same quantize+RLE
    pipeline used for PNG/JPG sources."""
    with open(path, "rb") as f:
        header_bytes = f.read(4)
        if len(header_bytes) != 4:
            raise ValueError("file too short for an LVGL image header")

        header_val = struct.unpack("<I", header_bytes)[0]
        cf = header_val & 0x1F
        w = (header_val >> 10) & 0x7FF
        h = (header_val >> 21) & 0x7FF

        if cf != LV_IMG_CF_TRUE_COLOR:
            raise ValueError(
                f"unsupported color format cf={cf} "
                f"(expected {LV_IMG_CF_TRUE_COLOR} = LV_IMG_CF_TRUE_COLOR)"
            )
        if w == 0 or h == 0:
            raise ValueError(f"bad dimensions in header: {w}x{h}")

        pixel_bytes = f.read(w * h * 2)
        if len(pixel_bytes) != w * h * 2:
            raise ValueError(
                f"truncated pixel data: got {len(pixel_bytes)} bytes, "
                f"expected {w * h * 2} for {w}x{h}"
            )

    raw = np.frombuffer(pixel_bytes, dtype="<u2").astype(np.uint32)

    if LV_COLOR_16_SWAP:
        raw = ((raw & 0xFF) << 8) | (raw >> 8)

    r5 = (raw >> 11) & 0x1F
    g6 = (raw >> 5) & 0x3F
    b5 = raw & 0x1F

    # Expand 5/6-bit channels back to 8-bit by replicating the high bits
    # into the low bits -- more accurate than a plain left-shift.
    r8 = ((r5 << 3) | (r5 >> 2)).astype(np.uint8)
    g8 = ((g6 << 2) | (g6 >> 4)).astype(np.uint8)
    b8 = ((b5 << 3) | (b5 >> 2)).astype(np.uint8)

    rgb = np.stack([r8, g8, b8], axis=-1).reshape(h, w, 3)
    return Image.fromarray(rgb, mode="RGB")


def load_source_image(src_path):
    if src_path.lower().endswith(".bin"):
        return load_bin_tile(src_path)
    return Image.open(src_path).convert("RGB")


def encode_image_bytes(img):
    """Core encoder: takes a PIL RGB Image, returns the encoded RLE2 tile
    as a bytes object. No file I/O here -- this is what lets
    tile_bundle.py encode straight into a bundle without ever writing an
    intermediate .rle file to disk."""
    w, h = img.size

    # Quantize down to <=256 colors. Cartographic tiles usually have far
    # fewer than 256 distinct colors already, so this step is often
    # lossless or near-lossless for that style of tile.
    pal_img = img.convert("P", palette=Image.ADAPTIVE, colors=MAX_PALETTE)
    palette_rgb = pal_img.getpalette()[: MAX_PALETTE * 3]
    indices = np.asarray(pal_img, dtype=np.uint8).reshape(-1)  # row-major, flat

    # Remap to only the colors actually used, so paletteCount can be < 256
    # and the palette table in the file stays as small as possible.
    # np.unique's return_inverse does the same job as a
    # sorted(set(...)) + dict + list-comprehension remap, but as one
    # vectorized C-level call instead of a pure-Python loop over every
    # pixel.
    used, indices = np.unique(indices, return_inverse=True)
    indices = indices.astype(np.uint8)
    palette_count = len(used)

    n = len(indices)
    checkpoint_count = (h + CHECKPOINT_INTERVAL - 1) // CHECKPOINT_INTERVAL
    checkpoint_pixels = (np.arange(checkpoint_count, dtype=np.int64) * CHECKPOINT_INTERVAL) * w

    # Find every position where the run-length-encoded *content* would
    # naturally need a new run (the pixel value changed), in one
    # vectorized pass instead of scanning pixel-by-pixel in Python.
    change_points = np.where(np.diff(indices) != 0)[0] + 1
    # Mandatory split points = natural color changes, union checkpoint-row
    # starts (so a checkpoint's offset always lands exactly on a run
    # start -- the decoder relies on this to resume cleanly after a seek).
    split_points = np.unique(np.concatenate(([0], change_points, checkpoint_pixels[1:])))

    run_starts = split_points
    run_ends = np.concatenate((split_points[1:], [n]))
    run_lengths = run_ends - run_starts
    run_values = indices[run_starts]

    # At this point every remaining run is already capped to a single
    # checkpoint block and a single color -- the only thing left to
    # enforce is MAX_RUN (255). Most runs need no further subdivision, so
    # this Python loop runs over a few hundred/thousand *runs*, not over
    # every pixel.
    run_stream = bytearray()
    checkpoint_offsets = [0] * checkpoint_count
    checkpoint_iter = 0

    for start, length, val in zip(run_starts.tolist(), run_lengths.tolist(), run_values.tolist()):
        while checkpoint_iter < checkpoint_count and start >= checkpoint_pixels[checkpoint_iter]:
            checkpoint_offsets[checkpoint_iter] = len(run_stream)
            checkpoint_iter += 1

        remaining = length
        while remaining > 0:
            chunk = remaining if remaining < MAX_RUN else MAX_RUN
            run_stream.append(chunk)
            run_stream.append(val)
            remaining -= chunk

    while checkpoint_iter < checkpoint_count:
        checkpoint_offsets[checkpoint_iter] = len(run_stream)
        checkpoint_iter += 1

    out = bytearray()
    out += MAGIC
    out += struct.pack("<HHHHH", w, h, palette_count, CHECKPOINT_INTERVAL, checkpoint_count)

    for old_idx in used.tolist():
        r = palette_rgb[old_idx * 3]
        g = palette_rgb[old_idx * 3 + 1]
        b = palette_rgb[old_idx * 3 + 2]
        out += struct.pack("<H", rgb888_to_rgb565(r, g, b))

    for offset in checkpoint_offsets:
        out += struct.pack("<I", offset)

    out += run_stream
    return bytes(out)


def encode_tile_bytes(src):
    """Load+encode a tile from any supported source format (.png/.jpg/
    .bin, or a PIL Image object), returning (raw_size, encoded_bytes)."""
    if isinstance(src, str):
        img = load_source_image(src)
    else:
        img = src
    w, h = img.size
    encoded = encode_image_bytes(img)
    raw_size = w * h * 2  # what the existing raw .bin tile would cost
    return raw_size, encoded


def encode_tile(src_path, dst_path):
    """File-to-file convenience wrapper around encode_tile_bytes(), for
    standalone use (python tile_rle_encode.py <in> <out>)."""
    raw_size, encoded = encode_tile_bytes(src_path)
    with open(dst_path, "wb") as f:
        f.write(encoded)
    return raw_size, len(encoded)



def _encode_one(args):
    """Module-level worker for ProcessPoolExecutor (must be a plain
    top-level function so it can be pickled/sent to worker processes,
    including on Windows where multiprocessing uses 'spawn')."""
    src_path, dst_path = args
    try:
        raw_size, rle_size = encode_tile(src_path, dst_path)
        return (src_path, raw_size, rle_size, None)
    except Exception as exc:  # corrupt/unreadable tile -- report, don't crash the worker
        return (src_path, 0, 0, str(exc))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input_dir")
    parser.add_argument("output_dir")
    parser.add_argument(
        "--workers", type=int, default=os.cpu_count(),
        help=f"parallel worker processes (default: all {os.cpu_count()} CPUs detected)",
    )
    args = parser.parse_args()

    src_dir, dst_dir = args.input_dir, args.output_dir

    # Pre-scan so we can show "N / total" rather than just a climbing counter.
    all_src_paths = []
    for root, _dirs, files in os.walk(src_dir):
        for name in files:
            if name.lower().endswith((".png", ".jpg", ".jpeg", ".bin")):
                all_src_paths.append(os.path.join(root, name))

    total_files = len(all_src_paths)
    if total_files == 0:
        print("No tiles found under", src_dir)
        return

    tasks = []
    for src_path in all_src_paths:
        rel = os.path.relpath(src_path, src_dir)
        dst_path = os.path.join(dst_dir, os.path.splitext(rel)[0] + ".rle")
        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        tasks.append((src_path, dst_path))

    total_raw = total_rle = 0
    count = 0
    failed = []
    worst_ratio = 0.0
    worst_path = None
    start = time.time()

    print(f"Encoding {total_files} tiles with {args.workers} worker process(es)...", file=sys.stderr)

    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        for src_path, raw_size, rle_size, err in pool.map(_encode_one, tasks, chunksize=16):
            rel = os.path.relpath(src_path, src_dir)
            if err:
                failed.append((rel, err))
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
        print(f"\nEncoded {count}/{total_files} tiles in {elapsed:.1f}s ({count/elapsed:.0f} tiles/sec)")
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
