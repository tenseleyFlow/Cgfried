#!/bin/sh
# The compile-time float engine must never touch the host FPU.
#
# Two independent reasons. DETERMINISM: a host `double` puts the host's
# rounding — and on x86 possibly the x87 unit's 80-bit intermediates —
# into our output, which breaks the byte-identical bootstrap. CROSS
# -COMPILATION: folding an arm64 fp128 constant on an x86 host must not
# round through the host's 64-bit double, because rounding twice is not
# the same as rounding once.
#
# So `float` and `double` may appear in these files only inside comments.
set -eu
LC_ALL=C
export LC_ALL

status=0
for f in src/util/softfp.c src/util/softfp.h src/util/bigint.c \
         src/util/bigint.h src/sema/constexpr.c src/opt/simplify.c; do
    [ -f "$f" ] || continue
    # Strip block and line comments, then look for the type keywords.
    hits=$(sed 's://.*::' "$f" |
        awk '/\/\*/{inc=1} {if(!inc) print} /\*\//{inc=0}' |
        grep -nE '\b(float|double|long double)\b' || true)
    if [ -n "$hits" ]; then
        echo "check_no_host_fpu: host floating-point type in $f:" >&2
        printf '%s\n' "$hits" >&2
        status=1
    fi
done

[ "$status" -eq 0 ] && echo "check_no_host_fpu: clean (no host FPU in the constant engine)"
exit $status
