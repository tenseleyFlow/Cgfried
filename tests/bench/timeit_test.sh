#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
timeit=${1:-$repo/build/timeit}
tmp=${TMPDIR:-/tmp}/cgf-timeit-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp"

fail() {
    echo "timeit_test: $*" >&2
    exit 1
}

expect_status() {
    name=$1
    expected=$2
    shift 2

    set +e
    "$@" >"$tmp/$name.out" 2>"$tmp/$name.err"
    actual=$?
    set -e
    if [ "$actual" -ne "$expected" ]; then
        fail "$name: expected status $expected, got $actual"
    fi
}

[ -x "$timeit" ] || fail "not executable: $timeit"

"$timeit" -n 3 -w 2 -o "$tmp/raw.txt" -- /bin/true >"$tmp/summary.txt"
sed 's/=.*//' "$tmp/summary.txt" >"$tmp/keys.txt"
cat >"$tmp/expected-keys.txt" <<'EOF'
wall_ms_median
wall_ms_mad
user_ms_median
sys_ms_median
maxrss_kb_max
EOF
if ! cmp -s "$tmp/expected-keys.txt" "$tmp/keys.txt"; then
    fail "summary metrics were missing, extra, or out of order"
fi
if ! awk -F= '
    BEGIN { valid = 1 }
    NF != 2 || $2 !~ /^[0-9]+([.][0-9]+)?$/ { valid = 0 }
    END { exit valid && NR == 5 ? 0 : 1 }
' "$tmp/summary.txt"; then
    fail "summary did not contain exactly five numeric metric=value lines"
fi

raw_count=$(wc -l <"$tmp/raw.txt")
[ "$raw_count" -eq 3 ] ||
    fail "warmups leaked into raw samples: expected 3, got $raw_count"
if ! awk '
    BEGIN { valid = 1 }
    index($0, "sample=" NR " ") != 1 { valid = 0 }
    END { exit valid && NR == 3 ? 0 : 1 }
' "$tmp/raw.txt"; then
    fail "raw samples were not numbered exactly 1 through 3"
fi

expect_status invalid-run-count 2 "$timeit" -n 0 -- /bin/true
expect_status missing-separator 2 "$timeit" -n 1 /bin/true
expect_status child-exit 7 "$timeit" -n 1 -w 0 -- /bin/sh -c 'exit 7'
expect_status child-signal 143 "$timeit" -n 1 -w 0 -- /bin/sh -c \
    'kill -TERM $$'

echo "timeit_test: metrics, raw samples, usage, and child status passed"
