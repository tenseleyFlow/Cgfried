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
        /* Apple's headers dispatch on `__arm64__`, NOT on `__aarch64__`:
         * sys/cdefs.h `#error Unsupported architecture` and
         * machine/_types.h `#error architecture not supported` both test
         * only the Apple spelling, so `#include <stdio.h>` fails on the
         * first line without it. Both spellings ship because third-party
         * code tests either.
         *
         * The minimum OS version appears in THREE places that must agree:
         * here, the emitter's `.build_version macos, 11, 0`, and the
         * linker's `-platform_version macos 11.0`. Availability.h reads
         * this one.
         *
         * NOT defined, deliberately: __APPLE_CC__ and
         * __apple_build_version__. They are compiler-identity macros, and
         * a header that sees them assumes Apple-clang extensions — the
         * same reason __GNUC__ stays absent until Sprint 55. */
        buf_printf(out,
                   "#define __arm64__ 1\n#define __arm64 1\n"
                   "#define __aarch64__ 1\n#define __AARCH64EL__ 1\n"
                   "#define __ARM64_ARCH_8__ 1\n#define __AARCH64_SIMD__ 1\n"
                   "#define __AARCH64_CMODEL_SMALL__ 1\n"
                   "#define __APPLE__ 1\n#define __MACH__ 1\n"
                   "#define __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ "
                   "110000\n"
                   "#define __ENVIRONMENT_OS_VERSION_MIN_REQUIRED__ 110000\n");
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

    /* Sprint 28: the exact-width / least / fast families and the
     * floating-point limits our freestanding <stdint.h>, <limits.h> and
     * <float.h> are written in terms of. This is the gcc technique —
     * one header per macro set, all target truth HERE (the sole target
     * site). Values verified against gcc -dM on x86_64-linux-gnu. */
    {
        /* int64_t is `long` on the LP64 ELF targets and `long long` on
         * Darwin; the literal suffix follows the type. */
        bool darwin = t.kind == CGF_TARGET_ARM64_MACOS;
        const char *i64 = darwin ? "long long int" : "long int";
        const char *u64 =
            darwin ? "long long unsigned int" : "long unsigned int";
        const char *sfx = darwin ? "LL" : "L";
        const char *usfx = darwin ? "ULL" : "UL";
        /* int_fastN_t: glibc/musl/FreeBSD widen 16/32/64 to `long`;
         * Darwin keeps the natural widths. The sprint file flags this
         * as the row most likely to be hardcoded wrong. */
        const char *f16 = darwin ? "short int" : "long int";
        const char *f32 = darwin ? "int" : "long int";
        const char *f16m = darwin ? "0x7fff" : "0x7fffffffffffffffL";
        const char *f32m = darwin ? "0x7fffffff" : "0x7fffffffffffffffL";
        unsigned f16w = darwin ? 16u : 64u, f32w = darwin ? 32u : 64u;

        buf_printf(out,
                   "#define __INT8_TYPE__ signed char\n"
                   "#define __INT16_TYPE__ short int\n"
                   "#define __INT32_TYPE__ int\n"
                   "#define __INT64_TYPE__ %s\n"
                   "#define __UINT8_TYPE__ unsigned char\n"
                   "#define __UINT16_TYPE__ short unsigned int\n"
                   "#define __UINT32_TYPE__ unsigned int\n"
                   "#define __UINT64_TYPE__ %s\n"
                   "#define __INT8_MAX__ 0x7f\n"
                   "#define __INT16_MAX__ 0x7fff\n"
                   "#define __INT32_MAX__ 0x7fffffff\n"
                   "#define __INT64_MAX__ 0x7fffffffffffffff%s\n"
                   "#define __UINT8_MAX__ 0xff\n"
                   "#define __UINT16_MAX__ 0xffff\n"
                   "#define __UINT32_MAX__ 0xffffffffU\n"
                   "#define __UINT64_MAX__ 0xffffffffffffffff%s\n"
                   "#define __INT8_C(c) c\n"
                   "#define __INT16_C(c) c\n"
                   "#define __INT32_C(c) c\n"
                   "#define __INT64_C(c) c ## %s\n"
                   "#define __UINT8_C(c) c\n"
                   "#define __UINT16_C(c) c\n"
                   "#define __UINT32_C(c) c ## U\n"
                   "#define __UINT64_C(c) c ## %s\n"
                   "#define __INTMAX_C(c) c ## L\n"
                   "#define __UINTMAX_C(c) c ## UL\n",
                   i64, u64, sfx, usfx, sfx, usfx);
        buf_printf(out,
                   "#define __INT_LEAST8_TYPE__ signed char\n"
                   "#define __INT_LEAST16_TYPE__ short int\n"
                   "#define __INT_LEAST32_TYPE__ int\n"
                   "#define __INT_LEAST64_TYPE__ %s\n"
                   "#define __UINT_LEAST8_TYPE__ unsigned char\n"
                   "#define __UINT_LEAST16_TYPE__ short unsigned int\n"
                   "#define __UINT_LEAST32_TYPE__ unsigned int\n"
                   "#define __UINT_LEAST64_TYPE__ %s\n"
                   "#define __INT_LEAST8_MAX__ 0x7f\n"
                   "#define __INT_LEAST16_MAX__ 0x7fff\n"
                   "#define __INT_LEAST32_MAX__ 0x7fffffff\n"
                   "#define __INT_LEAST64_MAX__ 0x7fffffffffffffff%s\n"
                   "#define __UINT_LEAST8_MAX__ 0xff\n"
                   "#define __UINT_LEAST16_MAX__ 0xffff\n"
                   "#define __UINT_LEAST32_MAX__ 0xffffffffU\n"
                   "#define __UINT_LEAST64_MAX__ 0xffffffffffffffff%s\n"
                   "#define __INT_LEAST8_WIDTH__ 8\n"
                   "#define __INT_LEAST16_WIDTH__ 16\n"
                   "#define __INT_LEAST32_WIDTH__ 32\n"
                   "#define __INT_LEAST64_WIDTH__ 64\n",
                   i64, u64, sfx, usfx);
        buf_printf(
            out,
            "#define __INT_FAST8_TYPE__ signed char\n"
            "#define __INT_FAST16_TYPE__ %s\n"
            "#define __INT_FAST32_TYPE__ %s\n"
            "#define __INT_FAST64_TYPE__ %s\n"
            "#define __UINT_FAST8_TYPE__ unsigned char\n"
            "#define __UINT_FAST16_TYPE__ %s unsigned int\n"
            "#define __UINT_FAST32_TYPE__ %s unsigned int\n"
            "#define __UINT_FAST64_TYPE__ %s\n"
            "#define __INT_FAST8_MAX__ 0x7f\n"
            "#define __INT_FAST16_MAX__ %s\n"
            "#define __INT_FAST32_MAX__ %s\n"
            "#define __INT_FAST64_MAX__ 0x7fffffffffffffff%s\n"
            "#define __UINT_FAST8_MAX__ 0xff\n"
            "#define __UINT_FAST16_MAX__ %s\n"
            "#define __UINT_FAST32_MAX__ %s\n"
            "#define __UINT_FAST64_MAX__ 0xffffffffffffffff%s\n"
            "#define __INT_FAST8_WIDTH__ 8\n"
            "#define __INT_FAST16_WIDTH__ %u\n"
            "#define __INT_FAST32_WIDTH__ %u\n"
            "#define __INT_FAST64_WIDTH__ 64\n",
            f16, f32, i64, darwin ? "short" : "long", darwin ? "" : "long", u64,
            f16m, f32m, sfx, darwin ? "0xffff" : "0xffffffffffffffffUL",
            darwin ? "0xffffffffU" : "0xffffffffffffffffUL", usfx, f16w, f32w);
        /* Plain char signedness: AAPCS64 Linux makes it UNSIGNED. */
        if (t.kind == CGF_TARGET_ARM64_LINUX)
            buf_printf(out, "#define __CHAR_UNSIGNED__ 1\n");
        /* wchar_t is unsigned int on arm64-linux, int elsewhere; the
         * shared block above spelled the common case, so correct it. */
        if (t.kind == CGF_TARGET_ARM64_LINUX)
            buf_printf(out, "#undef __WCHAR_TYPE__\n"
                            "#define __WCHAR_TYPE__ unsigned int\n"
                            "#undef __WCHAR_MAX__\n"
                            "#define __WCHAR_MAX__ 0xffffffffU\n"
                            "#define __WCHAR_MIN__ 0U\n");
        else
            buf_printf(out, "#define __WCHAR_MIN__ (-__WCHAR_MAX__ - 1)\n");
        /* Spelled exactly as gcc does (pp_dm_check compares the -dM
         * TEXT, so a numerically-equal respelling is still a diff). */
        buf_printf(out, "#define __SIG_ATOMIC_TYPE__ int\n"
                        "#define __SIG_ATOMIC_MAX__ 0x7fffffff\n"
                        "#define __SIG_ATOMIC_MIN__ (-__SIG_ATOMIC_MAX__ - 1)\n"
                        "#define __WINT_MIN__ 0U\n");

        /* FLT/DBL are IEEE binary32/64 on every v0.1.0 target; LDBL is
         * the three-column table (x87 80-bit, fp128, Apple's double).
         * Digits come from the Sprint 15 softfloat parameters — the
         * host FPU never produced any of these. */
        buf_printf(
            out,
            "#define __FLT_RADIX__ 2\n"
            "#define __FLT_MANT_DIG__ 24\n"
            "#define __FLT_DIG__ 6\n"
            "#define __FLT_DECIMAL_DIG__ 9\n"
            "#define __FLT_MIN_EXP__ (-125)\n"
            "#define __FLT_MAX_EXP__ 128\n"
            "#define __FLT_MIN_10_EXP__ (-37)\n"
            "#define __FLT_MAX_10_EXP__ 38\n"
            "#define __FLT_EPSILON__ "
            "1.19209289550781250000000000000000000e-7F\n"
            "#define __FLT_MIN__ 1.17549435082228750796873653722224568e-38F\n"
            "#define __FLT_MAX__ 3.40282346638528859811704183484516925e+38F\n"
            "#define __FLT_DENORM_MIN__ "
            "1.40129846432481707092372958328991613e-45F\n"
            "#define __FLT_HAS_DENORM__ 1\n"
            "#define __FLT_HAS_INFINITY__ 1\n"
            "#define __FLT_HAS_QUIET_NAN__ 1\n"
            "#define __FLT_EVAL_METHOD__ 0\n"
            "#define __DBL_MANT_DIG__ 53\n"
            "#define __DBL_DIG__ 15\n"
            "#define __DBL_DECIMAL_DIG__ 17\n"
            "#define __DBL_MIN_EXP__ (-1021)\n"
            "#define __DBL_MAX_EXP__ 1024\n"
            "#define __DBL_MIN_10_EXP__ (-307)\n"
            "#define __DBL_MAX_10_EXP__ 308\n"
            "#define __DBL_EPSILON__ "
            "((double)2.22044604925031308084726333618164062e-16L)\n"
            "#define __DBL_MIN__ "
            "((double)2.22507385850720138309023271733240406e-308L)\n"
            "#define __DBL_MAX__ "
            "((double)1.79769313486231570814527423731704357e+308L)\n"
            "#define __DBL_DENORM_MIN__ "
            "((double)4.94065645841246544176568792868221372e-324L)\n"
            "#define __DBL_HAS_DENORM__ 1\n"
            "#define __DBL_HAS_INFINITY__ 1\n"
            "#define __DBL_HAS_QUIET_NAN__ 1\n");
        switch (t.kind) {
        case CGF_TARGET_X86_64_LINUX_GNU:
        case CGF_TARGET_X86_64_LINUX_MUSL:
        case CGF_TARGET_X86_64_FREEBSD:
            buf_printf(out, "#define __LDBL_MANT_DIG__ 64\n"
                            "#define __LDBL_DIG__ 18\n"
                            "#define __LDBL_DECIMAL_DIG__ 21\n"
                            "#define __LDBL_MIN_EXP__ (-16381)\n"
                            "#define __LDBL_MAX_EXP__ 16384\n"
                            "#define __LDBL_MIN_10_EXP__ (-4931)\n"
                            "#define __LDBL_MAX_10_EXP__ 4932\n"
                            "#define __LDBL_EPSILON__ "
                            "1.08420217248550443400745280086994171e-19L\n"
                            "#define __LDBL_MIN__ "
                            "3.36210314311209350626267781732175260e-4932L\n"
                            "#define __LDBL_MAX__ "
                            "1.18973149535723176502126385303097021e+4932L\n"
                            "#define __LDBL_DENORM_MIN__ "
                            "3.64519953188247460252840593361941982e-4951L\n"
                            "#define __LDBL_NORM_MAX__ "
                            "1.18973149535723176502126385303097021e+4932L\n");
            break;
        case CGF_TARGET_ARM64_LINUX:
            buf_printf(out, "#define __LDBL_MANT_DIG__ 113\n"
                            "#define __LDBL_DIG__ 33\n"
                            "#define __LDBL_DECIMAL_DIG__ 36\n"
                            "#define __LDBL_MIN_EXP__ (-16381)\n"
                            "#define __LDBL_MAX_EXP__ 16384\n"
                            "#define __LDBL_MIN_10_EXP__ (-4931)\n"
                            "#define __LDBL_MAX_10_EXP__ 4932\n"
                            "#define __LDBL_EPSILON__ "
                            "1.92592994438723585305597794258492732e-34L\n"
                            "#define __LDBL_MIN__ "
                            "3.36210314311209350626267781732175260e-4932L\n"
                            "#define __LDBL_MAX__ "
                            "1.18973149535723176508575932662800702e+4932L\n"
                            "#define __LDBL_DENORM_MIN__ "
                            "6.47517511943802511092443895822764655e-4966L\n"
                            "#define __LDBL_NORM_MAX__ "
                            "1.18973149535723176508575932662800702e+4932L\n");
            break;
        case CGF_TARGET_ARM64_MACOS:
            /* Apple makes long double the SAME as double (Sprint 14's
             * TargetLayout row; the reason this cannot key on arch). */
            buf_printf(out, "#define __LDBL_MANT_DIG__ 53\n"
                            "#define __LDBL_DIG__ 15\n"
                            "#define __LDBL_DECIMAL_DIG__ 17\n"
                            "#define __LDBL_MIN_EXP__ (-1021)\n"
                            "#define __LDBL_MAX_EXP__ 1024\n"
                            "#define __LDBL_MIN_10_EXP__ (-307)\n"
                            "#define __LDBL_MAX_10_EXP__ 308\n"
                            "#define __LDBL_EPSILON__ "
                            "2.22044604925031308084726333618164062e-16L\n"
                            "#define __LDBL_MIN__ "
                            "2.22507385850720138309023271733240406e-308L\n"
                            "#define __LDBL_MAX__ "
                            "1.79769313486231570814527423731704357e+308L\n"
                            "#define __LDBL_DENORM_MIN__ "
                            "4.94065645841246544176568792868221372e-324L\n"
                            "#define __LDBL_NORM_MAX__ "
                            "1.79769313486231570814527423731704357e+308L\n");
            break;
        }
        buf_printf(out, "#define __LDBL_HAS_DENORM__ 1\n"
                        "#define __LDBL_HAS_INFINITY__ 1\n"
                        "#define __LDBL_HAS_QUIET_NAN__ 1\n");
        /* gcc also publishes NORM_MAX (== MAX, no subnormals involved)
         * and the IEC-60559 conformance flags. NORM_MAX must be the
         * LITERAL, not `__FLT_MAX__`: scripts/pp_dm_check.sh compares
         * -dM values against gcc's textually, and an alias is not the
         * same string (it caught exactly that here). */
        buf_printf(out,
                   "#define __FLT_NORM_MAX__ "
                   "3.40282346638528859811704183484516925e+38F\n"
                   "#define __DBL_NORM_MAX__ "
                   "((double)1.79769313486231570814527423731704357e+308L)\n"
                   "#define __FLT_IS_IEC_60559__ 1\n"
                   "#define __DBL_IS_IEC_60559__ 1\n"
                   "#define __LDBL_IS_IEC_60559__ 1\n"
                   "#define __FLT_EVAL_METHOD_TS_18661_3__ 0\n");
    }
}

