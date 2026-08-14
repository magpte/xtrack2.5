#!/usr/bin/env python3
"""
realign_tbnd.py

Fast, lossless utility to realign existing .tbnd files to 512-byte sector boundaries.
Does not re-encode images; performs direct bitstream repackaging with 512-byte alignment.

Usage:
    python realign_tbnd.py <input_file_or_dir> [output_file_or_dir] [--in-place] [--workers N]
"""

import os
import sys
import struct
import time
import argparse
from concurrent.futures import ProcessPoolExecutor

MAGIC = b"TBND"
ABSENT_OFFSET = 0xFFFFFFFF
HEADER_SIZE = 14
SECTOR_SIZE = 512

def realign_single_tbnd(in_path, out_path):
    """Realign a single .tbnd file so every tile starts at a 512-byte sector boundary."""
    try:
        with open(in_path, "rb") as f_in:
            header = f_in.read(HEADER_SIZE)
            if len(header) < HEADER_SIZE:
                return (in_path, False, "File too short")
            
            magic, block_size, block_x, block_y = struct.unpack("<4sHii", header)
            if magic != MAGIC:
                return (in_path, False, f"Invalid magic {magic}")
            
            index_size = block_size * block_size * 8
            index_bytes = f_in.read(index_size)
            if len(index_bytes) < index_size:
                return (in_path, False, "Truncated index table")
            
            data_section_start = HEADER_SIZE + index_size
            
            # Parse all existing entries
            entries = []
            for i in range(block_size * block_size):
                offset, length = struct.unpack("<II", index_bytes[i*8:(i+1)*8])
                if offset != ABSENT_OFFSET and length > 0:
                    entries.append((i, offset, length))
            
            if not entries:
                return (in_path, False, "No active tiles in bundle")
            
            # Sort entries by original offset to read sequentially from source
            entries.sort(key=lambda x: x[1])
            
            # Build new aligned data and new index
            new_index = [(ABSENT_OFFSET, 0)] * (block_size * block_size)
            new_data_offset = 0
            
            temp_out = out_path + ".tmp"
            os.makedirs(os.path.dirname(os.path.abspath(temp_out)), exist_ok=True)
            
            with open(temp_out, "wb") as f_out:
                # Placeholder for Header and Index table
                f_out.write(header)
                f_out.write(b"\x00" * index_size)
                
                for idx, old_offset, length in entries:
                    # Calculate padding needed to align (data_section_start + new_data_offset) to 512 bytes
                    current_abs_pos = data_section_start + new_data_offset
                    pad = (SECTOR_SIZE - (current_abs_pos % SECTOR_SIZE)) % SECTOR_SIZE
                    if pad > 0:
                        f_out.write(b"\x00" * pad)
                        new_data_offset += pad
                    
                    # Record aligned offset in new index
                    new_index[idx] = (new_data_offset, length)
                    
                    # Read tile from source and write to dest
                    f_in.seek(data_section_start + old_offset)
                    tile_data = f_in.read(length)
                    if len(tile_data) != length:
                        raise ValueError(f"Tile {idx} read truncated (expected {length}, got {len(tile_data)})")
                    
                    f_out.write(tile_data)
                    new_data_offset += length
                
                # Rewind and write updated Index table
                f_out.seek(HEADER_SIZE)
                for offset, length in new_index:
                    f_out.write(struct.pack("<II", offset, length))
            
            # Replace final file
            if os.path.exists(out_path):
                os.remove(out_path)
            os.rename(temp_out, out_path)
            
            orig_size = os.path.getsize(in_path)
            new_size = os.path.getsize(out_path)
            return (in_path, True, (len(entries), orig_size, new_size))
            
    except Exception as e:
        if os.path.exists(out_path + ".tmp"):
            try:
                os.remove(out_path + ".tmp")
            except Exception:
                pass
        return (in_path, False, str(e))


