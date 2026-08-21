#!/bin/sh
# Select the Sprint 61 re-audit sample from a recorded numeric seed.
# Input is a whitespace-separated list of finding IDs, from FILE or stdin.
set -eu

LC_ALL=C
export LC_ALL

if [ "$#" -gt 2 ] || [ "$#" -lt 1 ]; then
    echo "usage: audit-sample.sh SEED [ID-FILE]" >&2
    exit 2
fi

seed=$1
input=${2:--}
case "$seed" in
''|*[!0-9]*)
    echo "audit-sample: seed must be a non-negative integer: $seed" >&2
    exit 2
    ;;
esac

if [ "$input" != - ] && [ ! -r "$input" ]; then
    echo "audit-sample: cannot read ID file: $input" >&2
    exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-audit-sample.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

if [ "$input" = - ]; then
    awk '{ for (i = 1; i <= NF; i++) print $i }' >"$work/raw"
else
    awk '{ for (i = 1; i <= NF; i++) print $i }' "$input" >"$work/raw"
fi

invalid=$(awk '!/^[A-Z][A-Z0-9]*-[CHML]-[0-9][0-9]$/ { print; exit }' \
    "$work/raw")
if [ -n "$invalid" ]; then
    echo "audit-sample: malformed finding ID: $invalid" >&2
    exit 2
fi

sort -u "$work/raw" >"$work/ids"
count=$(wc -l <"$work/ids" | tr -d ' ')
if [ "$count" -lt 5 ]; then
    echo "audit-sample: need at least 5 distinct finding IDs (got $count)" >&2
    exit 2
fi

# The seed chooses the first zero-based position in the sorted ID list. Each
# next choice advances ten positions and wraps. If a short list makes that
# revisit an ID, advance to the next unselected ID so the minimum-five rule
# remains satisfiable without weakening the every-tenth rule for normal pools.
awk -v seed="$seed" '
    { id[NR - 1] = $0 }
    END {
        n = NR
        wanted = int((n + 9) / 10)
        if (wanted < 5)
            wanted = 5
        pos = seed % n
        for (picked = 0; picked < wanted; picked++) {
            while (seen[pos])
                pos = (pos + 1) % n
            print id[pos]
            seen[pos] = 1
            pos = (pos + 10) % n
        }
    }
' "$work/ids"
