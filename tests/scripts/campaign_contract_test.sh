#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
lint=$root/scripts/campaign-lint.sh
check=$root/scripts/campaign-check.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-contract-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

expect_fail() {
    label=$1
    pattern=$2
    shift 2
    if "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"; then
        echo "campaign-contract-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    grep -F "$pattern" "$tmp/$label.err" >/dev/null || {
        echo "campaign-contract-meta: $label emitted the wrong diagnostic" >&2
        cat "$tmp/$label.err" >&2
        exit 1
    }
}

write_results() {
    file=$1
    shift
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        for row do printf '%s\n' "$row"; done
    } >"$file"
}

pass=$(printf 'build\tPASS\tcompiler=cgfried')
known=$(printf 'test.known\tSKIP\tCAMP-DEMO-001;case=upstream')
fixed=$(printf 'test.known\tPASS\tcases=1')
write_results "$tmp/expected" "$pass" "$known"
cp "$tmp/expected" "$tmp/actual"
"$check" "$tmp/expected" "$tmp/actual" >/dev/null
LC_ALL=POSIX "$check" "$tmp/expected" "$tmp/actual" \
    >"$tmp/locale.out" 2>"$tmp/locale.err"
grep -F 'campaign-expected: PASS' "$tmp/locale.out" >/dev/null
[ ! -s "$tmp/locale.err" ] || {
    echo 'campaign-contract-meta: checker leaked caller locale diagnostics' >&2
    cat "$tmp/locale.err" >&2
    exit 1
}

write_results "$tmp/actual" "$pass" "$fixed"
expect_fail improvement 'missing expected row:' \
    "$check" "$tmp/expected" "$tmp/actual"
grep -F 'unexpected actual row:' "$tmp/improvement.err" >/dev/null

write_results "$tmp/actual" "$pass"
expect_fail regression 'missing expected row:' \
    "$check" "$tmp/expected" "$tmp/actual"

mkdir -p "$tmp/ci/campaigns"
mkdir -p "$tmp/tests/programs"
printf 'int main(void) { return 0; }\n' >"$tmp/tests/programs/demo.c"
cat >"$tmp/ci/campaigns/FINDINGS.md" <<'EOF'
# Test findings

## Scope exclusions

| ID | Severity | Status | Phase | Finding / disposition |
|---|---|---|---|---|
| `CAMP-DEMO-001` | scope | DEFERRED | test | Fixture exclusion. |

## Fixed compiler findings

| ID | Severity | Phase | Finding | Regression evidence |
|---|---|---|---|---|
| `CAMP-DEMO-002` | high | test | Fixture repair. | `tests/programs/demo.c` |
EOF
write_results "$tmp/ci/campaigns/demo.expected" "$pass" "$known"
cat >"$tmp/ci/campaigns/demo.mk" <<'EOF'
DEMO_REF := v1
DEMO_SRC := .docs/refs/demo
.PHONY: demo-configure demo-build demo-validate demo-expected
demo-configure:
demo-build: demo-configure
demo-validate: demo-build
demo-expected: demo-validate
EOF
"$lint" "$tmp/ci/campaigns/demo.mk" >/dev/null

wrong_namespace=$(printf 'test.known\tSKIP\tCAMP-CURL-003;case=upstream')
write_results "$tmp/ci/campaigns/demo.expected" "$pass" "$wrong_namespace"
expect_fail wrong-namespace 'deviation ID must match CAMP-DEMO-NNN' \
    "$lint" "$tmp/ci/campaigns/demo.mk"

missing_id=$(printf 'test.known\tFAIL\tCAMP-DEMO-999;case=regression')
write_results "$tmp/ci/campaigns/demo.expected" "$pass" "$missing_id"
expect_fail missing-finding \
    'deviation ID is absent from FINDINGS.md: CAMP-DEMO-999' \
    "$lint" "$tmp/ci/campaigns/demo.mk"

write_results "$tmp/ci/campaigns/demo.expected" "$pass" "$known"
sed 's,tests/programs/demo.c,tests/programs/missing.c,' \
    "$tmp/ci/campaigns/FINDINGS.md" >"$tmp/ci/campaigns/bad-findings.md"
mv "$tmp/ci/campaigns/bad-findings.md" "$tmp/ci/campaigns/FINDINGS.md"
expect_fail missing-regression \
    'CAMP-DEMO-002 cites missing regression path: tests/programs/missing.c' \
    "$lint" "$tmp/ci/campaigns/demo.mk"
sed 's,tests/programs/missing.c,tests/programs/demo.c,' \
    "$tmp/ci/campaigns/FINDINGS.md" >"$tmp/ci/campaigns/good-findings.md"
mv "$tmp/ci/campaigns/good-findings.md" "$tmp/ci/campaigns/FINDINGS.md"

sed '/demo-validate:/d' "$tmp/ci/campaigns/demo.mk" >"$tmp/bad.mk"
mv "$tmp/bad.mk" "$tmp/ci/campaigns/demo.mk"
expect_fail missing-stage 'missing target demo-validate' \
    "$lint" "$tmp/ci/campaigns/demo.mk"

inventory='chibicc curl lua musl qbe sqlite tinycc zlib'
for name in $inventory; do
    upper=$(printf '%s' "$name" | tr 'abcdefghijklmnopqrstuvwxyz-' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')
    {
        printf '%s_REF := v1\n' "$upper"
        printf '%s_SRC := .docs/refs/%s\n' "$upper" "$name"
        printf '%s-configure:\n%s-build:\n%s-validate:\n%s-expected:\n' \
            "$name" "$name" "$name" "$name"
    } >"$tmp/ci/campaigns/$name.mk"
    write_results "$tmp/ci/campaigns/$name.expected" "$pass"
done
{
    echo 'schema: cgf-campaign-ladder-v1'
    echo 'campaigns:'
    for name in $inventory; do
        printf '  - name: %s\n' "$name"
        printf '    descriptor: ci/campaigns/%s.mk\n' "$name"
        printf '    expected: ci/campaigns/%s.expected\n' "$name"
        printf '    target: %s-expected\n' "$name"
        echo '    lanes: test'
        echo '    cadence: test'
        echo '    bar: test'
    done
} >"$tmp/ci/campaigns/ladder.yml"
"$lint" --ladder "$tmp/ci/campaigns/ladder.yml" \
    "$tmp/ci/campaigns/chibicc.mk" >/dev/null

sed 's/target: curl-expected/target: wrong-expected/' \
    "$tmp/ci/campaigns/ladder.yml" >"$tmp/ci/campaigns/bad-ladder.yml"
expect_fail ladder-target 'target does not match campaign curl' \
    "$lint" --ladder "$tmp/ci/campaigns/bad-ladder.yml" \
        "$tmp/ci/campaigns/chibicc.mk"

printf 'campaign-contract-meta: PASS\n'
