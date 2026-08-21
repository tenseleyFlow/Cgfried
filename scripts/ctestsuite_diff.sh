#!/bin/sh
# Accept/reject differential over the c-testsuite single-exec corpus (220
# real programs) vs gcc. These are whole programs, not fixtures, so they
# exercise combinations no hand-written corpus reaches.
#
# The corpus lives in .docs/refs/, which is a set of reference CLONES and
# is not committed — so this harness must skip LOUDLY rather than silently
# pass when the clone is absent (CI has no refs).
#
# Disagreements are pinned in a ledger. A file listed there is expected to
# disagree and cites the reason; a NEW disagreement fails, and so does a
# ledger entry that starts agreeing (the XPASS-is-failure rule). That is
# what turns a known deferral into regression protection instead of a
# blanket allowance.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: ctestsuite_diff.sh path/to/cgfried}
DIR=${2:-.docs/refs/c-testsuite/tests/single-exec}
LEDGER=${3:-tests/fixtures/ctestsuite-parse-ledger.txt}
GCC=${CGF_DIFF_GCC:-gcc}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cgf-ctestsuite-diff.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

if [ ! -d "$DIR" ]; then
    echo "HARNESS_SKIP suite=ctestsuite test=corpus count=1 reason=\"reference clone .docs/refs/c-testsuite absent\""
    exit 0
fi
command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=ctestsuite test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}

# TI-M-03: filename-only membership allowed an unrelated failure to inherit a
# stale waiver. Require one sorted, unique, tab-separated row whose literal
# diagnostic fingerprint still explains each known disagreement.
if ! awk -F '\t' '
    /^#/ || /^[[:space:]]*$/ { next }
    NF != 4 || $1 !~ /^[0-9][0-9][0-9][0-9][0-9]\.c$/ ||
        $2 !~ /^XD-[A-Z0-9-]+$/ || $3 == "" || $4 == "" { exit 1 }
    previous != "" && $1 <= previous { exit 1 }
    { previous = $1 }
' "$LEDGER"; then
    echo "ctestsuite_diff: malformed, unsorted, or duplicate ledger: $LEDGER" >&2
    exit 1
fi

expected=$(awk -F '\t' '!/^#/ && !/^[[:space:]]*$/ { print $1 }' "$LEDGER")

# A hang must read as a hang. Without a cap, an infinite loop in the parser
# shows up as the shell reporting "Killed" (the OOM killer arriving first)
# with the counts still matching — which is how the 00210.c hang nearly
# went unnoticed. `timeout` is not POSIX, so degrade gracefully.
if command -v timeout >/dev/null 2>&1; then
    CAP="timeout 20"
else
    CAP=""
fi

agree=0
newdiff=0
xfail=0
xpass=0
actual=''

for f in "$DIR"/*.c; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    ours=0
    theirs=0
    cgf_err="$WORK/$base.cgf.err"
    $CAP "$CGF" -fsyntax-only -std=c17 "$f" >/dev/null 2>"$cgf_err" || ours=$?
    if [ "$ours" -gt 1 ]; then
        echo "ctestsuite_diff: ABNORMAL exit $ours on $base (124 = hang)" >&2
        newdiff=$((newdiff + 1))
        continue
    fi
    "$GCC" -fsyntax-only -std=c17 -w "$f" >/dev/null 2>&1 || theirs=1

    listed=0
    for e in $expected; do
        [ "$e" = "$base" ] && listed=1 && break
    done

    if [ "$ours" = "$theirs" ]; then
        if [ "$listed" = 1 ]; then
            echo "ctestsuite_diff: XPASS $base now agrees with gcc — remove it from $LEDGER" >&2
            xpass=$((xpass + 1))
        else
            agree=$((agree + 1))
        fi
    else
        if [ "$listed" = 1 ]; then
            debt_id=$(awk -F '\t' -v file="$base" '$1 == file { print $2; exit }' "$LEDGER")
            fingerprint=$(awk -F '\t' -v file="$base" '$1 == file { print $3; exit }' "$LEDGER")
            if grep -Fq -- "$fingerprint" "$cgf_err"; then
                xfail=$((xfail + 1))
            else
                echo "ctestsuite_diff: DIAGNOSTIC DRIFT $base ($debt_id no longer matches: $fingerprint)" >&2
                newdiff=$((newdiff + 1))
            fi
        else
            echo "ctestsuite_diff: NEW DISAGREEMENT $base (cgf $([ $ours = 0 ] && echo accept || echo reject), gcc $([ $theirs = 0 ] && echo accept || echo reject))" >&2
            "$CGF" -fsyntax-only -std=c17 "$f" 2>&1 >/dev/null | grep -m1 'error:' >&2 || true
            newdiff=$((newdiff + 1))
        fi
        actual="$actual $base"
    fi
done

total=$((agree + newdiff + xfail + xpass))
[ "$total" -gt 0 ] || { echo "ctestsuite_diff: no files found in $DIR" >&2; exit 1; }
echo "ctestsuite_diff: $total files, $agree agree, $xfail known-deferred, $newdiff new, $xpass xpass"
[ "$newdiff" -eq 0 ] && [ "$xpass" -eq 0 ]
