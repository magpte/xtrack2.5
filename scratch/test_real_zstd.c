#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define ZSTD_DECODER_INTERNAL_BUFFER 64
#include "zstd_decompress.h"
#include "zstd_decompress.c"

int main() {
    zstd_decompress_init();
    if (s_zstd_dctx == NULL) {
        printf("FAILED: s_zstd_dctx is NULL!\n");
        return 1;
    }
    printf("SUCCESS: s_zstd_dctx initialized successfully! sizeof(ZSTD_DCtx)=%u, workspace=%u\n",
           (unsigned)sizeof(ZSTD_DCtx), (unsigned)sizeof(s_zstd_workspace));
    return 0;
}
