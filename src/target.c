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

void cgf_target_predef_lines(TargetSpec t, bool gnu_mode, Buf *out)
{
    /* Exhaustive on purpose: adding a target must force this table to be
     * revisited. Values are the LP64 SysV answers; per-target divergence
     * (char signedness macros, Mach-O, ILP32) arrives with those targets. */
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
    case CGF_TARGET_X86_64_LINUX_MUSL:
        buf_printf(out, "#define __x86_64__ 1\n#define __x86_64 1\n"
                        "#define __amd64__ 1\n#define __amd64 1\n");
        goto linux_common;
    case CGF_TARGET_ARM64_LINUX:
        buf_printf(out, "#define __aarch64__ 1\n");
    linux_common:
        buf_printf(out, "#define __linux__ 1\n#define __linux 1\n"
                        "#define __gnu_linux__ 1\n#define __unix__ 1\n"
                        "#define __unix 1\n#define __ELF__ 1\n");
        if (gnu_mode)
            buf_printf(out, "#define linux 1\n#define unix 1\n");
        break;
    case CGF_TARGET_ARM64_MACOS:
        buf_printf(out, "#define __aarch64__ 1\n#define __APPLE__ 1\n"
                        "#define __MACH__ 1\n");
        break;
    case CGF_TARGET_X86_64_FREEBSD:
        buf_printf(out, "#define __x86_64__ 1\n#define __x86_64 1\n"
                        "#define __amd64__ 1\n#define __amd64 1\n"
                        "#define __FreeBSD__ 15\n#define __unix__ 1\n"
                        "#define __unix 1\n#define __ELF__ 1\n");
        break;
    }

    /* LP64 core-integer subset (all five current targets agree). */
    buf_printf(out,
               "#define __LP64__ 1\n#define _LP64 1\n"
               "#define __CHAR_BIT__ 8\n"
               "#define __SIZEOF_SHORT__ 2\n#define __SIZEOF_INT__ 4\n"
               "#define __SIZEOF_LONG__ 8\n#define __SIZEOF_LONG_LONG__ 8\n"
               "#define __SIZEOF_POINTER__ 8\n#define __SIZEOF_SIZE_T__ 8\n"
               "#define __SIZEOF_PTRDIFF_T__ 8\n#define __SIZEOF_FLOAT__ 4\n"
               "#define __SIZEOF_DOUBLE__ 8\n"
               "#define __SIZEOF_LONG_DOUBLE__ 16\n"
               "#define __SIZEOF_WCHAR_T__ 4\n#define __SIZEOF_WINT_T__ 4\n"
               "#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__\n"
               "#define __ORDER_LITTLE_ENDIAN__ 1234\n"
               "#define __ORDER_BIG_ENDIAN__ 4321\n"
               "#define __ORDER_PDP_ENDIAN__ 3412\n"
               "#define __SCHAR_MAX__ 0x7f\n#define __SHRT_MAX__ 0x7fff\n"
               "#define __INT_MAX__ 0x7fffffff\n"
               "#define __LONG_MAX__ 0x7fffffffffffffffL\n"
               "#define __LONG_LONG_MAX__ 0x7fffffffffffffffLL\n"
               "#define __WCHAR_MAX__ 0x7fffffff\n"
               "#define __WINT_MAX__ 0xffffffffU\n"
               "#define __SIZE_MAX__ 0xffffffffffffffffUL\n"
               "#define __PTRDIFF_MAX__ 0x7fffffffffffffffL\n"
               "#define __INTMAX_MAX__ 0x7fffffffffffffffL\n"
               "#define __UINTMAX_MAX__ 0xffffffffffffffffUL\n"
               "#define __INTPTR_MAX__ 0x7fffffffffffffffL\n"
               "#define __UINTPTR_MAX__ 0xffffffffffffffffUL\n"
               "#define __SIZE_TYPE__ long unsigned int\n"
               "#define __PTRDIFF_TYPE__ long int\n"
               "#define __WCHAR_TYPE__ int\n"
               "#define __WINT_TYPE__ unsigned int\n"
               "#define __INTMAX_TYPE__ long int\n"
               "#define __UINTMAX_TYPE__ long unsigned int\n"
               "#define __INTPTR_TYPE__ long int\n"
               "#define __UINTPTR_TYPE__ long unsigned int\n"
               "#define __CHAR16_TYPE__ short unsigned int\n"
               "#define __CHAR32_TYPE__ unsigned int\n");
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

/* The §7 long-double table, plus pointer facts. Apple's arm64 makes long
 * double the SAME as double, which is a real divergence from AAPCS64 and
 * the reason this cannot be keyed on architecture alone. */
TargetLayout cgf_target_layout(TargetSpec t)
{
    TargetLayout l;

    l.ptr_size = 8;
    l.ptr_align = 8;
    l.max_align = 16;
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
    case CGF_TARGET_X86_64_LINUX_MUSL:
    case CGF_TARGET_X86_64_FREEBSD:
        /* x87 80-bit extended: 10 value bytes, 6 of padding, 16 stored. */
        l.ldbl_kind = CGF_LDBL_X87_80;
        l.ldbl_size = 16;
        l.ldbl_align = 16;
        break;
    case CGF_TARGET_ARM64_LINUX:
        /* IEEE binary128, software-emulated — Sprint 49 reuses Sprint 15's
         * softfloat as the runtime. */
        l.ldbl_kind = CGF_LDBL_IEEE128;
        l.ldbl_size = 16;
        l.ldbl_align = 16;
        break;
    case CGF_TARGET_ARM64_MACOS:
        /* Apple diverges: long double IS double. */
        l.ldbl_kind = CGF_LDBL_IS_DOUBLE;
        l.ldbl_size = 8;
        l.ldbl_align = 8;
        break;
    }
    return l;
}
