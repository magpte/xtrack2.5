#include <stdio.h>
#include "zstd.h"

int main() {
    size_t dctx_sz = ZSTD_estimateDCtxSize();
    printf("ZSTD_estimateDCtxSize = %u bytes (%u KB)\n", (unsigned)dctx_sz, (unsigned)(dctx_sz / 1024));
    return 0;
}
