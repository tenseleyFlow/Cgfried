#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sample=$root/scripts/audit-sample.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-audit-sample-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

expect_failure()
{
    label=$1
    pattern=$2
    shift 2
    if "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"; then
        echo "audit-sample-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    grep -Fq "$pattern" "$tmp/$label.err" || {
        cat "$tmp/$label.out" "$tmp/$label.err" >&2
        exit 1
    }
}

# Deliberately reverse and duplicate the input. The selected positions below
# are therefore evidence that the script sorts and deduplicates before using
# the seed, rather than sampling input order.
: >"$tmp/ids"
i=37
while [ "$i" -ge 1 ]; do
    printf 'F%02d-H-%02d\n' "$i" "$i" >>"$tmp/ids"
    [ "$i" -eq 12 ] && printf 'F12-H-12\n' >>"$tmp/ids"
    i=$((i - 1))
done

"$sample" 2 "$tmp/ids" >"$tmp/first"
"$sample" 2 "$tmp/ids" >"$tmp/second"
cmp "$tmp/first" "$tmp/second"
cat >"$tmp/want" <<'EOF'
F03-H-03
F13-H-13
F23-H-23
F33-H-33
F06-H-06
EOF
cmp "$tmp/want" "$tmp/first"
[ "$(wc -l <"$tmp/first" | tr -d ' ')" -eq 5 ]
[ "$(sort -u "$tmp/first" | wc -l | tr -d ' ')" -eq 5 ]

# Sixty distinct IDs require six samples, proving the percentage is rounded
# up rather than truncated or treated as exactly five.
: >"$tmp/sixty"
i=1
while [ "$i" -le 60 ]; do
    printf 'G%02d-M-%02d\n' "$i" "$i" >>"$tmp/sixty"
    i=$((i + 1))
done
[ "$("$sample" 0 "$tmp/sixty" | wc -l | tr -d ' ')" -eq 6 ]

# A short pool makes a stride of ten revisit positions. The deterministic
# collision fallback must still return five distinct IDs.
head -n 20 "$tmp/sixty" >"$tmp/twenty"
"$sample" 0 "$tmp/twenty" >"$tmp/collision"
[ "$(wc -l <"$tmp/collision" | tr -d ' ')" -eq 5 ]
[ "$(sort -u "$tmp/collision" | wc -l | tr -d ' ')" -eq 5 ]

printf 'FE-H-01\nnot-an-id\n' >"$tmp/bad"
expect_failure malformed 'malformed finding ID: not-an-id' \
    "$sample" 0 "$tmp/bad"
printf 'FE-H-01\nIR-C-01\nOPT-H-01\nX64-C-01\n' >"$tmp/four"
expect_failure minimum 'need at least 5 distinct finding IDs (got 4)' \
    "$sample" 0 "$tmp/four"
expect_failure seed 'seed must be a non-negative integer' \
    "$sample" nope "$tmp/ids"

echo 'audit-sample-meta: PASS'
