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

if [ ! -d "$DIR" ]; then
    echo "HARNESS_SKIP suite=ctestsuite test=corpus count=1 reason=\"reference clone .docs/refs/c-testsuite absent\""
    exit 0
fi
command -v "$GCC" >/dev/null 2>&1 || {
    echo 'HARNESS_SKIP suite=ctestsuite test=gcc-oracle count=1 reason="gcc not found"'
    exit 0
}

expected=$(grep -v '^#' "$LEDGER" 2>/dev/null | grep -v '^[[:space:]]*$' |
    awk '{print $1}' | sort || true)

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
    "$CGF" -fsyntax-only -std=c17 "$f" >/dev/null 2>&1 || ours=1
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
            xfail=$((xfail + 1))
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
