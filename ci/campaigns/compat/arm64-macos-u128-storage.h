#ifndef CGF_CAMPAIGN_ARM64_MACOS_U128_STORAGE_H
#define CGF_CAMPAIGN_ARM64_MACOS_U128_STORAGE_H

/*
 * Apple ARM64 headers use __uint128_t for opaque machine-state storage, while
 * Cgfried deliberately leaves GNU integer-128 arithmetic outside v0.1. Include
 * the compiler's stdarg contract first, then keep the SDK from redeclaring its
 * va_list and preserve only the unused 16-byte storage layout. A project that
 * attempts integer-128 arithmetic still fails because this is an aggregate.
 */
#if !defined(__CGFRIED__) || !defined(__aarch64__) || !defined(__APPLE__)
#error "arm64-macos-u128-storage.h is only for Cgfried ARM64 macOS campaigns"
#endif

#include <stdarg.h>
#define _VA_LIST_T 1

typedef struct {
    _Alignas(16) unsigned long long lo;
    unsigned long long hi;
} __cgf_campaign_macos_u128_storage;

_Static_assert(sizeof(__cgf_campaign_macos_u128_storage) == 16,
               "ARM64 machine-state storage must remain 16 bytes");
_Static_assert(_Alignof(__cgf_campaign_macos_u128_storage) == 16,
               "ARM64 machine-state storage must remain 16-byte aligned");

#define __uint128_t __cgf_campaign_macos_u128_storage

#endif
