#include "target.h"

#include "diag.h"

const char *const cgf_target_names[CGF_TARGET_COUNT] = {
    "x86_64-linux-gnu",  "arm64-linux",    "arm64-macos",
    "x86_64-linux-musl", "x86_64-freebsd",
};

TargetSpec cgf_target_host(void)
{
    TargetSpec t;

    /* The one sanctioned host-sniff site. Unknown hosts fail at compile time
     * (never a silent default). A musl-libc x86_64 host still reports the
     * -gnu target: host libc is irrelevant to what we generate for; the
     * default target is about the dominant ecosystem on that arch/OS. */
#if defined(__x86_64__) && defined(__linux__)
    t.kind = CGF_TARGET_X86_64_LINUX_GNU;
#elif defined(__aarch64__) && defined(__linux__)
    t.kind = CGF_TARGET_ARM64_LINUX;
#elif defined(__aarch64__) && defined(__APPLE__)
    t.kind = CGF_TARGET_ARM64_MACOS;
#elif defined(__x86_64__) && defined(__FreeBSD__)
    t.kind = CGF_TARGET_X86_64_FREEBSD;
#else
#error "unsupported host; add it to the closed target set deliberately"
#endif
    return t;
}

size_t cgf_target_system_include_dirs(TargetSpec t, const char **out,
                                      size_t max)
{
    /* One POSIX-shaped default list for every current target: real per-
     * target divergence (sysroots, macOS SDK paths) arrives with Sprints
     * 50/51; keeping the switch exhaustive means those sprints cannot
     * forget a target. */
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
    case CGF_TARGET_ARM64_LINUX:
    case CGF_TARGET_ARM64_MACOS:
    case CGF_TARGET_X86_64_LINUX_MUSL:
    case CGF_TARGET_X86_64_FREEBSD:
        if (max < 2)
            return 0;
        out[0] = "/usr/local/include";
        out[1] = "/usr/include";
        return 2;
    }
    CGF_ICE("cgf_target_system_include_dirs: bad target kind %d", (int)t.kind);
}

const char *cgf_target_name(TargetSpec t)
{
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
        return "x86_64-linux-gnu";
    case CGF_TARGET_ARM64_LINUX:
        return "arm64-linux";
    case CGF_TARGET_ARM64_MACOS:
        return "arm64-macos";
    case CGF_TARGET_X86_64_LINUX_MUSL:
        return "x86_64-linux-musl";
    case CGF_TARGET_X86_64_FREEBSD:
        return "x86_64-freebsd";
    }
    CGF_ICE("cgf_target_name: bad target kind %d", (int)t.kind);
}
