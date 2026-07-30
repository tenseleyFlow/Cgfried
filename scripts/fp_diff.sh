#!/bin/sh
# The float engine's differential: our correctly-rounded conversion vs
# gcc's, for a corpus of literals.
#
# Neither side parses the other's output format. We print bit patterns
# from softfp; gcc compiles the SAME literal into a static initializer,
# and a tiny host program reads the bytes back. Agreement across both
# binary32 and binary64 for every literal is the proof.
#
# The oracle uses the host FPU — that is exactly right, and exactly why
# the compiler must not: the whole point is that our host-free conversion
# lands on the same bits the host would.
set -eu
LC_ALL=C
export LC_ALL

FPDIFF=${1:-build/fpdiff}
CORPUS=${2:-tests/fixtures/fp-literals.txt}
GCC=${CGF_DIFF_GCC:-gcc}
WORK=${CGF_FP_WORK:-build/fp-diff}

command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=fpdiff test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}
[ -x "$FPDIFF" ] || {
    echo 'HARNESS_SKIP suite=fpdiff test=printer count=1 reason="fpdiff not built"'
    exit 0
}
[ -f "$CORPUS" ] || { echo "fp_diff: missing corpus $CORPUS" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK"

# Build the oracle: one static float and one static double per literal,
# printed as hex from their object representation.
{
    echo '#include <stdio.h>'
    echo '#include <string.h>'
    echo 'int main(void){'
    echo '  unsigned int u32; unsigned long long u64;'
    n=0
    while IFS= read -r lit; do
        case $lit in ''|\#*) continue ;; esac
        # `(float)` rather than an `f` suffix: a literal with no `.` or
        # exponent is an INTEGER constant, and `9007199254740993f` is a
        # syntax error. The cast converts the double value once, which is
        # the same rounding the suffix would have given.
        echo "  { volatile float f = (float)(${lit}); volatile double d = ${lit};"
        echo "    memcpy(&u32,(void*)&f,4); memcpy(&u64,(void*)&d,8);"
        # printf, not echo: dash's builtin echo interprets backslash
        # escapes, so the `\n` inside this C string literal became a real
        # newline on CI and the generated file would not compile. Local
        # bash does not, which is exactly why it passed here first.
        printf '    printf("%%s %%08X %%016llX\\n", "%s", u32, u64); }\n' \
            "$lit"
        n=$((n + 1))
    done < "$CORPUS"
    echo '  return 0; }'
} > "$WORK/oracle.c"

"$GCC" -std=c11 -w -o "$WORK/oracle" "$WORK/oracle.c" 2>"$WORK/oracle.err" || {
    echo "fp_diff: the oracle would not build" >&2
    head -5 "$WORK/oracle.err" >&2
    exit 1
}
"$WORK/oracle" > "$WORK/theirs.txt"

# Ours, from the same corpus.
: > "$WORK/ours.txt"
while IFS= read -r lit; do
    case $lit in ''|\#*) continue ;; esac
    "$FPDIFF" "$lit" >> "$WORK/ours.txt"
done < "$CORPUS"

agree=0
disagree=0
while IFS= read -r line; do
    lit=${line%% *}
    ours=${line#* }
    # Match the whole FIRST FIELD, not a substring: `1e-308` occurs
    # inside `2.2250738585072011e-308`, and a substring match silently
    # compared one literal against another's answer.
    theirs=$(awk -v want="$lit" '$1 == want { print $2 " " $3; exit }' \
        "$WORK/theirs.txt")
    if [ "$ours" = "$theirs" ]; then
        agree=$((agree + 1))
    else
        echo "fp_diff: DISAGREE $lit" >&2
        echo "    ours   (b32 b64): $ours" >&2
        echo "    gcc    (b32 b64): $theirs" >&2
        disagree=$((disagree + 1))
    fi
done < "$WORK/ours.txt"

total=$((agree + disagree))
[ "$total" -gt 0 ] || { echo "fp_diff: empty corpus" >&2; exit 1; }
echo "fp_diff: $agree/$total literals convert bit-identically to gcc (binary32 + binary64)"
[ "$disagree" -eq 0 ]
