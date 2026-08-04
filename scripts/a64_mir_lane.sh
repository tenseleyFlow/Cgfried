#!/bin/sh
set -eu

tool=${1:-build/a64mir}
work=${CGF_A64_MIR_WORK:-build/a64-mir-lane}

mkdir -p "$work"
count=0
for input in tests/mir/arm64/*.cgfir; do
    name=${input##*/}
    name=${name%.cgfir}
    mode=
    if [ "$name" = reg31 ]; then
        mode=--reg31
    fi
    "$tool" $mode "$input" >"$work/$name.first"
    "$tool" $mode "$input" >"$work/$name.second"
    cmp "$work/$name.first" "$work/$name.second"
    cmp "$work/$name.first" "tests/mir/arm64/$name.expected"
    count=$((count + 1))
done

test "$count" -ge 8
patterns=$(grep -h '^// PATTERN:' tests/mir/arm64/*.cgfir | wc -l | tr -d ' ')
test "$patterns" -ge 40
large=tests/mir/arm64/large-bulk.reject
if "$tool" "$large" >"$work/large-bulk.out" 2>"$work/large-bulk.err"; then
    echo "a64_mir_lane: oversized bulk operation unexpectedly selected" >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 4
grep -F "memcpy/memset larger than 65536 bytes lands in Sprint 49" \
    "$work/large-bulk.err" >/dev/null
echo "a64_mir_lane: $count golden modules, $patterns patterns deterministic and exact; oversized bulk expansion bounded"
