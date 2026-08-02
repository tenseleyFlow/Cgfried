#!/bin/sh
# Warning-policy differential: compare exit status and warning count, not
# compiler-specific text. GCC 8 is opt-in because modern hosts rarely ship it.
set -eu
LC_ALL=C
SOURCE_DATE_EPOCH=0
export LC_ALL SOURCE_DATE_EPOCH

CGF=${1:?usage: warn_diff.sh path/to/cgfried}
GCC8=${CGF_DIFF_GCC8:-gcc-8}
WORK=${CGF_WARN_DIFF_WORK:-build/warn-diff}

command -v "$GCC8" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=warndiff test=gcc8-oracle count=1 reason="gcc-8 not found (set CGF_DIFF_GCC8)"'
    exit 0
}

rm -rf "$WORK"
mkdir -p "$WORK"
agree=0
disagree=0

check() {
    file=$1
    name=$(basename "$file" .c)
    flags=$(sed -n 's@^// FLAGS: @@p' "$file" | sed -n '1p')
    cgf_err="$WORK/$name.cgf.err"
    gcc_err="$WORK/$name.gcc.err"
    if "$CGF" $flags "$file" >/dev/null 2>"$cgf_err"; then
        cgf_status=0
    else
        cgf_status=$?
    fi
    if "$GCC8" $flags "$file" >/dev/null 2>"$gcc_err"; then
        gcc_status=0
    else
        gcc_status=$?
    fi
    cgf_count=$(grep -c ': warning:' "$cgf_err" || true)
    gcc_count=$(grep -c ': warning:' "$gcc_err" || true)
    if [ "$cgf_status" -eq "$gcc_status" ] &&
       [ "$cgf_count" -eq "$gcc_count" ]; then
        agree=$((agree + 1))
    else
        echo "warn_diff: DISAGREE $name: ours=$cgf_status/$cgf_count gcc8=$gcc_status/$gcc_count" >&2
        disagree=$((disagree + 1))
    fi
}

for file in \
    tests/warn/pragma/cpp.c \
    tests/warn/pragma/malformed.c \
    tests/warn/pragma/unknown.c \
    tests/warn/pragma/ignored.c \
    tests/warn/pragma/warning_restore.c \
    tests/warn/pragma/error.c \
    tests/warn/pragma/push_pop.c \
    tests/warn/pragma/nested_push_pop.c \
    tests/warn/pragma/pedantic_w.c \
    tests/warn/pragma/mid_function.c \
    tests/warn/pragma/skipped_region.c \
    tests/warn/pragma/pragma_macro_ignored.c \
    tests/warn/pragma/pragma_macro_warning.c \
    tests/warn/pragma/macro_definition_state.c \
    tests/warn/pragma/macro_expansion_state.c \
    tests/warn/pragma/error_implies_enable.c \
    tests/warn/pragma/global_demote.c \
    tests/warn/pragma/system_suppressed.c \
    tests/warn/pragma/system_enabled.c \
    tests/warn/pragma/system_pragma_threshold.c \
    tests/warn/pragma/unbalanced_pop.c
do
    check "$file"
done

total=$((agree + disagree))
echo "warn_diff: $agree/$total exit/count classifications match GCC 8"
[ "$disagree" -eq 0 ]
