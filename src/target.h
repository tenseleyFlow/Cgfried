#ifndef CGF_TARGET_H
#define CGF_TARGET_H

#include <stdbool.h>
#include <stddef.h>

#include "util/base.h"
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

/* How the target spells `long double` — the single biggest cross-target
 * layout trap, and the reason this is a table rather than a constant.
 * Sprint 15's float engine folds in the TARGET's format, and Sprint 28's
 * <float.h> is emitted from it. */
typedef enum {
    CGF_LDBL_X87_80,   /* 80-bit x87 extended: 10 value bytes, 16 stored */
    CGF_LDBL_IEEE128,  /* IEEE binary128; soft-float on arm64-linux */
    CGF_LDBL_IS_DOUBLE /* Apple: long double IS double (LDBL_MANT_DIG 53) */
} LongDoubleKind;

/* Everything layout needs from a target, in one place. Pure data: a
 * function of TargetKind alone, so `sizeof` folds identically whichever
 * host is doing the cross-compile. */
typedef struct {
    u64 ptr_size;
    u64 ptr_align;
    u64 ldbl_size;
    u64 ldbl_align;
    LongDoubleKind ldbl_kind;
    /* max_align_t's alignment: 16 on x86-64 AND arm64 (all five targets).
     * alignof(long long) is 8 here; it is 4 on i386, which we have no
     * target for — noted so the next data model does not surprise. */
    u64 max_align;
} TargetLayout;

TargetLayout cgf_target_layout(TargetSpec t);

/* The closed name set, indexed by TargetKind — the single source the test
 * runner's target selectors validate against. */
extern const char *const cgf_target_names[CGF_TARGET_COUNT];

/* The ONLY function in the repo allowed to sniff the host (macro checks on
 * the compiling toolchain). A host check anywhere else conflates host with
 * target and rots the cross-targeting story — review rejection. */
TargetSpec cgf_target_host(void);

/* The target being compiled FOR: the host unless --target= said otherwise.
 * THIS is what every code path outside target.c wants. Asking the host
 * instead yields a compiler that is right only when host == target, and a
 * cross build then miscompiles silently -- which is why
 * scripts/check_target_seam.sh rejects cgf_target_host() anywhere else. */
TargetSpec cgf_target_selected(void);

/* Select by exact name from the closed set; false = unknown (the caller
 * reports it and lists cgf_target_names). There is no triple PARSER on
 * purpose: an unrecognised triple that silently degrades to a default is
 * the failure mode a closed enum exists to prevent. */
bool cgf_target_select(const char *name);

/* Debian/Ubuntu multiarch tuple (NULL where the layout does not exist).
 * NOT the target name: Debian spells arm64 `aarch64-linux-gnu`. */
const char *cgf_target_multiarch(TargetSpec t);
/* True where every image is position-independent no matter what the
 * command line said, so an address in an initializer is always written
 * by the loader rather than fixed at link time. */
bool cgf_target_always_pic(TargetSpec t);

const char *cgf_target_name(TargetSpec t);

/* ELF dynamic-linker path for the target; NULL for arm64-macos (dyld,
 * Sprint 50). */
const char *cgf_target_dynamic_linker(TargetSpec t);

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
