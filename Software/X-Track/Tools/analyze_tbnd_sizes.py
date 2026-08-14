#!/usr/bin/env python3
"""
analyze_tbnd_sizes.py

Analyze the tile size distribution in one or more .tbnd files or directories.
Helps evaluate RLE compression efficiency across different zoom levels (e.g. Level 16 vs Level 18).

Usage:
    python analyze_tbnd_sizes.py <path_to_file_or_dir>
"""

import os
import sys
import struct

def analyze_file(filepath):
    try:
        with open(filepath, "rb") as f:
            header = f.read(14)
            if len(header) < 14:
                return []
            magic, block_size, block_x, block_y = struct.unpack("<4sHII", header)
            if magic != b"TBND":
                return []
            
            index_bytes = f.read(block_size * block_size * 8)
            sizes = []
            for i in range(block_size * block_size):
                offset, length = struct.unpack("<II", index_bytes[i*8:(i+1)*8])
                if offset != 0xFFFFFFFF and length > 0:
                    sizes.append(length)
            return sizes
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return []

def print_stats(name, sizes):
    if not sizes:
        print(f"[{name}] No active tiles found.")
        return
    
    sizes.sort()
    count = len(sizes)
    min_s = sizes[0]
    max_s = sizes[-1]
    avg_s = sum(sizes) / count
    med_s = sizes[count // 2]
    p95_s = sizes[int(count * 0.95)]
    
    # Histogram buckets (in KB)
    buckets = {
        "< 5 KB": 0,
        "5 - 10 KB": 0,
        "10 - 20 KB": 0,
        "20 - 30 KB": 0,
        "30 - 40 KB": 0,
        "40 - 50 KB": 0,
        "> 50 KB": 0,
    }
    
    for s in sizes:
        kb = s / 1024.0
        if kb < 5:
            buckets["< 5 KB"] += 1
        elif kb < 10:
            buckets["5 - 10 KB"] += 1
        elif kb < 20:
            buckets["10 - 20 KB"] += 1
        elif kb < 30:
            buckets["20 - 30 KB"] += 1
        elif kb < 40:
            buckets["30 - 40 KB"] += 1
        elif kb < 50:
            buckets["40 - 50 KB"] += 1
        else:
            buckets["> 50 KB"] += 1
            
    print("=" * 65)
    print(f" Tile Size Distribution Analysis: {name}")
    print("=" * 65)
    print(f" Total Tiles Count : {count}")
    print(f" Min Tile Size     : {min_s} B ({min_s/1024:.2f} KB)")
    print(f" Average Size      : {avg_s:.1f} B ({avg_s/1024:.2f} KB)")
    print(f" Median Size       : {med_s} B ({med_s/1024:.2f} KB)")
    print(f" 95th Percentile   : {p95_s} B ({p95_s/1024:.2f} KB)")
    print(f" Max Tile Size     : {max_s} B ({max_s/1024:.2f} KB)")
    print("-" * 65)
    print(" Size Distribution Breakdown:")
    for b_name, b_count in buckets.items():
        pct = (b_count / count) * 100.0
        bar = "#" * int(pct / 2.5)
        print(f"  {b_name:<10} : {b_count:>6} tiles ({pct:>5.1f}%) | {bar}")
    print("=" * 65)

def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_tbnd_sizes.py <file_or_directory>")
        sys.exit(1)
        
    target = sys.argv[1]
    if os.path.isfile(target):
        sizes = analyze_file(target)
        print_stats(os.path.basename(target), sizes)
    elif os.path.isdir(target):
        all_sizes = []
        for root, _, files in os.walk(target):
            for file in files:
                if file.endswith(".tbnd"):
                    path = os.path.join(root, file)
                    sizes = analyze_file(path)
                    all_sizes.extend(sizes)
        print_stats(f"Directory: {target}", all_sizes)
    else:
        print(f"Target not found: {target}")

if __name__ == "__main__":
    main()
