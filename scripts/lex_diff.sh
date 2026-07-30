#!/bin/sh
# Integer-ladder differential vs gcc. We cannot codegen yet, so the oracle
# is gcc's own type classification asserted with _Static_assert: if gcc
# accepts the file, gcc agrees with the type WE recorded. The generator
# writes one assertion per (constant, expected-type) pair taken from our
# --dump-tokens output, so the two are compared by construction.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: lex_diff.sh path/to/cgfried}
GCC=${CGF_DIFF_GCC:-gcc}
WORK=$(dirname "$CGF")/lexdiff
mkdir -p "$WORK"

command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=lexdiff test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}

CONSTS='0 1 42 2147483647 2147483648 4294967295 4294967296
9223372036854775807 0x0 0x7fffffff 0x80000000 0xffffffff 0x100000000
0x7fffffffffffffff 0x8000000000000000 0xffffffffffffffff 1u 1U 1l 1L 1ul
1lu 1ULL 1ll 1LL 0777 0x10 2147483648u 4294967295u'

: > "$WORK/probe.c"
n=0
for c in $CONSTS; do
    printf '%s\n' "$c" > "$WORK/one.c"
    ours=$("$CGF" --dump-tokens "$WORK/one.c" 2>/dev/null || true)
    ty=$(printf '%s' "$ours" | sed -n 's/^INT_CONST [0-9]* //p' | head -1)
    [ -n "$ty" ] || { echo "lex_diff: no INT_CONST for $c" >&2; exit 1; }
    printf '_Static_assert(sizeof(%s) == sizeof(%s), "size %s");\n' \
        "$c" "$ty" "$c" >> "$WORK/probe.c"
    # Signedness: 0*X-1 has X's promoted type, and is < 0 only if that
    # type is SIGNED (unsigned wraps to its maximum). Comparing that
    # predicate for the constant and for our claimed type asks gcc
    # whether it agrees about signedness.
    printf '_Static_assert(((0*(%s)-1) < 0) == ((0*(%s)0-1) < 0), "sign %s");\n' \
        "$c" "$ty" "$c" >> "$WORK/probe.c"
    n=$((n + 1))
done

if "$GCC" -std=c17 -fsyntax-only "$WORK/probe.c" 2>"$WORK/err"; then
    echo "lex_diff: $n integer constants classified as gcc does"
else
    echo "lex_diff: gcc disagrees with our integer-constant types:" >&2
    head -20 "$WORK/err" >&2
    exit 1
fi
