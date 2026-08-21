#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-burndown-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$tmp/scripts" "$tmp/.docs/audits" "$tmp/tests/audit-regressions"
cp "$root/scripts/check-burndown.sh" "$tmp/scripts/"
gate="$tmp/scripts/check-burndown.sh"
chmod +x "$gate"

write_ledger()
{
    cat >"$tmp/.docs/audits/audit-01-alpha.md" <<'EOF'
# Front one

~~ID: `AA-H-01`~~
~~Severity: High~~
Resolution: RESOLVED 2026-08-20 by `1111111`.

ID: `AA-M-02`
Severity: Medium
EOF
    cat >"$tmp/.docs/audits/audit-02-beta.md" <<'EOF'
# Front two

ID: `BB-C-01`
Severity: Critical

~~ID: `BB-L-02`~~
~~Severity: Low~~
Resolution: RESOLVED 2026-08-20 by `2222222`.
EOF
}

write_manifest()
{
    cat >"$tmp/tests/audit-regressions/manifest.tsv" <<'EOF'
# Finding ID	Fixture	State	Title
AA-H-01	aa-h-01.c	PASS	resolved high
AA-M-02	aa-m-02.c	OPEN	open medium
BB-C-01	bb-c-01.c	OPEN	open critical
BB-L-02	bb-l-02.c	PASS	resolved low
EOF
}

write_burndown()
{
    counts=$1
    cat >"$tmp/.docs/audits/burndown.md" <<EOF
# Test burndown

| Date | Commit | Remediation | Critical | High | Medium | Low | Total |
|---|---|---|---:|---:|---:|---:|---:|
| 2026-08-20 | \`seed\` | seed | 1 | 1 | 1 | 1 | 4 |
| 2026-08-21 | \`head\` | current | $counts |
EOF
}

expect_failure()
{
    label=$1
    pattern=$2
    if "$gate" >"$tmp/$label.out" 2>"$tmp/$label.err"; then
        echo "burndown-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    if ! grep -Fq "$pattern" "$tmp/$label.out" &&
       ! grep -Fq "$pattern" "$tmp/$label.err"; then
        cat "$tmp/$label.out" "$tmp/$label.err" >&2
        exit 1
    fi
}

write_ledger
write_manifest
write_burndown '1 | 0 | 1 | 0 | 2'
"$gate" >"$tmp/pass.out"
grep -Fq 'PASS (2 open: C=1 H=0 M=1 L=0; ledger and lifecycle agree)' \
    "$tmp/pass.out"

write_burndown '0 | 0 | 0 | 0 | 0'
expect_failure stale-final-row \
    'final row is C/H/M/L/total 0 0 0 0 0; current open findings are 1 0 1 0 2'

write_burndown '1 | 0 | 1 | 0 | 2'
sed 's/AA-M-02\taa-m-02.c\tOPEN/AA-M-02\taa-m-02.c\tPASS/' \
    "$tmp/tests/audit-regressions/manifest.tsv" >"$tmp/manifest.changed"
mv "$tmp/manifest.changed" "$tmp/tests/audit-regressions/manifest.tsv"
expect_failure lifecycle-mismatch \
    'front-ledger resolution state disagrees with lifecycle manifest'

write_manifest
sed '/Resolution: RESOLVED 2026-08-20 by `1111111`/d' \
    "$tmp/.docs/audits/audit-01-alpha.md" >"$tmp/ledger.changed"
mv "$tmp/ledger.changed" "$tmp/.docs/audits/audit-01-alpha.md"
expect_failure incomplete-resolution \
    'struck finding AA-H-01 must have one RESOLVED line'

write_ledger
write_burndown '1 | 0 | 1 | 0 | 3'
expect_failure malformed-total 'total 3 does not equal C/H/M/L sum 2'

echo 'burndown-meta: PASS'
