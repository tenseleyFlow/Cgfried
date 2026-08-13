#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
report=$root/scripts/campaign-failure-report.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-failure-report-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

write_results() {
    file=$1
    shift
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        for row do printf '%s\n' "$row"; done
    } >"$file"
}

build=$(printf 'build\tPASS\tcompiler=cgfried')
expected_test=$(printf 'test.upstream\tSKIP\tCAMP-ZLIB-001;case=known')
actual_test=$(printf 'test.upstream\tPASS\tcases=4')
write_results "$tmp/expected" "$build" "$expected_test"
write_results "$tmp/actual" "$build" "$actual_test"

"$report" zlib "$tmp/expected" "$tmp/actual" \
    'https://ci.example/artifacts/123?download=1' \
    'https://ci.example/runs/123' >"$tmp/first"
"$report" zlib "$tmp/expected" "$tmp/actual" \
    'https://ci.example/artifacts/123?download=1' \
    'https://ci.example/runs/123' >"$tmp/second"
cmp "$tmp/first" "$tmp/second"
LC_ALL=POSIX "$report" zlib "$tmp/expected" "$tmp/actual" \
    'https://ci.example/artifacts/123?download=1' \
    'https://ci.example/runs/123' >"$tmp/hostile-locale" \
    2>"$tmp/hostile-locale.err"
cmp "$tmp/first" "$tmp/hostile-locale"
[ ! -s "$tmp/hostile-locale.err" ] || {
    echo 'campaign-failure-report-meta: report leaked caller locale diagnostics' >&2
    cat "$tmp/hostile-locale.err" >&2
    exit 1
}

cat >"$tmp/want" <<'EOF'
# Campaign drift: zlib (CAMP-ZLIB)

- Project: `zlib`
- Finding namespace: `CAMP-ZLIB`
- Run: <https://ci.example/runs/123>
- Artifacts: <https://ci.example/artifacts/123?download=1>

## Missing expected rows

    test.upstream	SKIP	CAMP-ZLIB-001;case=known

## Unexpected actual rows

    test.upstream	PASS	cases=4

This diff is bidirectional: regressions and unrecorded improvements both fail the campaign ratchet.
Classify the result under `CAMP-ZLIB-NNN`; update the expected file only with matching finding and regression evidence.
EOF
cmp "$tmp/want" "$tmp/first"

hostile=$(printf 'test.upstream\tPASS\t```html,<script>alert(1)</script>')
write_results "$tmp/hostile-actual" "$build" "$hostile"
"$report" zlib "$tmp/expected" "$tmp/hostile-actual" \
    'https://ci.example/artifacts/123' 'https://ci.example/runs/123' \
    >"$tmp/hostile-report"
grep -F '    test.upstream' "$tmp/hostile-report" >/dev/null
if grep -F '^```' "$tmp/hostile-report" >/dev/null; then
    echo 'campaign-failure-report-meta: result detail escaped its code block' >&2
    exit 1
fi

if "$report" zlib "$tmp/expected" "$tmp/expected" \
    'https://ci.example/artifacts/123' 'https://ci.example/runs/123' \
    >"$tmp/match.out" 2>"$tmp/match.err"; then
    echo 'campaign-failure-report-meta: matching results unexpectedly passed' >&2
    exit 1
fi
grep -F 'expected result drift' "$tmp/match.err" >/dev/null

if "$report" 'bad/project' "$tmp/expected" "$tmp/actual" \
    'https://ci.example/artifacts/123' 'https://ci.example/runs/123' \
    >"$tmp/project.out" 2>"$tmp/project.err"; then
    echo 'campaign-failure-report-meta: invalid project unexpectedly passed' >&2
    exit 1
fi
grep -F 'invalid project' "$tmp/project.err" >/dev/null

printf 'campaign-failure-report-meta: PASS\n'
