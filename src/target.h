#ifndef CGF_TARGET_H
#define CGF_TARGET_H

#include <stdbool.h>
#include <stddef.h>

#include "util/buf.h"

/* Closed target set — no triple parser (locked decision). Adding a target is
 * an enum variant plus the exhaustive-switch fallout, on purpose. */
typedef enum {
    CGF_TARGET_X86_64_LINUX_GNU,
    CGF_TARGET_ARM64_LINUX,
    CGF_TARGET_ARM64_MACOS,
    CGF_TARGET_X86_64_LINUX_MUSL,
    CGF_TARGET_X86_64_FREEBSD,
} TargetKind;

typedef struct {
    TargetKind kind;
} TargetSpec;

enum { CGF_TARGET_COUNT = 5 };

/* The closed name set, indexed by TargetKind — the single source the test
 * runner's target selectors validate against. */
extern const char *const cgf_target_names[CGF_TARGET_COUNT];

/* The ONLY function in the repo allowed to sniff the host (macro checks on
 * the compiling toolchain). A host check anywhere else conflates host with
 * target and rots the cross-targeting story — review rejection. */
TargetSpec cgf_target_host(void);

const char *cgf_target_name(TargetSpec t);

/* Appends the target's predefined-macro `#define` lines (the gcc -dM
 * core-integer subset: arch/OS/ABI ids, type sizes, limits, byte order,
 * __SIZE_TYPE__ family). NEVER __GNUC__ — see pp_predefine_all's policy. */
void cgf_target_predef_lines(TargetSpec t, bool gnu_mode, Buf *out);

/* Default system include directories for the target, in search order.
 * Returns the count written (<= max). Host-native only until sysroots
 * arrive (Sprint 51); macOS SDK discovery arrives with Sprint 50. */
size_t cgf_target_system_include_dirs(TargetSpec t, const char **out,
                                      size_t max);

#endif
