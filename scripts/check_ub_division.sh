#!/bin/sh
# Sprint 47: keep cross-architecture runtime/OPT_EQ fixtures free of C
# division UB whose hardware symptoms differ between x86-64 and AArch64.
set -eu
LC_ALL=C
export LC_ALL

usage()
{
    echo "usage: $0 [--self-test] [file-or-directory ...]" >&2
    exit 2
}

lint_one()
{
    file=$1
    # Only executable-result fixtures are in scope.  Compile-error tests may
    # intentionally contain these expressions and are not differential
    # execution oracles.
    if ! grep -Eq '^// (OPT_EQ|CHECK|EXIT_CODE):' "$file"; then
        return 0
    fi

    clean="$work/clean.$seq"
    seq=$((seq + 1))
    # Erase comments, strings, and character constants while preserving line
    # breaks and operators.  This prevents examples/directives from becoming
    # findings and gives diagnostics the original source line number.
    awk '
    BEGIN { block = 0; quote = 0; esc = 0 }
    {
        out = ""
        for (i = 1; i <= length($0); i++) {
            c = substr($0, i, 1)
            n = (i < length($0)) ? substr($0, i + 1, 1) : ""
            if (block) {
                if (c == "*" && n == "/") { block = 0; i++ }
                out = out " "
            } else if (quote) {
                if (esc) esc = 0
                else if (c == "\\") esc = 1
                else if (c == quote) quote = 0
                out = out " "
            } else if (c == "/" && n == "*") {
                block = 1; i++; out = out "  "
            } else if (c == "/" && n == "/") {
                break
            } else if (c == "\"" || c == "\047") {
                quote = c; out = out " "
            } else out = out c
        }
        print out
    }' "$file" >"$clean"

    bad=0
    # Integer zero only: the token boundary deliberately excludes 0.0/0.0,
    # which is defined IEEE floating-point behavior used to construct NaNs.
    if grep -En '[/%][[:space:]]*\(*\+?(0[xX]0+|0+)[uUlL]*\)*([^[:alnum:]_.]|$)' \
        "$clean" >"$work/hit.$seq"; then
        echo "ub-division FAIL: $file: literal integer zero divisor" >&2
        cat "$work/hit.$seq" >&2
        bad=1
    fi
    if grep -En '(INT_MIN|LONG_MIN|LLONG_MIN|INT32_MIN|INT64_MIN)[[:space:]]*[/%][[:space:]]*\(*-[[:space:]]*1([lL]*)\)*([^[:alnum:]_]|$)' \
        "$clean" >"$work/hit.$seq"; then
        echo "ub-division FAIL: $file: signed minimum divided by -1" >&2
        cat "$work/hit.$seq" >&2
        bad=1
    fi
    if ! awk -f tests/lint/ub-division/constprop.awk "$clean" \
        >"$work/flow.$seq"; then
        echo "ub-division FAIL: $file: propagated division UB" >&2
        cat "$work/flow.$seq" >&2
        bad=1
    fi
    return "$bad"
}

self_test()
{
    fixtures=tests/lint/ub-division
    for f in "$fixtures"/bad-*.c; do
        if lint_one "$f" 2>/dev/null; then
            echo "ub-division self-test FAIL: planted violation passed: $f" >&2
            return 1
        fi
    done
    for f in "$fixtures"/good-*.c; do
        if ! lint_one "$f"; then
            echo "ub-division self-test FAIL: legal fixture rejected: $f" >&2
            return 1
        fi
    done
    echo "ub-division lint: planted violations rejected; legal controls accepted"
}

mode=scan
if [ "${1:-}" = "--self-test" ]; then
    mode=self
    shift
elif [ "${1:-}" = "--help" ]; then
    usage
fi

work=${CGF_UB_DIV_WORK:-build/ub-division-lint}
mkdir -p "$work"
seq=0

if [ "$mode" = self ]; then
    self_test
fi

if [ "$#" -eq 0 ]; then
    set -- tests/corpus tests/programs/lower-exec
fi

files="$work/files"
: >"$files"
for path in "$@"; do
    if [ -d "$path" ]; then
        find "$path" -type f -name '*.c' -print
    elif [ -f "$path" ]; then
        printf '%s\n' "$path"
    else
        echo "ub-division lint: input not found: $path" >&2
        exit 2
    fi
done | sort -u >"$files"

n=0
fails=0
while IFS= read -r file; do
    n=$((n + 1))
    lint_one "$file" || fails=$((fails + 1))
done <"$files"

[ "$n" -gt 0 ] || {
    echo "ub-division lint: no C fixtures found" >&2
    exit 1
}
[ "$fails" -eq 0 ] || exit 1
echo "ub-division lint: $n fixtures checked; no architecture-dependent division UB"