size_t cgf_target_system_include_dirs(TargetSpec t, const char **out,
                                      size_t max)
{
    /* /usr/local/include, then the DEBIAN MULTIARCH dir, then
     * /usr/include — gcc's order on a multiarch host.
     *
     * The multiarch entry is not optional politeness: on Debian/Ubuntu
     * glibc's `bits/` headers live ONLY there, so `#include <stdio.h>`
     * fails outright without it (found by the Sprint 28 header lane on
     * CI; Arch keeps everything under /usr/include, which is why the
     * Sprint 5 acid test never noticed). A dir that does not exist
     * simply never matches, so listing it costs nothing elsewhere.
     * Real sysroot/SDK divergence arrives with Sprints 50/51; keeping
     * the switch exhaustive means those sprints cannot forget a
     * target. */
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
        if (max < 3)
            return 0;
        out[0] = "/usr/local/include";
        out[1] = "/usr/include/x86_64-linux-gnu";
        out[2] = "/usr/include";
        return 3;
    case CGF_TARGET_ARM64_LINUX:
        if (max < 3)
            return 0;
        out[0] = "/usr/local/include";
        out[1] = "/usr/include/aarch64-linux-gnu";
        out[2] = "/usr/include";
        return 3;
    case CGF_TARGET_X86_64_LINUX_MUSL:
        if (max < 3)
            return 0;
        out[0] = "/usr/local/include";
        out[1] = "/usr/include/x86_64-linux-musl";
        out[2] = "/usr/include";
        return 3;
    case CGF_TARGET_ARM64_MACOS:
        /* macOS has NO /usr/include. Every system header lives inside an
         * SDK whose path moves with Xcode and can only be learned by
         * running `xcrun`, which is a subprocess and therefore cannot
         * happen in this file — target.c is pure by construction. The
         * driver asks cgf_probe_macos_sdk() and prepends the root to the
         * two suffixes below, so the ORDER still lives here with every
         * other target's. */
        if (max < 2)
            return 0;
        out[0] = "/usr/local/include";
        out[1] = "/usr/include";
        return 2;
    case CGF_TARGET_X86_64_FREEBSD:
        if (max < 2)
            return 0;
        out[0] = "/usr/local/include";
        out[1] = "/usr/include";
        return 2;
    }
    CGF_ICE("cgf_target_system_include_dirs: bad target kind %d", (int)t.kind);
}

