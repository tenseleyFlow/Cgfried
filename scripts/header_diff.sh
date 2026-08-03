#!/bin/sh
# Sprint 28 (DoD 2): every macro our <stdint.h>/<limits.h>/<float.h>
# publish, printed by a program compiled with cgf and the same program
# compiled with gcc — stdout must be BYTE-EQUAL. That compares the
# VALUES (and the types they promote to), not the header text, so a
# wrong suffix or a wrong fast-type width shows up immediately.
#
# Types are checked the same way: sizeof/signedness of every typedef.
# HARNESS_SKIP (loudly) when gcc is missing.
set -u
LC_ALL=C
export LC_ALL

CGF=${1:-build/cgfried}
WORK=${CGF_HEADER_WORK:-build/header-diff}

if ! command -v gcc >/dev/null 2>&1; then
    echo "HARNESS_SKIP suite=headerdiff test=all count=1" \
        "reason=\"no gcc on this host\""
    exit 0
fi

rm -rf "$WORK"
mkdir -p "$WORK"
SRC="$WORK/probe.c"

cat > "$SRC" << 'EOF'
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdalign.h>
#include <iso646.h>

int printf(const char *, ...);

/* Signedness of a typedef without <type_traits>: -1 stays negative
 * only in a signed type. Width via sizeof. */
#define TY(t)                                                                  \
    printf("%s size=%u signed=%d\n", #t, (unsigned)sizeof(t),                  \
           (int)((t)-1 < (t)0))
#define D(m) printf("%s = %lld\n", #m, (long long)(m))
#define U(m) printf("%s = %llu\n", #m, (unsigned long long)(m))
/* Floating macros print with enough digits to expose any last-bit
 * difference in the constant we published. */
#define F(m) printf("%s = %.17g\n", #m, (double)(m))
#define LF(m) printf("%s = %.21Lg\n", #m, (long double)(m))

int main(void)
{
    TY(int8_t); TY(int16_t); TY(int32_t); TY(int64_t);
    TY(uint8_t); TY(uint16_t); TY(uint32_t); TY(uint64_t);
    TY(int_least8_t); TY(int_least16_t); TY(int_least32_t); TY(int_least64_t);
    TY(uint_least8_t); TY(uint_least16_t); TY(uint_least32_t);
    TY(uint_least64_t);
    TY(int_fast8_t); TY(int_fast16_t); TY(int_fast32_t); TY(int_fast64_t);
    TY(uint_fast8_t); TY(uint_fast16_t); TY(uint_fast32_t); TY(uint_fast64_t);
    TY(intptr_t); TY(uintptr_t); TY(intmax_t); TY(uintmax_t);
    TY(size_t); TY(ptrdiff_t); TY(wchar_t);
    /* max_align_t is a struct: size and alignment are the contract. */
    printf("max_align_t size=%u align=%u\n", (unsigned)sizeof(max_align_t),
           (unsigned)_Alignof(max_align_t));

    D(INT8_MIN); D(INT16_MIN); D(INT32_MIN); D(INT64_MIN);
    D(INT8_MAX); D(INT16_MAX); D(INT32_MAX); D(INT64_MAX);
    U(UINT8_MAX); U(UINT16_MAX); U(UINT32_MAX); U(UINT64_MAX);
    D(INT_LEAST8_MIN); D(INT_LEAST16_MIN); D(INT_LEAST32_MIN);
    D(INT_LEAST64_MIN);
    D(INT_LEAST8_MAX); D(INT_LEAST16_MAX); D(INT_LEAST32_MAX);
    D(INT_LEAST64_MAX);
    U(UINT_LEAST8_MAX); U(UINT_LEAST16_MAX); U(UINT_LEAST32_MAX);
    U(UINT_LEAST64_MAX);
    D(INT_FAST8_MIN); D(INT_FAST16_MIN); D(INT_FAST32_MIN); D(INT_FAST64_MIN);
    D(INT_FAST8_MAX); D(INT_FAST16_MAX); D(INT_FAST32_MAX); D(INT_FAST64_MAX);
    U(UINT_FAST8_MAX); U(UINT_FAST16_MAX); U(UINT_FAST32_MAX);
    U(UINT_FAST64_MAX);
    D(INTPTR_MIN); D(INTPTR_MAX); U(UINTPTR_MAX);
    D(INTMAX_MIN); D(INTMAX_MAX); U(UINTMAX_MAX);
    D(PTRDIFF_MIN); D(PTRDIFF_MAX); U(SIZE_MAX);
    D(SIG_ATOMIC_MIN); D(SIG_ATOMIC_MAX);
    D(WCHAR_MIN); D(WCHAR_MAX); U(WINT_MIN); U(WINT_MAX);
    /* _C macros: the suffix must make these the right TYPE, so print
     * the size of the resulting expression too. */
    printf("INT64_C size=%u val=%lld\n", (unsigned)sizeof(INT64_C(1)),
           (long long)INT64_C(-9223372036854775807));
    printf("UINT64_C size=%u val=%llu\n", (unsigned)sizeof(UINT64_C(1)),
           (unsigned long long)UINT64_C(18446744073709551615));
    printf("INT32_C size=%u\n", (unsigned)sizeof(INT32_C(1)));
    printf("UINT32_C size=%u\n", (unsigned)sizeof(UINT32_C(1)));
    printf("INTMAX_C size=%u\n", (unsigned)sizeof(INTMAX_C(1)));

    D(CHAR_BIT); D(SCHAR_MIN); D(SCHAR_MAX); U(UCHAR_MAX);
    D(CHAR_MIN); D(CHAR_MAX);
    D(SHRT_MIN); D(SHRT_MAX); U(USHRT_MAX);
    D(INT_MIN); D(INT_MAX); U(UINT_MAX);
    D(LONG_MIN); D(LONG_MAX); U(ULONG_MAX);
    D(LLONG_MIN); D(LLONG_MAX); U(ULLONG_MAX);
    D(MB_LEN_MAX);

    D(FLT_RADIX); D(FLT_EVAL_METHOD); D(FLT_ROUNDS); D(DECIMAL_DIG);
    D(FLT_MANT_DIG); D(FLT_DIG); D(FLT_DECIMAL_DIG);
    D(FLT_MIN_EXP); D(FLT_MIN_10_EXP); D(FLT_MAX_EXP); D(FLT_MAX_10_EXP);
    F(FLT_MAX); F(FLT_MIN); F(FLT_EPSILON); F(FLT_TRUE_MIN);
    D(DBL_MANT_DIG); D(DBL_DIG); D(DBL_DECIMAL_DIG);
    D(DBL_MIN_EXP); D(DBL_MIN_10_EXP); D(DBL_MAX_EXP); D(DBL_MAX_10_EXP);
    F(DBL_MAX); F(DBL_MIN); F(DBL_EPSILON); F(DBL_TRUE_MIN);
    D(LDBL_MANT_DIG); D(LDBL_DIG); D(LDBL_DECIMAL_DIG);
    D(LDBL_MIN_EXP); D(LDBL_MIN_10_EXP); D(LDBL_MAX_EXP); D(LDBL_MAX_10_EXP);
    LF(LDBL_MAX); LF(LDBL_MIN); LF(LDBL_EPSILON); LF(LDBL_TRUE_MIN);

    /* stddef / stdbool / stdalign / iso646 behavior. */
    printf("NULL is zero: %d\n", (void *)NULL == (void *)0);
    printf("bool: %d %d %u\n", (int)true, (int)false, (unsigned)sizeof(bool));
    printf("alignof: %u %u\n", (unsigned)alignof(long long),
           (unsigned)alignof(max_align_t));
    printf("iso646: %d %d\n", (1 and 1) bitor 0, not 0);
    return 0;
}
EOF

