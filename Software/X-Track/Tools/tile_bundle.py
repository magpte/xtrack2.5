#!/usr/bin/env python3
"""
tile_bundle.py

Goes directly from source map tile images (PNG/JPG, or the project's
existing raw .bin tiles) to packed "TBND" bundle files -- combining what
used to be two separate steps (tile_rle_encode.py, then a separate
bundling pass) into one. No intermediate .rle files are ever written to
disk; each tile is encoded straight into memory and packed into its
bundle.

Why bundle at all: a deep zoom level can have hundreds of thousands of
individual tiny tile files. FAT32/SD cards (directory lookups, cluster
allocation overhead) and host-side copying both degrade badly with huge
numbers of tiny files. Bundling groups many tiles into far fewer,
larger files.

Expects the source layout:
    <input_dir>/<level>/<tileX>/<tileY>.<png|jpg|jpeg|bin>

Produces:
    <output_dir>/<level>/<blockX>_<blockY>.tbnd

where blockX = tileX // BLOCK_SIZE, blockY = tileY // BLOCK_SIZE (default
BLOCK_SIZE=100, i.e. up to 100*100=10000 tiles per bundle file -- this
must match TILE_BUNDLE_BLOCK_SIZE in the firmware's TileBundleFS.h).

Bundle file format (little-endian) -- see TileBundleFS.cpp for the
firmware-side reader:
    4 bytes   magic "TBND"
    2 bytes   blockSize (N -- the grid is N x N tiles)
    4 bytes   blockX
    4 bytes   blockY
    index table: blockSize*blockSize entries, 8 bytes each
        4 bytes  offset (into the data section; 0xFFFFFFFF = tile absent)
        4 bytes  length (0 if absent)
        entry for local position (lx, ly) is at index ly*blockSize + lx
    data section: concatenated RLE2-encoded tile bytes

Usage:
    python tile_bundle.py <input_dir> <output_dir> [--block-size N] [--workers N]
"""
import argparse
import os
import re
import struct
import sys
import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor

# Reuses the exact same RLE2 encoder used by tile_rle_encode.py, so a
# tile encoded via this script and one encoded via tile_rle_encode.py
# are byte-for-byte identical -- this is not a separate/parallel
# implementation of the format.
from tile_rle_encode import encode_tile_bytes

MAGIC = b"TBND"
ABSENT_OFFSET = 0xFFFFFFFF
DEFAULT_BLOCK_SIZE = 100

TILE_RE = re.compile(r"^(\d+)\.(png|jpg|jpeg|bin)$", re.IGNORECASE)


def find_tiles(input_dir):
    """Yields (level, tileX, tileY, full_path) for every source tile
    under <input_dir>/<level>/<tileX>/<tileY>.<ext>"""
    for level_name in os.listdir(input_dir):
        level_dir = os.path.join(input_dir, level_name)
        if not os.path.isdir(level_dir) or not level_name.isdigit():
            continue
        level = int(level_name)

        for tilex_name in os.listdir(level_dir):
            tilex_dir = os.path.join(level_dir, tilex_name)
            if not os.path.isdir(tilex_dir) or not tilex_name.isdigit():
                continue
            tile_x = int(tilex_name)

            for fname in os.listdir(tilex_dir):
                m = TILE_RE.match(fname)
                if not m:
                    continue
                tile_y = int(m.group(1))
                yield level, tile_x, tile_y, os.path.join(tilex_dir, fname)


def _encode_block(args):
    """Worker: encodes every tile belonging to one bundle block and
    writes the finished .tbnd file. Runs in a separate process -- each
    block is fully independent, so this parallelizes across all
    available cores with no shared state between workers."""
    level, block_x, block_y, block_size, entries, out_path = args

    index = [(ABSENT_OFFSET, 0)] * (block_size * block_size)
    data_chunks = []
    data_offset = 0
    total_raw = 0
    failed = []

    for local_x, local_y, path in entries:
        try:
            raw_size, encoded = encode_tile_bytes(path)
        except Exception as exc:  # corrupt/unreadable source tile -- skip, don't abort the block
            failed.append((path, str(exc)))
            continue

        idx = local_y * block_size + local_x
        index[idx] = (data_offset, len(encoded))
        data_chunks.append(encoded)
        data_offset += len(encoded)
        total_raw += raw_size

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<H", block_size))
        f.write(struct.pack("<ii", block_x, block_y))
        for offset, length in index:
            f.write(struct.pack("<II", offset, length))
        for chunk in data_chunks:
            f.write(chunk)

    bundle_size = os.path.getsize(out_path)
    encoded_count = len(entries) - len(failed)
    return (level, block_x, block_y, encoded_count, total_raw, bundle_size, failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input_dir", help="directory containing <level>/<tileX>/<tileY>.<png|jpg|bin>")
    parser.add_argument("output_dir", help="directory to write <level>/<blockX>_<blockY>.tbnd into")
    parser.add_argument(
        "--block-size", type=int, default=DEFAULT_BLOCK_SIZE,
        help=f"tiles per side of a bundle's grid (default {DEFAULT_BLOCK_SIZE} -> "
             f"up to {DEFAULT_BLOCK_SIZE * DEFAULT_BLOCK_SIZE} tiles/bundle). "
             f"Must match TILE_BUNDLE_BLOCK_SIZE in the firmware's TileBundleFS.h.",
    )
    parser.add_argument(
        "--workers", type=int, default=os.cpu_count(),
        help=f"parallel worker processes (default: all {os.cpu_count()} CPUs detected). "
             "Parallelized per-block, so use a bigger --block-size if you have far more "
             "CPUs than blocks at a given zoom level.",
    )
    args = parser.parse_args()

    print(f"Scanning {args.input_dir} ...", file=sys.stderr)
    by_level = defaultdict(list)
    file_count = 0
    for level, tile_x, tile_y, path in find_tiles(args.input_dir):
        by_level[level].append((tile_x, tile_y, path))
        file_count += 1

    if not by_level:
        print("No tiles found under", args.input_dir)
        sys.exit(1)

    print(f"Found {file_count} source tiles across {len(by_level)} zoom level(s)")

    # Group into blocks across all levels first, so the whole job can be
    # handed to the process pool as one flat list of independent tasks
    # (better load balancing than processing one level at a time when
    # zoom levels vary hugely in tile count).
    tasks = []
    for level, tiles in by_level.items():
        blocks = defaultdict(list)
        for tile_x, tile_y, path in tiles:
            block_x, local_x = divmod(tile_x, args.block_size)
            block_y, local_y = divmod(tile_y, args.block_size)
            blocks[(block_x, block_y)].append((local_x, local_y, path))

        level_out_dir = os.path.join(args.output_dir, str(level))
        for (block_x, block_y), entries in blocks.items():
            out_path = os.path.join(level_out_dir, f"{block_x}_{block_y}.tbnd")
            tasks.append((level, block_x, block_y, args.block_size, entries, out_path))

    total_blocks = len(tasks)
    print(f"Packing into {total_blocks} bundle file(s) using {args.workers} worker process(es)...",
          file=sys.stderr)

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
    print(f"({file_count} small source files -> {total_blocks} bundle files)")

    if all_failed:
        print(f"\n{len(all_failed)} tile(s) failed and were skipped:")
        for path, err in all_failed[:20]:
            print(f"  {path}: {err}")
        if len(all_failed) > 20:
            print(f"  ... and {len(all_failed) - 20} more")


if __name__ == "__main__":
    main()
