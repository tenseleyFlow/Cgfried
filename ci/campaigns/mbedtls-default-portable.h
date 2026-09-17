#ifndef CGF_CAMPAIGN_MBEDTLS_DEFAULT_PORTABLE_H
#define CGF_CAMPAIGN_MBEDTLS_DEFAULT_PORTABLE_H

/*
 * Preserve Mbed TLS 3.6.7's upstream default configuration while removing the
 * one accelerator that hard-requires GCC identity, x86 intrinsics, or GNU
 * inline assembly. Both the Cgfried and host-GCC trees use this same overlay.
 * Target-neutral library behavior remains covered by the 30-suite self-test.
 */
#include "mbedtls/mbedtls_config.h"
#undef MBEDTLS_AESNI_C

#endif
