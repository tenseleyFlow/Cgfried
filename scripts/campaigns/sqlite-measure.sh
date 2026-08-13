#!/bin/sh
set -eu

fail() {
    echo "sqlite-measure: $*" >&2
    exit 1
}

[ "$#" -ge 9 ] ||
    fail "usage: $0 TIMEIT RUNS WARMUP TIMEOUT RAW RECEIPT LOG -- COMMAND..."
timeit=$1
runs=$2
warmup=$3
timeout_seconds=$4
raw=$5
receipt=$6
log=$7
shift 7
[ "$1" = -- ] || fail "missing -- before command"
shift
[ "$#" -gt 0 ] || fail "missing measured command"

case $runs:$warmup:$timeout_seconds in
    *[!0-9:]* | :* | *:) fail "runs, warmup, and timeout must be integers" ;;
esac
[ "$runs" -ge 1 ] || fail "runs must be at least 1"
[ "$timeout_seconds" -ge 1 ] || fail "timeout must be at least 1 second"
[ -x "$timeit" ] || fail "timer is not executable: $timeit"

"$timeit" -n "$runs" -w "$warmup" -t "$timeout_seconds" -o "$raw" -- "$@" \
    >"$receipt" 2>"$log"
for key in wall_ms_median wall_ms_mad user_ms_median sys_ms_median maxrss_kb_max; do
    count=$(awk -F= -v key="$key" '$1 == key { n++ } END { print n + 0 }' "$receipt")
    [ "$count" -eq 1 ] || fail "receipt must contain exactly one $key: $receipt"
    value=$(awk -F= -v key="$key" '$1 == key { print substr($0, length(key) + 2) }' \
        "$receipt")
    printf '%s\n' "$value" | awk '
        /^[0-9]+([.][0-9]+)?$/ { ok = 1 }
        END { exit !ok }
    ' || fail "receipt has nonnumeric $key=$value"
done
awk -v runs="$runs" '
    BEGIN { numeric = "^[0-9]+([.][0-9]+)?$" }
    {
        if (NF != 5) exit 1
        split($1, sample, "=")
        split($2, wall, "=")
        split($3, user, "=")
        split($4, sys, "=")
        split($5, rss, "=")
        if (sample[1] != "sample" || sample[2] != NR ||
            wall[1] != "wall_ms" || wall[2] !~ numeric ||
            user[1] != "user_ms" || user[2] !~ numeric ||
            sys[1] != "sys_ms" || sys[2] !~ numeric ||
            rss[1] != "maxrss_kb" || rss[2] !~ /^[0-9]+$/)
            exit 1
    }
    END { exit NR != runs }
' "$raw" || fail "timer raw samples are malformed or incomplete: $raw"
