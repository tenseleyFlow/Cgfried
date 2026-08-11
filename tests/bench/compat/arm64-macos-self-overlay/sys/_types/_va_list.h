#ifndef CGF_BENCH_ARM64_MACOS_OVERLAY_SYS_TYPES_VA_LIST_H
#define CGF_BENCH_ARM64_MACOS_OVERLAY_SYS_TYPES_VA_LIST_H

/*
 * Cgfried's shipped <stdarg.h> owns va_list for compilations it drives. The
 * Apple SDK otherwise supplies a second, incompatible typedef. Substitute
 * only when an existing include reaches the SDK's va_list boundary.
 */
#include <stdarg.h>
#define _VA_LIST_T 1
#include_next <sys/_types/_va_list.h>

#endif