/* Sprint 27: the ELF dynamic-linker table. arm64-macos is dyld's world
 * (Sprint 50) — NULL here, and the link path must never ask. */
const char *cgf_target_dynamic_linker(TargetSpec t)
{
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
        return "/lib64/ld-linux-x86-64.so.2";
    case CGF_TARGET_X86_64_LINUX_MUSL:
        return "/lib/ld-musl-x86_64.so.1";
    case CGF_TARGET_ARM64_LINUX:
        return "/lib/ld-linux-aarch64.so.1";
    case CGF_TARGET_X86_64_FREEBSD:
        return "/libexec/ld-elf.so.1";
    case CGF_TARGET_ARM64_MACOS:
        return NULL;
    }
    CGF_ICE("cgf_target_dynamic_linker: bad target kind %d", (int)t.kind);
}

/* The Debian/Ubuntu multiarch tuple, which is NOT the target name: Debian
 * spells arm64 `aarch64-linux-gnu` while our closed target set calls it
 * `arm64-linux`. The crt probe and the system include path both key off
 * this, and hardcoding the x86 tuple is exactly how a native arm64 link
 * failed to find crt1.o. */
const char *cgf_target_multiarch(TargetSpec t)
{
    switch (t.kind) {
    case CGF_TARGET_X86_64_LINUX_GNU:
    case CGF_TARGET_X86_64_LINUX_MUSL:
        return "x86_64-linux-gnu";
    case CGF_TARGET_ARM64_LINUX:
        return "aarch64-linux-gnu";
    case CGF_TARGET_ARM64_MACOS:
    case CGF_TARGET_X86_64_FREEBSD:
        /* No multiarch layout on either. */
        return NULL;
    }
    CGF_ICE("cgf_target_multiarch: bad target kind %d", (int)t.kind);
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
