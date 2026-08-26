#define ZSTD_DECODER_INTERNAL_BUFFER 64
#include "zstd_decompress.c"
char sizeof_dctx_test[sizeof(ZSTD_DCtx)];
