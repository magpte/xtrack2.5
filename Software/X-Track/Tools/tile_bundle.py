#!/usr/bin/env python3
"""
tile_bundle.py

Goes directly from source map tile images (PNG/JPG/IMAGE, or raw .bin tiles)
to packed "TBND" bundle files.

Supports:
  - Tencent Map tiles (--tencent): automatically flips Tencent's inverted Y axis
    (Y_osm = 2^z - 1 - Y_tencent).
  - GCJ-02 to WGS-84 conversion (--gcj02-to-wgs84): performs sub-pixel PIL
    re-projection and tile cropping on PC so output bundles are 100% standard WGS-84.
  - Flexible file matching: supports both flat names (e.g. 16.53354.28462.image)
    and nested directory layouts (<level>/<tileX>/<tileY>.<ext>).

Usage:
    python tile_bundle.py <input_dir> <output_dir> [--tencent] [--gcj02-to-wgs84] [--block-size N] [--workers N]
"""
import argparse
import math
import os
import re
import struct
import sys
import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor

try:
    from PIL import Image
except ImportError:
    Image = None

from tile_lz4_encode import encode_tile_bytes as encode_tile_bytes_lz4, load_source_image
from tile_zstd_encode import encode_tile_bytes as encode_tile_bytes_zstd

MAGIC = b"TBND"
ABSENT_OFFSET = 0xFFFFFFFF
DEFAULT_BLOCK_SIZE = 100

TILE_RE = re.compile(r"^(\d+)\.(png|jpg|jpeg|bin|image)$", re.IGNORECASE)
FLAT_TILE_RE = re.compile(r"^(\d+)[\._](\d+)[\._](\d+)\.(png|jpg|jpeg|bin|image)$", re.IGNORECASE)


# --- GCJ-02 to WGS-84 Coordinate Transformation Helpers ---

def out_of_china(lat, lon):
    return not (72.004 <= lon <= 137.8347 and 0.8293 <= lat <= 55.8271)


