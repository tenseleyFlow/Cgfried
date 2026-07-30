#!/bin/sh
# TinyCC preprocessor conformance corpus (tests/tinycc-pp, imported with
# attribution). Token-insensitive compare of `cgf -E NN.c` vs NN.expect.
# Known failures live in tests/tinycc-pp/xfail.txt (one name + reason per
# line) — an xfailed test that PASSES is an error (XPASS discipline).
set -eu
LC_ALL=C
export LC_ALL
CGF=${1:?usage: tinycc_pp_smoke.sh path/to/cgfried}
DIR=tests/tinycc-pp
norm() { printf '%s' "$1" | tr -s ' \t\n' ' ' | sed 's/^ //;s/ $//'; }
pass=0; fail=0; xfail=0; xpass=0; failed=""
for f in "$DIR"/*.c; do
    base=$(basename "$f" .c)
    exp="$DIR/$base.expect"
    [ -f "$exp" ] || continue
    is_xfail=0
    grep -q "^$base\\b" "$DIR/xfail.txt" 2>/dev/null && is_xfail=1
    ours=$("$CGF" -E "$f" 2>/dev/null) || ours="<<ERROR>>"
    a=$(norm "$ours"); b=$(norm "$(cat "$exp")")
    if [ "$a" = "$b" ]; then
        if [ "$is_xfail" = 1 ]; then
            echo "tinycc-pp: XPASS $base (remove from xfail.txt)" >&2
            xpass=$((xpass + 1))
        else
            pass=$((pass + 1))
        fi
    else
        if [ "$is_xfail" = 1 ]; then
            xfail=$((xfail + 1))
        else
            echo "tinycc-pp: FAIL $base" >&2
            failed="$failed $base"
            fail=$((fail + 1))
        fi
    fi
done
total=$((pass + fail + xfail + xpass))
echo "tinycc_pp: $pass/$total pass, $fail fail, $xfail xfail, $xpass xpass"
[ "$fail" -eq 0 ] && [ "$xpass" -eq 0 ]
