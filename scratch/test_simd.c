#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define TILE_SIZE 256
#define TILE_CHUNK_ROWS 32
#define LV_COLOR_16_SWAP 1

static uint16_t g_buf1[TILE_SIZE * TILE_CHUNK_ROWS];
static uint16_t g_buf2[TILE_SIZE * TILE_CHUNK_ROWS];

void transform_scalar(uint16_t* px16) {
    for (int i = 0; i < TILE_SIZE * TILE_CHUNK_ROWS; i++) {
        uint32_t val = px16[i];
        uint32_t dr5 = (val >> 11) & 0x1F;
        uint32_t g6  = (val >> 5) & 0x3F;
        uint32_t db5 = val & 0x1F;
        uint32_t half_g = g6 >> 1;
        uint32_t r5 = (dr5 + half_g) & 0x1F;
        uint32_t b5 = (db5 + half_g) & 0x1F;
        uint16_t c565 = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
#if LV_COLOR_16_SWAP == 1
        px16[i] = (uint16_t)((c565 << 8) | (c565 >> 8));
#else
        px16[i] = c565;
#endif
    }
}

void transform_parallel(uint16_t* px16) {
    uint32_t* px32 = (uint32_t*)px16;
    for (int i = 0; i < (TILE_SIZE * TILE_CHUNK_ROWS) / 2; i++) {
        uint32_t pair = px32[i];
        uint32_t val0 = pair & 0xFFFF;
        uint32_t val1 = pair >> 16;

        uint32_t dr0 = (val0 >> 11) & 0x1F;
        uint32_t g0  = (val0 >> 5) & 0x3F;
        uint32_t db0 = val0 & 0x1F;
        uint32_t hg0 = g0 >> 1;
        uint32_t r0  = (dr0 + hg0) & 0x1F;
        uint32_t b0  = (db0 + hg0) & 0x1F;
        uint32_t c0  = (r0 << 11) | (g0 << 5) | b0;

        uint32_t dr1 = (val1 >> 11) & 0x1F;
        uint32_t g1  = (val1 >> 5) & 0x3F;
        uint32_t db1 = val1 & 0x1F;
        uint32_t hg1 = g1 >> 1;
        uint32_t r1  = (dr1 + hg1) & 0x1F;
        uint32_t b1  = (db1 + hg1) & 0x1F;
        uint32_t c1  = (r1 << 11) | (g1 << 5) | b1;

#if LV_COLOR_16_SWAP == 1
        c0 = ((c0 << 8) & 0xFF00) | ((c0 >> 8) & 0x00FF);
        c1 = ((c1 << 8) & 0xFF00) | ((c1 >> 8) & 0x00FF);
#endif
        px32[i] = c0 | (c1 << 16);
    }
}

int main() {
    for (int i = 0; i < TILE_SIZE * TILE_CHUNK_ROWS; i++) {
        g_buf1[i] = (uint16_t)(i * 37 + 11);
        g_buf2[i] = g_buf1[i];
    }

    transform_scalar(g_buf1);
    transform_parallel(g_buf2);

    if (memcmp(g_buf1, g_buf2, sizeof(g_buf1)) == 0) {
        printf("VERIFICATION SUCCESS: Parallel transform is 100%% bit-exact identical!\n");
        return 0;
    } else {
        printf("ERROR: Mismatch between scalar and parallel transforms!\n");
        return 1;
    }
}
