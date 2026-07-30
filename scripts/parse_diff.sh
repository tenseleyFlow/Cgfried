#!/bin/sh
# Accept/reject differential vs gcc over the declaration corpus. We cannot
# codegen yet, so the comparison is the ACCEPT/REJECT verdict of
# `cgf -fsyntax-only` against `gcc -fsyntax-only` — the only thing both
# can answer today. Disagreements print both verdicts and fail.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: parse_diff.sh path/to/cgfried [dir]}
DIR=${2:-tests/fixtures/parse}
GCC=${CGF_DIFF_GCC:-gcc}

command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=parsediff test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}

agree=0
disagree=0
for f in "$DIR"/accept/*.c "$DIR"/reject/*.c; do
    [ -f "$f" ] || continue
    ours=0
    theirs=0
    "$CGF" -fsyntax-only -std=c17 "$f" >/dev/null 2>&1 || ours=1
    "$GCC" -fsyntax-only -std=c17 -w "$f" >/dev/null 2>&1 || theirs=1
    if [ "$ours" = "$theirs" ]; then
        agree=$((agree + 1))
    else
        echo "parse_diff: DISAGREE $f (cgf $([ $ours = 0 ] && echo accept || echo reject), gcc $([ $theirs = 0 ] && echo accept || echo reject))" >&2
        disagree=$((disagree + 1))
    fi
done

total=$((agree + disagree))
[ "$total" -gt 0 ] || { echo "parse_diff: no fixtures found" >&2; exit 1; }
echo "parse_diff: $agree/$total agree with gcc on accept/reject"
[ "$disagree" -eq 0 ]
