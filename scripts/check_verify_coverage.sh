#!/bin/sh
# Every verifier check number must have a bad/ fixture that pins it
# firing — a verifier check without a firing fixture is a check that
# doesn't exist. Enumerates verr(v, N) sites vs tests/programs/ir/bad/.
set -eu
LC_ALL=C
export LC_ALL

checks=$(grep -o 'verr(v, [0-9]*' src/ir/verify.c | grep -o '[0-9]*' |
    sort -n -u)
missing=0
for n in $checks; do
    if ! grep -rlq "ir verify \[$n\]" tests/programs/ir/bad/; then
        # check 10 is text-unreachable (no reserved spelling parses);
        # its firing fixture lives in the unit suite by necessity.
        [ "$n" = 10 ] && continue
        echo "check_verify_coverage: check $n has no bad/ fixture" >&2
        missing=1
    fi
done
[ "$missing" = 0 ] || exit 1
echo "check_verify_coverage: every check has a firing fixture"
