#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-audit-lifecycle-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir -p "$tmp/scripts" "$tmp/tests/audit-regressions" "$tmp/include"
cp "$root/scripts/check-audit-fixtures.sh" "$tmp/scripts/"
gate="$tmp/scripts/check-audit-fixtures.sh"
compiler="$tmp/fake-cgfried"
chmod +x "$gate"

cat >"$compiler" <<'EOF'
#!/bin/sh
case ${FAKE_RESULT:-0} in
0) exit 0 ;;
1) printf 'fixture rejected\n' >&2; exit 1 ;;
*) exit 2 ;;
esac
EOF
chmod +x "$compiler"

id=FE-H-01
fixture=fe-h-01.c
title='an unnamed variadic prototype is accepted in ISO C'
manifest="$tmp/tests/audit-regressions/manifest.tsv"
source="$tmp/tests/audit-regressions/$fixture"

write_manifest()
{
    state=$1
    {
        printf '# Finding ID\tFixture\tState\tTitle\n'
        printf '%s\t%s\t%s\t%s\n' "$id" "$fixture" "$state" "$title"
    } >"$manifest"
}

write_header()
{
    marker=$1
    printf '// %s(audit): %s %s\nint fixture;\n' \
        "$marker" "$id" "$title" >"$source"
}

expect_failure()
{
    label=$1
    pattern=$2
    result=${3:-0}
    if FAKE_RESULT=$result "$gate" "$compiler" \
        >"$tmp/$label.out" 2>"$tmp/$label.err"; then
        echo "audit-fixture-lifecycle-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    if ! grep -Fq "$pattern" "$tmp/$label.out" &&
       ! grep -Fq "$pattern" "$tmp/$label.err"; then
        cat "$tmp/$label.out" "$tmp/$label.err" >&2
        exit 1
    fi
}

write_manifest OPEN
write_header XFAIL
FAKE_RESULT=0 "$gate" "$compiler" >"$tmp/open.out"
grep -Fq "XFAIL $id: $title" "$tmp/open.out"
grep -Fq '1 checks; 0 PASS, 1 XFAIL, 0 XPASS, 0 FAIL' "$tmp/open.out"

write_manifest BROKEN
expect_failure malformed-state "malformed state for $id: BROKEN"

{
    printf '# Finding ID\tFixture\tState\tTitle\n'
    printf '%s\t%s\t\t%s\n' "$id" "$fixture" "$title"
} >"$manifest"
expect_failure missing-state "missing state for $id"

write_manifest OPEN
write_header RESOLVED
expect_failure header-mismatch 'header/state mismatch' 0

write_header XFAIL
expect_failure open-unexpected-pass "XPASS $id: $title" 1
grep -Fq '1 checks; 0 PASS, 0 XFAIL, 1 XPASS, 0 FAIL' \
    "$tmp/open-unexpected-pass.out"

write_manifest PASS
write_header RESOLVED
FAKE_RESULT=1 "$gate" "$compiler" >"$tmp/pass.out"
grep -Fq "PASS $id: $title" "$tmp/pass.out"
grep -Fq '1 checks; 1 PASS, 0 XFAIL, 0 XPASS, 0 FAIL' "$tmp/pass.out"

expect_failure pass-regression "FAIL $id: resolved fixture regressed" 0
grep -Fq '1 checks; 0 PASS, 0 XFAIL, 0 XPASS, 1 FAIL' \
    "$tmp/pass-regression.out"

echo 'audit-fixture-lifecycle-meta: PASS'
