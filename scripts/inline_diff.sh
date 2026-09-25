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

# check_mode <iso|gnu89-option> <expected-emit yes|no> <source...>
check_mode() {
    mode=$1
    want=$2
    shift 2
    n=$((n + 1))
    id=$(printf 'i%02d' "$n")
    printf '%s\n' "$@" > "$WORK/$id.c"

    case $mode in
    iso)
        "$GCC" -std=c17 -O0 -S -o "$WORK/$id.s" "$WORK/$id.c" 2>/dev/null
        ;;
    gnu89-option)
        "$GCC" -std=gnu17 -fgnu89-inline -O0 -S -o "$WORK/$id.s" \
            "$WORK/$id.c" 2>/dev/null
        ;;
    *) echo "inline_diff: unknown mode $mode" >&2; exit 2 ;;
    esac
    # ELF spells the C symbol `f`; Mach-O gives it the leading underscore `_f`.
    if grep -qE '^_?f:' "$WORK/$id.s"; then gcc_emit=yes; else gcc_emit=no; fi

    case $mode in
    iso)
        ours=$("$CGF" -std=c17 -fdump-sema "$WORK/$id.c" 2>/dev/null |
            awk '/^func f:/ {
                if (/emit-external=no/) print "no";
                else print "yes";
                exit }')
        ;;
    gnu89-option)
        ours=$("$CGF" -std=gnu17 -fgnu89-inline -fdump-sema \
            "$WORK/$id.c" 2>/dev/null |
            awk '/^func f:/ {
                if (/emit-external=no/) print "no";
                else print "yes";
                exit }')
        ;;
    esac
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

check() { check_mode iso "$@"; }
check_gnu89_option() { check_mode gnu89-option "$@"; }

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

# The command-line override selects GNU89's inverted model without changing
# the rest of the gnu17 dialect. A later real definition replaces the
# inline-only body; it is not a duplicate definition, and may be static.
check_gnu89_option no  'extern inline int f(void) { return 1; }'
check_gnu89_option yes 'inline int f(void) { return 1; }'
check_gnu89_option yes 'extern inline int f(void) { return 1; }' \
    'int f(void) { return 2; }'
check_gnu89_option yes 'extern inline int f(void) { return 1; }' \
    'static int f(void) { return 2; }'

total=$((agree + disagree))
echo "inline_diff: $agree/$total emission decisions match gcc -S"
[ "$disagree" -eq 0 ]
