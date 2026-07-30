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
    # The two corpora ask gcc different questions, and the asymmetry is the
    # point. ACCEPT files must survive gcc's DEFAULT flags — several are
    # legal C that gcc merely pedwarns about (`int a[] = {};`), and
    # -pedantic-errors would turn those into false disagreements. REJECT
    # files are constraint violations, where the standard demands only "a
    # diagnostic" and gcc is free to pick warning or error: gcc <= 13 warns
    # on an identifier list in a declaration (6.7.6.3p3) while gcc >= 14
    # errors. -pedantic-errors pins that to error on every version, so this
    # harness does not silently depend on the runner's gcc.
    case $f in
    */reject/*) gccflags='-pedantic-errors' ;;
    *) gccflags='-w' ;;
    esac
    "$CGF" -fsyntax-only -std=c17 "$f" >/dev/null 2>&1 || ours=1
    "$GCC" -fsyntax-only -std=c17 $gccflags "$f" >/dev/null 2>&1 || theirs=1
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