def process_task(task):
    in_path, out_path = task
    return realign_single_tbnd(in_path, out_path)


def main():
    parser = argparse.ArgumentParser(
        description="Fast lossless 512-byte sector alignment tool for .tbnd bundles."
    )
    parser.add_argument("input", help="Source .tbnd file or directory containing .tbnd files")
    parser.add_argument("output", nargs="?", default=None, help="Destination file or directory (default: in-place or <input>_aligned)")
    parser.add_argument("--in-place", action="store_true", help="Overwrite input files directly")
    parser.add_argument("--workers", type=int, default=os.cpu_count(), help="Number of parallel workers")
    
    args = parser.parse_args()
    
    in_target = os.path.abspath(args.input)
    if not os.path.exists(in_target):
        print(f"Error: Target '{args.input}' does not exist.")
        sys.exit(1)
        
    tasks = []
    
    if os.path.isfile(in_target):
        if not in_target.endswith(".tbnd"):
            print(f"Error: '{args.input}' is not a .tbnd file.")
            sys.exit(1)
        if args.in_place or args.output is None:
            out_target = in_target
        else:
            out_target = os.path.abspath(args.output)
            if os.path.isdir(out_target):
                out_target = os.path.join(out_target, os.path.basename(in_target))
        tasks.append((in_target, out_target))
    else:
        # Directory mode
        out_base = os.path.abspath(args.output) if args.output else (in_target if args.in_place else in_target + "_aligned")
        for root, _, files in os.walk(in_target):
            for file in files:
                if file.endswith(".tbnd"):
                    src_file = os.path.join(root, file)
                    rel = os.path.relpath(src_file, in_target)
                    dst_file = os.path.join(out_base, rel)
                    tasks.append((src_file, dst_file))
                    
    if not tasks:
        print("No .tbnd files found.")
        sys.exit(0)
        
    print("=" * 68)
    print(f" TBND 512-Byte Sector Realignment Tool")
    print("=" * 68)
    print(f" Total TBND Files to process : {len(tasks)}")
    print(f" Parallel Worker Processes   : {args.workers}")
    print("-" * 68)
    
    start_time = time.time()
    total_tiles = 0
    total_orig_bytes = 0
    total_new_bytes = 0
    success_count = 0
    fail_count = 0
    
    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        for i, (path, success, info) in enumerate(pool.map(process_task, tasks), 1):
            fname = os.path.basename(path)
            if success:
                success_count += 1
                tiles, orig_b, new_b = info
                total_tiles += tiles
                total_orig_bytes += orig_b
                total_new_bytes += new_b
                overhead = (new_b - orig_b) / orig_b * 100.0 if orig_b > 0 else 0
                print(f"  [{i}/{len(tasks)}] OK: {fname} ({tiles} tiles, {orig_b/1024:.1f}KB -> {new_b/1024:.1f}KB, +{overhead:.2f}%)")
            else:
                fail_count += 1
                print(f"  [{i}/{len(tasks)}] FAIL: {fname} - {info}", file=sys.stderr)
                
    elapsed = time.time() - start_time
    print("=" * 68)
    print(" Summary:")
    print(f"  Processed Files : {success_count} succeeded, {fail_count} failed")
    print(f"  Total Tiles     : {total_tiles}")
    print(f"  Original Size   : {total_orig_bytes / 1024 / 1024:.2f} MB")
    print(f"  Aligned Size    : {total_new_bytes / 1024 / 1024:.2f} MB (Overhead: {(total_new_bytes - total_orig_bytes) / 1024 / 1024:.2f} MB, {((total_new_bytes - total_orig_bytes) / total_orig_bytes * 100.0 if total_orig_bytes else 0):.2f}%)")
    print(f"  Elapsed Time    : {elapsed:.2f} s ({total_new_bytes / 1024 / 1024 / elapsed:.1f} MB/s)")
    print("=" * 68)

if __name__ == "__main__":
    main()