fails=0
"$CGF" "$SRC" -o "$WORK/cgf.bin" 2> "$WORK/cgf.err" || {
    echo "header_diff: cgf failed to build the probe" >&2
    cat "$WORK/cgf.err" >&2
    exit 1
}
gcc -std=c17 -w "$SRC" -o "$WORK/gcc.bin" 2> "$WORK/gcc.err" || {
    echo "header_diff: gcc failed to build the probe" >&2
    cat "$WORK/gcc.err" >&2
    exit 1
}
"$WORK/cgf.bin" > "$WORK/cgf.out" 2>&1
"$WORK/gcc.bin" > "$WORK/gcc.out" 2>&1

# THE one documented divergence (Sprint 28 deliverable 5): MB_LEN_MAX is
# 4 here (the UTF-8 maximum) and 16 in glibc (stateful legacy encodings).
# It is allowlisted BY VALUE on both sides — if either number moves, or
# any other line differs, this fails.
ours=$(grep '^MB_LEN_MAX = ' "$WORK/cgf.out" | sed 's/.*= //')
theirs=$(grep '^MB_LEN_MAX = ' "$WORK/gcc.out" | sed 's/.*= //')
if [ "$ours" != "4" ]; then
    echo "header_diff: our MB_LEN_MAX is $ours, expected the documented 4" >&2
    fails=$((fails + 1))
fi
if [ "$theirs" != "16" ]; then
    echo "header_diff: gcc's MB_LEN_MAX is now $theirs (was 16) — re-read" \
        "the divergence note in include/limits.h" >&2
    fails=$((fails + 1))
fi
grep -v '^MB_LEN_MAX = ' "$WORK/cgf.out" > "$WORK/cgf.cmp"
grep -v '^MB_LEN_MAX = ' "$WORK/gcc.out" > "$WORK/gcc.cmp"
if ! cmp -s "$WORK/cgf.cmp" "$WORK/gcc.cmp"; then
    echo "header_diff: macro/type values differ from gcc:" >&2
    diff -u "$WORK/gcc.cmp" "$WORK/cgf.cmp" >&2 || true
    fails=$((fails + 1))
fi
n=$(grep -c '' "$WORK/cgf.out")

# DoD 6: the shipped tree retains the nine top-level compiler-owned standard
# headers.  Namespaced extension headers may live in subdirectories without
# making this check mistake directories for headers.
hdrs=$(find include -type f | awk -F/ 'NF == 2 { n++ } END { print n + 0 }')
if [ "$hdrs" -ne 9 ]; then
    echo "header_diff: include/ has $hdrs top-level files, expected exactly 9" >&2
    fails=$((fails + 1))
fi
if [ ! -f include/cgfried/memsafe.h ]; then
    echo "header_diff: missing namespaced <cgfried/memsafe.h>" >&2
    fails=$((fails + 1))
fi
printf '#include <stdio.h>\nint main(void){return 0;}\n' > "$WORK/hosted.c"
if ! "$CGF" -M "$WORK/hosted.c" 2>/dev/null | grep -q '/usr/include/stdio.h'; then
    echo "header_diff: <stdio.h> did not resolve to the system header" >&2
    fails=$((fails + 1))
fi

[ "$fails" -eq 0 ] || exit 1
echo "header_diff: $n macro/type lines byte-identical to gcc;" \
    "$hdrs standard headers plus cgfried/memsafe.h, no libc shadowing"
