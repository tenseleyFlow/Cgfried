#ifndef CGF_BENCH_ARM64_MACOS_OVERLAY_MACH_PORT_H
#define CGF_BENCH_ARM64_MACOS_OVERLAY_MACH_PORT_H

/*
 * Preserve the SDK's real declarations, then relax only its private ABI-size
 * assertion wrapper. This overlay is reached only when a measured source
 * naturally includes <mach/port.h>; it must never be force-included.
 */
#include_next <mach/port.h>

#undef xnu_static_assert_struct_size
#define xnu_static_assert_struct_size(name, expected_size)                     \
    _Static_assert(1, "XNU ABI layout is outside this syntax-only benchmark")

#endif
