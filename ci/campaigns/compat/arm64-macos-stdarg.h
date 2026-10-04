#ifndef CGF_CAMPAIGN_ARM64_MACOS_STDARG_H
#define CGF_CAMPAIGN_ARM64_MACOS_STDARG_H

/*
 * Cgfried's shipped <stdarg.h> owns va_list for compilations it drives.
 * Apple's sys/_types/_va_list.h otherwise selects a void-pointer fallback for
 * compilers that do not advertise GCC identity and redeclares va_list with an
 * incompatible type. Include Cgfried's definition first and then use the
 * SDK's public include guard to suppress only that duplicate declaration.
 * Native __uint128_t remains a compiler type; this header replaces no types.
 */
#if !defined(__CGFRIED__) || !defined(__aarch64__) || !defined(__APPLE__)
#error "arm64-macos-stdarg.h is only for Cgfried ARM64 macOS campaigns"
#endif

#include <stdarg.h>
#define _VA_LIST_T 1

#endif
