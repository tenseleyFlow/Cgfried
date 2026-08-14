#ifndef CGF_CAMPAIGN_ARM64_LINUX_U128_STORAGE_H
#define CGF_CAMPAIGN_ARM64_LINUX_U128_STORAGE_H

/*
 * ARM64 Linux system headers use __uint128_t for opaque vector-register
 * storage. GNU integer-128 remains outside Cgfried v0.1, so preserve only the
 * storage contract required by those declarations. The aggregate is
 * intentionally not arithmetic: a project that actually uses integer-128
 * operations must still fail instead of silently receiving different
 * semantics.
 */
#if !defined(__CGFRIED__) || !defined(__aarch64__) || !defined(__linux__)
#error "arm64-linux-u128-storage.h is only for Cgfried ARM64 Linux campaigns"
#endif

typedef struct {
    _Alignas(16) unsigned long long lo;
    unsigned long long hi;
} __cgf_campaign_u128_storage;

_Static_assert(sizeof(__cgf_campaign_u128_storage) == 16,
               "ARM64 vector-register storage must remain 16 bytes");
_Static_assert(_Alignof(__cgf_campaign_u128_storage) == 16,
               "ARM64 vector-register storage must remain 16-byte aligned");

#define __uint128_t __cgf_campaign_u128_storage

#endif
