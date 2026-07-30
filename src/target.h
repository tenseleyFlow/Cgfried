#ifndef CGF_TARGET_H
#define CGF_TARGET_H

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

/* The ONLY function in the repo allowed to sniff the host (macro checks on
 * the compiling toolchain). A host check anywhere else conflates host with
 * target and rots the cross-targeting story — review rejection. */
TargetSpec cgf_target_host(void);

const char *cgf_target_name(TargetSpec t);

#endif
