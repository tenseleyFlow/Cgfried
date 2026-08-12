#!/bin/sh
set -eu

usage() {
    echo "usage: $0 EXPECTED ACTUAL" >&2
    exit 2
}

fail() {
    echo "campaign-expected: $*" >&2
    exit 1
}

[ "$#" -eq 2 ] || usage
expected=$1
actual=$2

[ -f "$expected" ] && [ -r "$expected" ] ||
    fail "expected file is not readable: $expected"
[ -f "$actual" ] && [ -r "$actual" ] ||
    fail "actual file is not readable: $actual"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-expected.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

validate() {
    input=$1
    label=$2
    rows=$3

    first=$(sed -n '1p' "$input")
    second=$(sed -n '2p' "$input")
    [ "$first" = '# cgf-campaign-results-v1' ] ||
        fail "$label has an invalid version header: $input"
    expected_columns=$(printf '# columns=key\toutcome\tdetail')
    [ "$second" = "$expected_columns" ] ||
        fail "$label has an invalid columns header: $input"

    if ! sed '1,2d' "$input" | awk '
        BEGIN { FS = sprintf("%c", 9) }
        /^#/ || NF != 3 { bad = 1; next }
        $1 !~ /^[a-z0-9][a-z0-9._-]*$/ { bad = 1 }
        $2 !~ /^(PASS|FAIL|SKIP)$/ { bad = 1 }
        $3 == "" { bad = 1 }
        END { exit bad }
    '; then
        fail "$label has a malformed result row: $input"
    fi

    sed '1,2d' "$input" >"$rows"
    [ -s "$rows" ] || fail "$label contains no result rows: $input"
    LC_ALL=C sort "$rows" >"$rows.sorted"
    cmp -s "$rows" "$rows.sorted" ||
        fail "$label result rows are not in canonical byte order: $input"
    cut -f 1 "$rows" | LC_ALL=C sort | uniq -d >"$rows.duplicates"
    if [ -s "$rows.duplicates" ]; then
        sed 's/^/campaign-expected: duplicate result key: /' \
            "$rows.duplicates" >&2
        exit 1
    fi
}

validate "$expected" expected "$tmp/expected"
validate "$actual" actual "$tmp/actual"

comm -23 "$tmp/expected" "$tmp/actual" >"$tmp/missing"
comm -13 "$tmp/expected" "$tmp/actual" >"$tmp/unexpected"

if [ -s "$tmp/missing" ] || [ -s "$tmp/unexpected" ]; then
    sed 's/^/campaign-expected: missing expected row: /' "$tmp/missing" >&2
    sed 's/^/campaign-expected: unexpected actual row: /' "$tmp/unexpected" >&2
    fail "result drift: $actual differs from $expected"
fi

printf 'campaign-expected: PASS rows=%s expected=%s actual=%s\n' \
    "$(wc -l <"$tmp/expected" | tr -d ' ')" "$expected" "$actual"
