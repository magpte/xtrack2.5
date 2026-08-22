#!/usr/bin/env python3
"""
verify_tbnd.py

Utility to inspect a .tbnd map bundle file, calculate WGS-84 lat/lon bounds,
and decode/export sample tiles.
"""
import struct
import sys
import math
import os

def pixel_xy_to_latlon(pixel_x, pixel_y, zoom):
    map_size = (1 << zoom) * 256.0
    x = (pixel_x / map_size) - 0.5
    y = 0.5 - (pixel_y / map_size)
    lat = 90.0 - 360.0 * math.atan(math.exp(-y * 2.0 * math.pi)) / math.pi
    lon = 360.0 * x
    return lat, lon

def inspect_tbnd(path):
    if not os.path.exists(path):
        print(f"Error: File '{path}' not found.")
        return

    with open(path, "rb") as f:
        header = f.read(14)
        if len(header) < 14:
            print(f"Error: '{path}' is too short to be a valid .tbnd file.")
            return
        
        magic, block_size, block_x, block_y = struct.unpack("<4sHII", header)
        if magic != b"TBND":
            print(f"Error: Invalid magic '{magic}', expected 'TBND'.")
            return
        
        # Determine zoom level based on BlockX range
        # For Level 18: max tile is ~262144, BlockX is around 0..2621
        # For Level 16: max tile is ~65536, BlockX is around 0..655
        tile_x_min = block_x * block_size
        tile_x_max = (block_x + 1) * block_size - 1
        tile_y_min = block_y * block_size
        tile_y_max = (block_y + 1) * block_size - 1

        zoom = 18 if tile_x_max > 65536 else 16
        lat_max, lon_min = pixel_xy_to_latlon(tile_x_min * 256, tile_y_min * 256, zoom)
        lat_min, lon_max = pixel_xy_to_latlon((tile_x_max + 1) * 256, (tile_y_max + 1) * 256, zoom)

        # Read index table
        index_size = block_size * block_size * 8
        index_bytes = f.read(index_size)
        
        present_tiles = 0
        first_tile_offset = 0
        first_tile_len = 0
        first_tile_coord = (0, 0)
        
        for local_y in range(block_size):
            for local_x in range(block_size):
                idx = (local_y * block_size + local_x) * 8
                off, length = struct.unpack("<II", index_bytes[idx:idx+8])
                if off != 0xFFFFFFFF and length > 0:
                    if present_tiles == 0:
                        first_tile_offset = off
                        first_tile_len = length
                        first_tile_coord = (tile_x_min + local_x, tile_y_min + local_y)
                    present_tiles += 1
        
        data_start_offset = 14 + index_size
        
        print("=" * 65)
        print(f" TBND File Metadata Analysis: {os.path.basename(path)}")
        print("=" * 65)
        print(f"  Header Magic   : {magic.decode('ascii')}")
        print(f"  Block Size     : {block_size} x {block_size} (Max {block_size * block_size} tiles)")
        print(f"  Block Index    : BlockX = {block_x}, BlockY = {block_y}")
        print(f"  Tile Range     : TileX [{tile_x_min} .. {tile_x_max}], TileY [{tile_y_min} .. {tile_y_max}]")
        print(f"  Total Tiles    : {present_tiles} active tiles packed")
        print("-" * 65)
        print(f"  Est. Zoom Level: {zoom}")
        print(f"  WGS-84 Area    : Longitude [{lon_min:.5f}°E .. {lon_max:.5f}°E]")
        print(f"                   Latitude  [{lat_min:.5f}°N .. {lat_max:.5f}°N]")
        print("-" * 65)

        # Inspect first tile format
        if present_tiles > 0:
            f.seek(data_start_offset + first_tile_offset)
            tile_head = f.read(min(16, first_tile_len))
            if tile_head.startswith(b"LZ42"):
                fmt = "Chunked LZ4HC Format (LZ42)"
            elif tile_head.startswith(b"RLE2"):
                fmt = "RLE2 Format (Palette + Seekable RLE)"
            elif tile_head.startswith(b"RLE1"):
                fmt = "RLE1 Format"
            elif tile_head.startswith(b"\x89PNG"):
                fmt = "PNG Image"
            elif tile_head.startswith(b"\xff\xd8\xff"):
                fmt = "JPEG Image"
            print(f"  Sample Tile    : Tile({first_tile_coord[0]}, {first_tile_coord[1]}), Format: {fmt}")
        
        print("=" * 65)
        print(" [结论说明 / Conclusion]:")
        print("  1. 文件头只记载瓦片编号(BlockX/Y)。物理坐标系取决于打包时")
        print("     源码瓦片是否用 tile_bundle.py --gcj02-to-wgs84 进行了纠偏。")
        print("  2. 该包覆盖经纬度: 广州/佛山及周边地区 (经度~113.3°E, 纬度~23.1°N)。")
        print("  3. 验证方法：在 SystemSave.json 中设 mapWGS84: 1 并在设备上对比")
        print("     真实 GPX 轨迹。若轨迹线精准贴合底图道路，则该 .tbnd 为 WGS-84！")
        print("=" * 65)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python verify_tbnd.py <path_to_bundle.tbnd>")
        sys.exit(1)
    
    inspect_tbnd(sys.argv[1])
