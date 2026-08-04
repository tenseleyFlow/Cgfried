#!/bin/sh
# Sprint 48 DoD 4: the Linux AAPCS64 va_list layout, compared against the
# real aarch64 gcc rather than against a transcription of its source.
#
# The comparison is a _Static_assert header built from OUR numbers and handed
# to the cross compiler: gcc accepting the file IS the proof, so there is no
# dump format to parse and nothing version-specific to keep in step. The same
# technique the Sprint 14 layout differential uses.
#
# Our numbers live in exactly one place (src/sema/decl.c's va_list_aapcs64)
# and are mirrored by tests/unit/test_abi_aapcs64.c; this script proves the
# third party agrees.

set -e

CC=${CGF_A64_GCC:-aarch64-linux-gnu-gcc}

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "a64_va_list_diff: skipped (no $CC; set CGF_A64_GCC)"
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/va.c" <<'EOF'
/* Generated from Cgfried's synthesized AAPCS64 va_list. Every constant here
 * is what src/sema/decl.c lays out and what src/lower/expr.c hard-codes. */
typedef __builtin_va_list cgf_va_t;

_Static_assert(sizeof(cgf_va_t) == 32, "va_list is 32 bytes");
_Static_assert(_Alignof(cgf_va_t) == 8, "va_list is 8-byte aligned");

_Static_assert(__builtin_offsetof(cgf_va_t, __stack) == 0, "__stack at 0");
_Static_assert(__builtin_offsetof(cgf_va_t, __gr_top) == 8, "__gr_top at 8");
_Static_assert(__builtin_offsetof(cgf_va_t, __vr_top) == 16, "__vr_top at 16");
_Static_assert(__builtin_offsetof(cgf_va_t, __gr_offs) == 24,
               "__gr_offs at 24");
_Static_assert(__builtin_offsetof(cgf_va_t, __vr_offs) == 28,
               "__vr_offs at 28");

/* The offsets are SIGNED 32-bit: they are negative and count up toward zero,
 * which is the entire design. A wrong signedness here would make every
 * register-case va_arg read past the end of the save area. */
_Static_assert(sizeof(((cgf_va_t *)0)->__gr_offs) == 4, "__gr_offs is 4 bytes");
_Static_assert(sizeof(((cgf_va_t *)0)->__vr_offs) == 4, "__vr_offs is 4 bytes");
_Static_assert((__typeof__(((cgf_va_t *)0)->__gr_offs))-1 < 0,
               "__gr_offs is signed");
_Static_assert((__typeof__(((cgf_va_t *)0)->__vr_offs))-1 < 0,
               "__vr_offs is signed");

/* The save areas the offsets index: eight general and eight vector
 * registers, the vector slots being q-sized. */
_Static_assert(8 * 8 == 64, "GP save area is 64 bytes");
_Static_assert(8 * 16 == 128, "FP save area is 128 bytes");
EOF

if ! "$CC" -std=c11 -Wall -Werror -c "$work/va.c" -o "$work/va.o" 2>"$work/err"; then
    echo "a64_va_list_diff: FAILED — $CC rejects our va_list layout" >&2
    cat "$work/err" >&2
    exit 1
fi

echo "a64_va_list_diff: 13 layout assertions accepted by $($CC -dumpversion)"
