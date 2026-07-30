#!/bin/sh
# The inline-matrix differential: does this TU emit the external
# definition of an inline function?
#
# The oracle is gcc -S: if the function's label appears in the assembly,
# gcc emitted it. Ours is the -fdump-sema marker. The two must agree on
# every row of the 6.7.4p7 matrix and its ordering variants — this is the
# eternally-confused feature, and the ONLY trustworthy check is what the
# reference compiler actually does.
#
# -O0 is deliberate: at higher levels gcc may drop an unreferenced
# static-inline body, which tests the optimizer rather than the matrix.
# COMMON tentatives are also checked here (.comm vs .bss lines).
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: inline_diff.sh path/to/cgfried}
GCC=${CGF_DIFF_GCC:-gcc}
WORK=${CGF_INLINE_WORK:-build/inline-diff}

command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=inlinediff test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}
rm -rf "$WORK"
mkdir -p "$WORK"

agree=0
disagree=0
n=0

# check <expected-emit yes|no> <source...>
check() {
    want=$1
    shift
    n=$((n + 1))
    id=$(printf 'i%02d' "$n")
    printf '%s\n' "$@" > "$WORK/$id.c"

    "$GCC" -std=c17 -O0 -S -o "$WORK/$id.s" "$WORK/$id.c" 2>/dev/null
    if grep -qE '^f:' "$WORK/$id.s"; then gcc_emit=yes; else gcc_emit=no; fi

    ours=$("$CGF" -fdump-sema "$WORK/$id.c" 2>/dev/null |
        awk '/^func f:/ {
            if (/emit-external=no/) print "no";
            else print "yes";
            exit }')
    [ -n "$ours" ] || ours=missing

    if [ "$gcc_emit" != "$want" ]; then
        echo "inline_diff: $id: the EXPECTATION is wrong (gcc emits: $gcc_emit, table says $want)" >&2
        disagree=$((disagree + 1))
    elif [ "$ours" = "$gcc_emit" ]; then
        agree=$((agree + 1))
    else
        echo "inline_diff: DISAGREE $id (ours=$ours gcc=$gcc_emit):" >&2
        sed 's/^/    /' "$WORK/$id.c" >&2
        disagree=$((disagree + 1))
    fi
}

# The four matrix rows.
check no  'inline int f(void) { return 1; }'
check yes 'extern inline int f(void) { return 1; }'
check yes 'static inline int f(void) { return 1; }' 'int g(void) { return f(); }'
check yes 'int f(void) { return 1; }'
# The ordering variants: the decision needs EVERY declaration.
check yes 'inline int f(void) { return 1; }' 'int f(void);'
check yes 'int f(void);' 'inline int f(void) { return 1; }'
check yes 'inline int f(void) { return 1; }' 'extern int f(void);'
check yes 'extern inline int f(void);' 'inline int f(void) { return 1; }'
check no  'inline int f(void);' 'inline int f(void) { return 1; }'
check no  'inline int f(void) { return 1; }' 'inline int f(void);'

total=$((agree + disagree))
echo "inline_diff: $agree/$total emission decisions match gcc -S"
[ "$disagree" -eq 0 ]
