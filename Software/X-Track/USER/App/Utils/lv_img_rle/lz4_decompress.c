/*
 * lz4_decompress.c
 *
 * Ultra-lightweight LZ4 decompressor optimized for ARM Cortex-M4.
 * Part of X-Track 2.5 Map Tile Engine.
 */
#include "lz4_decompress.h"
#include <string.h>

#define LZ4_MIN_MATCH 4

static inline uint16_t LZ4_readLE16(const void* memPtr)
{
    const uint8_t* p = (const uint8_t*)memPtr;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
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
            memcpy(op, ip, length);
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

        /* Copy match (handling overlap safely) */
        uint8_t* match_end = op + length;
        if (match_end > oend)
        {
            return -3;
        }

        if (offset >= 8)
        {
            while (op + 8 <= match_end)
            {
                memcpy(op, match, 8);
                op += 8;
                match += 8;
            }
            while (op < match_end)
            {
                *op++ = *match++;
            }
        }
        else
        {
            /* Overlapping short offset copy */
            while (op < match_end)
            {
                *op++ = *match++;
            }
        }
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
            memcpy(op, ip, length);
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

        uint8_t* match_end = op + length;
        if (offset >= 8)
        {
            while (op + 8 <= match_end)
            {
                memcpy(op, match, 8);
                op += 8;
                match += 8;
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

    return (int)(op - (uint8_t*)dest);
}
