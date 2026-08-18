#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
===============================================================================
X-Track 2.5 - 卫星历书 (Almanac) 在线抓取与生成工具
===============================================================================
功能：
  1. 自动从美国海岸警卫队 (USCG NAVCEN) 或全球 GNSS 镜像站下载最新的
     全天空 GPS / 北斗卫星历书 (YUMA 格式)；
  2. 自动解析卫星轨道六根数 (Keplerian elements)；
  3. 打包成 CASIC 协议二进制数据帧 (Class 0x0B, ID 0x30 / AID-ALM)；
  4. 添加 X-Track 专属 32 字节文件头 (魔数 'GALM', CRC32, 时间戳)；
  5. 输出为 SD 卡专用的 '/gpsalm.bin' 文件。

使用方法：
  python generate_gpsalm.py [输出路径，默认当前目录下生成 gpsalm.bin]
  例如:
    python generate_gpsalm.py
    python generate_gpsalm.py G:\\gpsalm.bin  (直接写入 SD 卡盘符)
===============================================================================
"""

import sys
import os
import time
import struct
import zlib
import urllib.request
import math

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

# USCG NAVCEN 官方实时 GPS 历书 URL
YUMA_URL_PRIMARY = "https://www.navcen.uscg.gov/sites/default/files/gps/almanac/current_yuma.alm"
# 备用镜像源 (Celestrak)
YUMA_URL_BACKUP = "https://celestrak.org/GPS/almanac/Yuma/current.alm"

# CASIC / X-Track 规范常量
HEADER_MAGIC = 0x4D4C4147  # 'GALM' in Little-Endian
HEADER_VERSION = 1


def calc_casic_checksum(msg_class, msg_id, payload_bytes):
    """
    计算 CASIC 协议 4 字节累加校验和 (Little-Endian uint32)
    ckSum = (ID << 24) | (Class << 16) | Len + sum(payload words)
    """
    length = len(payload_bytes)
    cksum = ((msg_id << 24) | (msg_class << 16) | length) & 0xFFFFFFFF
    for i in range(0, length, 4):
        chunk = payload_bytes[i:i + 4]
        if len(chunk) < 4:
            chunk = chunk.ljust(4, b'\x00')
        word = struct.unpack('<I', chunk)[0]
        cksum = (cksum + word) & 0xFFFFFFFF
    return cksum


def build_casic_packet(msg_class, msg_id, payload_bytes):
    """
    构造完整的 CASIC 二进制报文:
    [0xBA, 0xCE] [Len_L, Len_H] [Class] [ID] [Payload...] [CkSum (4 Bytes)]
    """
    header = b'\xBA\xCE'
    length = len(payload_bytes)
    len_bytes = struct.pack('<H', length)
    class_id_bytes = struct.pack('BB', msg_class, msg_id)
    cksum = calc_casic_checksum(msg_class, msg_id, payload_bytes)
    cksum_bytes = struct.pack('<I', cksum)
    return header + len_bytes + class_id_bytes + payload_bytes + cksum_bytes


def download_yuma_almanac():
    """从网络下载最新的 YUMA 历书文本"""
    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
    }
    urls = [YUMA_URL_PRIMARY, YUMA_URL_BACKUP]

    for url in urls:
        print(f"[*] 正在尝试连接下载历书: {url} ...")
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=10) as resp:
                if resp.status == 200:
                    content = resp.read().decode('utf-8', errors='ignore')
                    if "ID:" in content or "GPS ALMANAC" in content:
                        print(f"[+] 成功获取历书数据 ({len(content)} 字节)")
                        return content
        except Exception as e:
            print(f"[-] 镜像站连接失败 ({e})，尝试下一个源...")

    return None


def parse_yuma_text(yuma_text):
    """解析 YUMA 文本，返回每颗卫星的轨道参数字典列表"""
    satellites = []
    blocks = yuma_text.split("********")

    for block in blocks:
        lines = [line.strip() for line in block.splitlines() if line.strip()]
        if not lines:
            continue

        sat_dict = {}
        for line in lines:
            if ":" in line:
                key, val = line.split(":", 1)
                sat_dict[key.strip()] = val.strip()

        if "ID" in sat_dict:
            try:
                sat = {
                    'id': int(sat_dict.get('ID', '0')),
                    'health': int(sat_dict.get('Health', '0')),
                    'eccentricity': float(sat_dict.get('Eccentricity', '0')),
                    'toa': float(sat_dict.get('Time of Applicability(s)', '0')),
                    'inclination': float(sat_dict.get('Orbital Inclination(rad)', '0')),
                    'rate_ra': float(sat_dict.get('Rate of Right Ascen(r/s)', '0')),
                    'sqrt_a': float(sat_dict.get('SQRT(A)  (m 1/2)', '0')),
                    'ra_at_week': float(sat_dict.get('Right Ascen at Week(rad)', '0')),
                    'arg_perigee': float(sat_dict.get('Argument of Perigee(rad)', '0')),
                    'mean_anomaly': float(sat_dict.get('Mean Anom(rad)', '0')),
                    'af0': float(sat_dict.get('Af0(s)', '0')),
                    'af1': float(sat_dict.get('Af1(s/s)', '0')),
                    'week': int(sat_dict.get('week', '0'))
                }
                if sat['id'] > 0:
                    satellites.append(sat)
            except ValueError:
                continue

    return satellites


def encode_casic_aid_alm(sat):
    """
    将单个卫星的 YUMA 轨道参数编码为 CASIC AID-ALM (Class 0x0B, ID 0x30 / 0x04) Payload
    标准 CASIC 历书载荷格式 (44 字节):
      - uint32_t svid;          // 卫星 PRN 编号 (1~32)
      - uint32_t week;          // GPS 历书周号
      - uint32_t toa;           // 历书参考时间 Toa (秒)
      - float    ecc;           // 偏心率 e
      - float    delta_i;       // 轨道倾角与标称值偏差 (rad, 标称 ~0.9424 rad / 54°)
      - float    omega_dot;     // 升交点赤经变化率 (rad/s)
      - float    sqrt_a;        // 轨道半长轴平方根 (m^0.5)
      - float    omega_0;       // 周初升交点赤经 (rad)
      - float    omega;         // 近地点幅角 (rad)
      - float    m_0;           // 平近点角 (rad)
      - float    af0;           // 卫星钟偏差 (s)
      - float    af1;           // 卫星钟漂移 (s/s)
      - uint32_t health;        // 卫星健康状态 (0 = 健康)
    """
    delta_i = sat['inclination'] - 0.9424777960769379  # 相对标称倾角偏差 (0.3 rad)

    payload = struct.pack(
        '<IIIfffffffffI',
        sat['id'],
        sat['week'],
        int(sat['toa']),
        sat['eccentricity'],
        delta_i,
        sat['rate_ra'],
        sat['sqrt_a'],
        sat['ra_at_week'],
        sat['arg_perigee'],
        sat['mean_anomaly'],
        sat['af0'],
        sat['af1'],
        sat['health']
    )
    return payload


def generate_gpsalm_file(output_path="gpsalm.bin"):
    """主流程：下载、解析、编码、打包生成 gpsalm.bin"""
    print("=" * 60)
    print("      X-Track 2.5 - 卫星历书 (Almanac) 在线生成工具")
    print("=" * 60)

    # 1. 下载 YUMA 历书
    yuma_text = download_yuma_almanac()
    if not yuma_text:
        print("\n[!] 错误: 无法从网络获取历书，请检查网络连接或代理设置。")
        return False

    # 2. 解析历书
    satellites = parse_yuma_text(yuma_text)
    if not satellites:
        print("\n[!] 错误: 历书解析失败，未能提取到有效的卫星参数。")
        return False

    print(f"[+] 成功解析到 {len(satellites)} 颗在轨卫星的历书轨道根数。")

    # 3. 编码 CASIC 二进制报文序列
    raw_packets = bytearray()
    packet_count = 0

    # 先构造一个系统健康度包 AID-HUI (Class 0x0B, ID 0x03)
    now_unix = int(time.time())
    hui_payload = struct.pack('<IIII', 0xFFFFFFFF, 0, 0, now_unix)
    raw_packets.extend(build_casic_packet(0x0B, 0x03, hui_payload))
    packet_count += 1

    # 依次打包每颗卫星的 AID-ALM (Class 0x0B, ID 0x30)
    for sat in satellites:
        payload = encode_casic_aid_alm(sat)
        packet = build_casic_packet(0x0B, 0x30, payload)
        raw_packets.extend(packet)
        packet_count += 1

    payload_size = len(raw_packets)
    crc32_val = zlib.crc32(raw_packets) & 0xFFFFFFFF

    # 4. 构造 X-Track 32 字节头部 (GPS_Almanac_Header_t)
    header = struct.pack(
        '<IIIIIIII',
        HEADER_MAGIC,       # magic: 'GALM' (0x4D4C4147)
        HEADER_VERSION,     # version: 1
        now_unix,           # saveUnixTime
        payload_size,       # payloadSize
        packet_count,       # packetCount
        crc32_val,          # crc32
        0, 0                # reserved[2]
    )

    final_data = header + raw_packets

    # 5. 写入目标文件
    try:
        with open(output_path, 'wb') as f:
            f.write(final_data)
        print("\n" + "=" * 60)
        print(f"[√] 成功生成历书文件: {os.path.abspath(output_path)}")
        print(f"    - 文件大小: {len(final_data)} 字节")
        print(f"    - 包含数据包: {packet_count} 个 ({len(satellites)} 颗卫星 + 1 个健康包)")
        print(f"    - 数据校验和 (CRC32): 0x{crc32_val:08X}")
        print(f"    - 生成时间: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(now_unix))}")
        print("=" * 60)
        print("\n【使用指引】:")
        print("1. 将生成的 'gpsalm.bin' 复制到 X-Track 的 SD 卡根目录中 (即 /gpsalm.bin);")
        print("2. 将 SD 卡插回码表并开机;")
        print("3. 开机时 X-Track 会自动读取该文件并灌入 GPS 芯片，大幅加快搜星！")
        return True
    except Exception as e:
        print(f"\n[!] 写入文件失败: {e}")
        return False


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "gpsalm.bin"
    generate_gpsalm_file(out)
