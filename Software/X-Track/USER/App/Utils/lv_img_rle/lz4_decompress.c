/*
 * lz4_decompress.c
 *
 * Ultra-lightweight LZ4 decompressor optimized for ARM Cortex-M4 (AT32F435 @ 288MHz).
 * Part of X-Track 2.5 Map Tile Engine.
 *
 * Optimizations:
 *   1. Zero function-call overhead: replaces generic memcpy with inline 32-bit word transfers.
 *   2. 16-byte unrolled burst copy for long matches (offset >= 8) and literals.
 *   3. Hardware-friendly short-offset specialization (offset == 1, 2, 4) with 32-bit splatting.
 */
#include "lz4_decompress.h"
#include <string.h>

#define LZ4_MIN_MATCH 4

static inline uint16_t LZ4_readLE16(const void* memPtr)
{
    const uint8_t* p = (const uint8_t*)memPtr;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t LZ4_read32(const void* ptr)
{
    uint32_t val;
    memcpy(&val, ptr, sizeof(val));
    return val;
}

static inline void LZ4_write32(void* ptr, uint32_t val)
{
    memcpy(ptr, &val, sizeof(val));
}

/* Inline fast memory copy for literals and non-overlapping matches */
static inline void LZ4_copy_bytes(uint8_t* __restrict op, const uint8_t* __restrict ip, size_t length)
{
    while (length >= 16)
    {
        LZ4_write32(op + 0,  LZ4_read32(ip + 0));
        LZ4_write32(op + 4,  LZ4_read32(ip + 4));
        LZ4_write32(op + 8,  LZ4_read32(ip + 8));
        LZ4_write32(op + 12, LZ4_read32(ip + 12));
        op += 16;
        ip += 16;
        length -= 16;
    }
    while (length >= 4)
    {
        LZ4_write32(op, LZ4_read32(ip));
        op += 4;
        ip += 4;
        length -= 4;
    }
    while (length > 0)
    {
        *op++ = *ip++;
        length--;
    }
}

/* Optimized match copy handling overlap conditions */
static inline void LZ4_copy_match(uint8_t* op, const uint8_t* match, size_t length, uint16_t offset)
{
    uint8_t* const match_end = op + length;

    if (offset >= 8)
    {
        while (op + 16 <= match_end)
        {
            LZ4_write32(op + 0,  LZ4_read32(match + 0));
            LZ4_write32(op + 4,  LZ4_read32(match + 4));
            LZ4_write32(op + 8,  LZ4_read32(match + 8));
            LZ4_write32(op + 12, LZ4_read32(match + 12));
            op += 16;
            match += 16;
        }
        while (op + 4 <= match_end)
        {
            LZ4_write32(op, LZ4_read32(match));
            op += 4;
            match += 4;
        }
        while (op < match_end)
        {
            *op++ = *match++;
        }
    }
    else if (offset == 1)
    {
        uint8_t b = match[0];
        uint32_t val32 = (uint32_t)b * 0x01010101u;
        while (op + 16 <= match_end)
        {
            LZ4_write32(op + 0,  val32);
            LZ4_write32(op + 4,  val32);
            LZ4_write32(op + 8,  val32);
            LZ4_write32(op + 12, val32);
            op += 16;
        }
        while (op + 4 <= match_end)
        {
            LZ4_write32(op, val32);
            op += 4;
        }
        while (op < match_end)
        {
            *op++ = b;
        }
    }
    else if (offset == 2)
    {
        uint16_t h = (uint16_t)(match[0] | ((uint16_t)match[1] << 8));
        uint32_t val32 = (uint32_t)h | ((uint32_t)h << 16);
        while (op + 16 <= match_end)
        {
            LZ4_write32(op + 0,  val32);
            LZ4_write32(op + 4,  val32);
            LZ4_write32(op + 8,  val32);
            LZ4_write32(op + 12, val32);
            op += 16;
        }
        while (op + 4 <= match_end)
        {
            LZ4_write32(op, val32);
            op += 4;
        }
        while (op < match_end)
        {
            *op++ = match[0];
            if (op < match_end) *op++ = match[1];
        }
    }
    else if (offset == 4)
    {
        uint32_t val32 = LZ4_read32(match);
        while (op + 16 <= match_end)
        {
            LZ4_write32(op + 0,  val32);
            LZ4_write32(op + 4,  val32);
            LZ4_write32(op + 8,  val32);
            LZ4_write32(op + 12, val32);
            op += 16;
        }
        while (op + 4 <= match_end)
        {
            LZ4_write32(op, val32);
            op += 4;
        }
        while (op < match_end)
        {
            *op++ = *match++;
        }
    }
    else
    {
        while (op < match_end)
        {
            *op++ = *match++;
        }
    }
}

int LZ4_decompress_fast(const char* src, char* dest, int originalSize)
{
    const uint8_t* ip = (const uint8_t*)src;
    uint8_t* op = (uint8_t*)dest;
    uint8_t* const oend = op + originalSize;

    while (op < oend)
    {
        uint32_t token = *ip++;
        size_t length = token >> 4;

        /* 1. Literal run */
        if (length == 15)
        {
            uint32_t s;
            do {
                s = *ip++;
                length += s;
            } while (s == 255);
        }

        /* Copy literals */
        if (length > 0)
        {
            LZ4_copy_bytes(op, ip, length);
            op += length;
            ip += length;
        }

        if (op >= oend)
        {
            break;
        }

        /* 2. Match offset */
        uint16_t offset = LZ4_readLE16(ip);
        ip += 2;
        if (offset == 0)
        {
            return -1; /* Corrupt stream */
        }

        const uint8_t* match = op - offset;
        if (match < (const uint8_t*)dest)
        {
            return -2; /* Out of bounds */
        }

        /* 3. Match length */
        length = token & 0x0F;
        if (length == 15)
        {
            uint32_t s;
            do {
                s = *ip++;
                length += s;
            } while (s == 255);
        }
        length += LZ4_MIN_MATCH;

        /* Check bounds */
        if (op + length > oend)
        {
            return -3;
        }

        /* Copy match */
        LZ4_copy_match(op, match, length, offset);
        op += length;
    }

    return (int)(ip - (const uint8_t*)src);
}

int LZ4_decompress_safe(const char* src, char* dest, int compressedSize, int maxDecompressedSize)
{
    const uint8_t* ip = (const uint8_t*)src;
    const uint8_t* const iend = ip + compressedSize;
    uint8_t* op = (uint8_t*)dest;
    uint8_t* const oend = op + maxDecompressedSize;

    while (ip < iend)
    {
        uint32_t token = *ip++;
        size_t length = token >> 4;

        /* 1. Literal run */
        if (length == 15)
        {
            uint32_t s;
            do {
                if (ip >= iend) return -1;
                s = *ip++;
                length += s;
            } while (s == 255);
        }

        if (ip + length > iend || op + length > oend)
        {
            return -2;
        }

        if (length > 0)
        {
            LZ4_copy_bytes(op, ip, length);
            op += length;
            ip += length;
        }

        if (ip >= iend || op >= oend)
        {
            break;
        }

        /* 2. Match offset */
        if (ip + 2 > iend) return -3;
        uint16_t offset = LZ4_readLE16(ip);
        ip += 2;
        if (offset == 0) return -4;

        const uint8_t* match = op - offset;
        if (match < (const uint8_t*)dest) return -5;

        /* 3. Match length */
        length = token & 0x0F;
        if (length == 15)
        {
            uint32_t s;
            do {
                if (ip >= iend) return -6;
                s = *ip++;
                length += s;
            } while (s == 255);
        }
        length += LZ4_MIN_MATCH;

        if (op + length > oend) return -7;

        LZ4_copy_match(op, match, length, offset);
        op += length;
    }

    return (int)(op - (uint8_t*)dest);
}

