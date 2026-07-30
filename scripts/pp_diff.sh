#!/bin/sh
# Differential: cgf -E vs gcc -E -P over directive-free fixtures, compared
# token-insensitively (all whitespace runs collapse to one space; our -E
# spacing is SPACE/BOL-faithful, not layout-faithful — Sprint 6 owns exact
# line fidelity). Precursor of the Sprint 6 harness.
set -eu
LC_ALL=C
export LC_ALL

CGF=${1:?usage: pp_diff.sh path/to/cgfried fixtures-dir}
DIR=${2:?usage: pp_diff.sh path/to/cgfried fixtures-dir}

command -v gcc >/dev/null 2>&1 || {
    echo "pp_diff: gcc is required as the differential oracle" >&2
    exit 1
}

norm() {
    printf '%s' "$1" | tr -s ' \t\n' ' ' | sed 's/^ //;s/ $//'
}

status=0
for f in "$DIR"/*.c; do
    ours=$("$CGF" -E "$f")
    theirs=$(gcc -E -P "$f")
    a=$(norm "$ours")
    b=$(norm "$theirs")
    if [ "$a" != "$b" ]; then
        echo "pp_diff: token stream differs for $f" >&2
        echo "  cgf: $a" >&2
        echo "  gcc: $b" >&2
        status=1
    fi
done

[ "$status" -eq 0 ] && echo "pp_diff: all fixtures match gcc"
exit "$status"
