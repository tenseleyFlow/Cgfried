#!/bin/sh
# src/sema/ must never ask the HOST what the target looks like.
#
# `char` is signed on x86_64 and UNSIGNED on arm64-linux; integer widths,
# alignment and endianness are all per-target too. A conditional compiled
# against the host silently produces a compiler that is correct only when
# host == target, and cross-compilation miscompiles with no diagnostic
# anywhere. Everything must route through the TargetSpec parameter.
set -eu
LC_ALL=C
export LC_ALL

status=0

hits=$(grep -rnE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)' src/sema/ |
    grep -vE 'CGF_[A-Z_]*_H' || true)
if [ -n "$hits" ]; then
    echo "check_sema_target: conditional compilation in src/sema/:" >&2
    printf '%s\n' "$hits" >&2
    echo "  Target properties come from the TargetSpec parameter, never" >&2
    echo "  from the host — see src/sema/conv.c's header comment." >&2
    status=1
fi

# The same reasoning bans asking the host's own types for a size. Comments
# are stripped first — `sizeof(int[f()])` legitimately appears in PROSE
# about C semantics, and the gate is after code, not documentation.
hits=""
for f in src/sema/*.c src/sema/*.h; do
    h=$(sed 's://.*::' "$f" |
        awk '/\/\*/{inc=1} {if(!inc) print NR": "$0} /\*\//{inc=0}' |
        grep -E '\bsizeof[[:space:]]*\([[:space:]]*(int|long|short|char|float|double|void[[:space:]]*\*)' |
        sed "s:^:$f\::" || true)
    [ -n "$h" ] && hits="$hits$h
"
done
if [ -n "$hits" ]; then
    echo "check_sema_target: host sizeof() in src/sema/:" >&2
    printf '%s' "$hits" >&2
    status=1
fi

[ "$status" -eq 0 ] && echo "check_sema_target: clean (no host assumptions)"
exit $status