def _transform_lat(x, y):
    ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(y * math.pi) + 40.0 * math.sin(y / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (160.0 * math.sin(y / 12.0 * math.pi) + 320.0 * math.sin(y * math.pi / 30.0)) * 2.0 / 3.0
    return ret


def _transform_lon(x, y):
    ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(x * math.pi) + 40.0 * math.sin(x / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (150.0 * math.sin(x / 12.0 * math.pi) + 300.0 * math.sin(x / 30.0 * math.pi)) * 2.0 / 3.0
    return ret


def wgs84_to_gcj02(lat, lon):
    if out_of_china(lat, lon):
        return lat, lon
    d_lat = _transform_lat(lon - 105.0, lat - 35.0)
    d_lon = _transform_lon(lon - 105.0, lat - 35.0)
    rad_lat = lat / 180.0 * math.pi
    magic = math.sin(rad_lat)
    magic = 1.0 - 0.00669342162296594323 * magic * magic
    sqrt_magic = math.sqrt(magic)
    d_lat = (d_lat * 180.0) / ((6378245.0 * (1.0 - 0.00669342162296594323)) / (magic * sqrt_magic) * math.pi)
    d_lon = (d_lon * 180.0) / (6378245.0 / sqrt_magic * math.cos(rad_lat) * math.pi)
    return lat + d_lat, lon + d_lon


def gcj02_to_wgs84(lat, lon):
    if out_of_china(lat, lon):
        return lat, lon
    g_lat, g_lon = wgs84_to_gcj02(lat, lon)
    return lat * 2 - g_lat, lon * 2 - g_lon


def latlon_to_pixel_xy(lat, lon, zoom):
    map_size = (1 << zoom) * 256.0
    x = (lon + 180.0) / 360.0
    sin_lat = math.sin(math.radians(lat))
    sin_lat = max(-0.9999, min(0.9999, sin_lat))
    y = 0.5 - math.log((1.0 + sin_lat) / (1.0 - sin_lat)) / (4.0 * math.pi)
    return x * map_size, y * map_size


def pixel_xy_to_latlon(pixel_x, pixel_y, zoom):
    map_size = (1 << zoom) * 256.0
    x = (pixel_x / map_size) - 0.5
    y = 0.5 - (pixel_y / map_size)
    lat = 90.0 - 360.0 * math.atan(math.exp(-y * 2.0 * math.pi)) / math.pi
    lon = 360.0 * x
    return lat, lon


def tile_xy_to_latlon(tile_x, tile_y, zoom):
    return pixel_xy_to_latlon(tile_x * 256.0, tile_y * 256.0, zoom)


def latlon_to_tile_xy(lat, lon, zoom):
    px, py = latlon_to_pixel_xy(lat, lon, zoom)
    return int(px // 256), int(py // 256)


def find_tiles(input_dir, is_tencent=False):
    """Recursively scans input_dir for tile files (supports both nested and flat layouts).
    Yields (level, tileX, tileY_osm, full_path)."""
    for root, dirs, files in os.walk(input_dir):
        for fname in files:
            full_path = os.path.join(root, fname)

            # 1. Flat layout check: e.g. 16.53354.28462.image or 16_53354_28462.png
            m_flat = FLAT_TILE_RE.match(fname)
            if m_flat:
                level = int(m_flat.group(1))
                tile_x = int(m_flat.group(2))
                tile_y = int(m_flat.group(3))
                if is_tencent:
                    tile_y = (1 << level) - 1 - tile_y
                yield level, tile_x, tile_y, full_path
                continue

            # 2. Nested layout check: <level>/<tileX>/<tileY>.<ext> or <tileX>/<tileY>.<ext> when root is <level>
            m = TILE_RE.match(fname)
            if m:
                rel_path = os.path.relpath(full_path, input_dir)
                parts = os.path.normpath(rel_path).split(os.sep)
                if len(parts) >= 3 and parts[0].isdigit() and parts[1].isdigit():
                    level = int(parts[0])
                    tile_x = int(parts[1])
                    tile_y = int(m.group(1))
                    if is_tencent:
                        tile_y = (1 << level) - 1 - tile_y
                    yield level, tile_x, tile_y, full_path
                elif len(parts) == 2 and parts[0].isdigit() and os.path.basename(input_dir).isdigit():
                    level = int(os.path.basename(input_dir))
                    tile_x = int(parts[0])
                    tile_y = int(m.group(1))
                    if is_tencent:
                        tile_y = (1 << level) - 1 - tile_y
                    yield level, tile_x, tile_y, full_path


def _encode_block(args):
    """Worker process: packs one bundle block (.tbnd). Handles sub-pixel cropping if PIL is available."""
    level, block_x, block_y, block_size, entries, out_path, gcj_warp, tile_lookup, encoder = args

    encode_fn = encode_tile_bytes_zstd if encoder == "zstd" else encode_tile_bytes_lz4

    index = [(ABSENT_OFFSET, 0)] * (block_size * block_size)
    data_chunks = []
    data_offset = 0
    total_raw = 0
    failed = []

    for local_x, local_y, path_or_target in entries:
        tile_x_wgs = block_x * block_size + local_x
        tile_y_wgs = block_y * block_size + local_y

        try:
            if gcj_warp and Image is not None and tile_lookup:
                # Sub-pixel precise GCJ-02 -> WGS-84 PIL stitching & cropping
                lat_wgs, lon_wgs = tile_xy_to_latlon(tile_x_wgs, tile_y_wgs, level)
                lat_gcj, lon_gcj = wgs84_to_gcj02(lat_wgs, lon_wgs)
                px_gcj, py_gcj = latlon_to_pixel_xy(lat_gcj, lon_gcj, level)

                tile_x_base = int(px_gcj // 256)
                tile_y_base = int(py_gcj // 256)
                off_x = int(px_gcj % 256)
                off_y = int(py_gcj % 256)

                # Stitch 2x2 GCJ-02 source tiles
                canvas = Image.new("RGB", (512, 512), (255, 255, 255))
                for dx in range(2):
                    for dy in range(2):
                        src_path = tile_lookup.get((level, tile_x_base + dx, tile_y_base + dy))
                        if src_path:
                            try:
                                tile_img = load_source_image(src_path)
                                canvas.paste(tile_img, (dx * 256, dy * 256))
                            except Exception:
                                pass

                cropped = canvas.crop((off_x, off_y, off_x + 256, off_y + 256))
                raw_size, encoded = encode_fn(cropped)
            else:
                src_path = path_or_target if isinstance(path_or_target, str) else path_or_target[0]
                raw_size, encoded = encode_fn(src_path)
        except Exception as exc:
            failed.append((str(path_or_target), str(exc)))
            continue

        # Align (data_section_start + data_offset) to 512-byte physical sector boundary
        data_section_start = 14 + (block_size * block_size * 8)
        current_abs_pos = data_section_start + data_offset
        pad = (512 - (current_abs_pos % 512)) % 512
        if pad > 0:
            data_chunks.append(b"\x00" * pad)
            data_offset += pad

        idx = local_y * block_size + local_x
        index[idx] = (data_offset, len(encoded))
        data_chunks.append(encoded)
        data_offset += len(encoded)
        total_raw += raw_size

    if data_chunks:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(MAGIC)
            f.write(struct.pack("<H", block_size))
            f.write(struct.pack("<ii", block_x, block_y))
            for offset, length in index:
                f.write(struct.pack("<II", offset, length))
            for chunk in data_chunks:
                f.write(chunk)

    bundle_size = os.path.getsize(out_path) if os.path.exists(out_path) else 0
    encoded_count = len(entries) - len(failed)
    return (level, block_x, block_y, encoded_count, total_raw, bundle_size, failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input_dir", help="directory containing tile files/folders")
    parser.add_argument("output_dir", help="directory to write <level>/<blockX>_<blockY>.tbnd into")
    parser.add_argument(
        "--encoder", choices=["zstd", "lz4"], default="zstd",
        help="tile compression engine: 'zstd' (Adaptive ZST2, 100%% lossless, default) or 'lz4' (LZ42).",
    )
    parser.add_argument(
        "--tencent", action="store_true",
        help="enable Tencent Map Y-axis flip (Y_osm = 2^z - 1 - Y_tencent). Use when downloading tiles from Tencent Maps.",
    )
    parser.add_argument(
        "--gcj02-to-wgs84", action="store_true",
        help="convert input GCJ-02 tiles to WGS-84 standard coordinates using sub-pixel PIL cropping.",
    )
    parser.add_argument(
        "--block-size", type=int, default=DEFAULT_BLOCK_SIZE,
        help=f"tiles per side of a bundle grid (default {DEFAULT_BLOCK_SIZE}).",
    )
    parser.add_argument(
        "--workers", type=int, default=os.cpu_count(),
        help=f"parallel worker processes (default: {os.cpu_count()}).",
    )
    args = parser.parse_args()

    print(f"Scanning {args.input_dir} ... (Encoder: {args.encoder.upper()})", file=sys.stderr)
    if args.tencent:
        print("  [Option] Tencent Map Y-axis inversion enabled.", file=sys.stderr)
    if args.gcj02_to_wgs84:
        print("  [Option] GCJ-02 to WGS-84 sub-pixel coordinate conversion enabled.", file=sys.stderr)

    tile_lookup = {}
    by_level = defaultdict(list)
    file_count = 0

    for level, tile_x, tile_y, path in find_tiles(args.input_dir, is_tencent=args.tencent):
        tile_lookup[(level, tile_x, tile_y)] = path
        by_level[level].append((tile_x, tile_y, path))
        file_count += 1

    if not by_level:
        print("No tiles found under", args.input_dir)
        sys.exit(1)

    print(f"Found {file_count} source tiles across {len(by_level)} zoom level(s)")

    tasks = []
    for level, tiles in by_level.items():
        blocks = defaultdict(list)
        for tile_x, tile_y, path in tiles:
            if args.gcj02_to_wgs84 and Image is not None:
                # WGS-84 target grid
                lat_gcj, lon_gcj = tile_xy_to_latlon(tile_x + 0.5, tile_y + 0.5, level)
                lat_wgs, lon_wgs = gcj02_to_wgs84(lat_gcj, lon_gcj)
                target_x, target_y = latlon_to_tile_xy(lat_wgs, lon_wgs, level)
            else:
                target_x, target_y = tile_x, tile_y

            block_x, local_x = divmod(target_x, args.block_size)
            block_y, local_y = divmod(target_y, args.block_size)
            blocks[(block_x, block_y)].append((local_x, local_y, path))

        level_out_dir = os.path.join(args.output_dir, str(level))
        for (block_x, block_y), entries in blocks.items():
            out_path = os.path.join(level_out_dir, f"{block_x}_{block_y}.tbnd")
            tasks.append((level, block_x, block_y, args.block_size, entries, out_path, args.gcj02_to_wgs84, tile_lookup, args.encoder))

    total_blocks = len(tasks)
    print(f"Packing into {total_blocks} bundle file(s) using {args.workers} worker process(es)...", file=sys.stderr)

    grand_tiles = 0
    grand_raw = 0
    grand_bundle_bytes = 0
    all_failed = []
    done = 0
    start = time.time()

    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        for level, block_x, block_y, encoded_count, total_raw, bundle_size, failed in pool.map(_encode_block, tasks):
            done += 1
            grand_tiles += encoded_count
            grand_raw += total_raw
            grand_bundle_bytes += bundle_size
            all_failed.extend(failed)

            print(
                f"  [{done}/{total_blocks}] level {level}: {block_x}_{block_y}.tbnd "
                f"({encoded_count} tiles, {bundle_size / 1024:.1f} KB)",
                file=sys.stderr,
            )

    elapsed = time.time() - start
    print()
    print(f"Packed {grand_tiles} tiles into {total_blocks} bundle file(s) in {elapsed:.1f}s "
          f"({grand_tiles / elapsed:.0f} tiles/sec)")
    print(f"Raw (.bin-equivalent): {grand_raw / 1024 / 1024:.1f} MB")
    if grand_raw:
        print(f"Bundle total:          {grand_bundle_bytes / 1024 / 1024:.1f} MB "
              f"({100 * grand_bundle_bytes / grand_raw:.1f}% of raw)")

    if all_failed:
        print(f"\n{len(all_failed)} tile(s) failed and were skipped:")
        for path, err in all_failed[:20]:
            print(f"  {path}: {err}")


if __name__ == "__main__":
    main()
