#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-closeout-gate-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
gate=$root/ci/check-closeouts.sh

mkdir -p "$tmp/.docs/audits"
: >"$tmp/ci-manifest-rows"

write_valid()
{
    number=$1
    slug=$2
    shown=$(printf '%s\n' "$number" | sed 's/^0*//')
    [ -n "$shown" ] || shown=0
    printf '%s\t%s\t1\n' "$number-$slug" "$shown" \
        >>"$tmp/ci-manifest-rows"
    file=$tmp/.docs/audits/closeout-$number-$slug.md
    {
        printf '# Closeout: Phase %s — %s\n\n' "$number" "$slug"
        printf 'Date:            2026-08-21\n'
        printf 'Baseline commit: abcdef0123456789\n'
        printf 'Reviewer:        Independent Reviewer\n\n'
        printf '## DoD items (from the phase\047s sprint files, every numbered item)\n\n'
        printf '%s\n\n' "- [x] S$shown.1 Verified behavior — EVIDENCE: make test"
        printf '## Open audit findings against this phase\n\n'
        printf 'none\n\n'
        printf '## Verdict\n\n'
        printf 'All phase requirements have current evidence.\n\n'
        printf 'READY\n'
    } >"$file"
}

for number in 00 01 02 03 04 05 06 07 08 09 10 11 12 13; do
    write_valid "$number" "phase-$number"
done
mkdir -p "$tmp/ci"
{
    printf '%s\n' '# Phase\tSprint\tNumbered DoD items'
    cat "$tmp/ci-manifest-rows"
} >"$tmp/ci/closeout-dod.tsv"
CGF_CLOSEOUT_EXPECTED_SPRINTS=14
CGF_CLOSEOUT_EXPECTED_ITEMS=14
export CGF_CLOSEOUT_EXPECTED_SPRINTS CGF_CLOSEOUT_EXPECTED_ITEMS

run_gate()
{
    sh "$gate" "$tmp"
}

expect_failure()
{
    label=$1
    pattern=$2
    if run_gate >"$tmp/$label.out" 2>"$tmp/$label.err"; then
        echo "closeout-gate-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    if ! grep -Fq "$pattern" "$tmp/$label.err"; then
        cat "$tmp/$label.out" "$tmp/$label.err" >&2
        exit 1
    fi
}

run_gate >"$tmp/valid.out"
grep -Fq '14 phase closeouts are conforming and READY' "$tmp/valid.out"

fixture=$tmp/.docs/audits/closeout-00-phase-00.md
cp "$fixture" "$tmp/valid.md"

sed 's/ — EVIDENCE: make test/ — EVIDENCE:/' "$tmp/valid.md" >"$fixture"
expect_failure missing-evidence 'checked item requires nonempty EVIDENCE'

sed 's/- \[x\] S0.1 Verified behavior — EVIDENCE: make test/- [x] S0.1 Verified behavior/' \
    "$tmp/valid.md" >"$fixture"
expect_failure bare-checked 'checked item requires nonempty EVIDENCE'

sed 's/- \[x\] S0.1 Verified behavior — EVIDENCE: make test/- [ ] S0.1 Remaining work/' \
    "$tmp/valid.md" >"$fixture"
expect_failure missing-item-verdict 'unchecked DoD item requires VERDICT'

sed 's/- \[x\] S0.1 Verified behavior — EVIDENCE: make test/- [ ] S0.1 SEMA-C-08 remains — VERDICT: waived: accepted risk/' \
    "$tmp/valid.md" >"$fixture"
expect_failure waived-critical 'Critical or High audit finding may not be waived'

sed 's/- \[x\] S0.1 Verified behavior — EVIDENCE: make test/- [ ] S0.1 SEMA-H-07 remains — VERDICT: waived: accepted risk/' \
    "$tmp/valid.md" >"$fixture"
expect_failure waived-high 'Critical or High audit finding may not be waived'

cp "$tmp/valid.md" "$fixture"
# The tracked manifest is the fresh-clone source of truth for exact coverage.
awk -F '\t' 'BEGIN { OFS = "\t" }
$1 == "00-phase-00" && $2 == 0 { $3 = 2 }
{ print }
' "$tmp/ci/closeout-dod.tsv" >"$tmp/manifest-expanded"
mv "$tmp/manifest-expanded" "$tmp/ci/closeout-dod.tsv"
CGF_CLOSEOUT_EXPECTED_ITEMS=15
export CGF_CLOSEOUT_EXPECTED_ITEMS
expect_failure missing-dod-coverage 'DoD coverage differs from tracked manifest'
awk -F '\t' 'BEGIN { OFS = "\t" }
$1 == "00-phase-00" && $2 == 0 { $3 = 1 }
{ print }
' "$tmp/ci/closeout-dod.tsv" >"$tmp/manifest-restored"
mv "$tmp/manifest-restored" "$tmp/ci/closeout-dod.tsv"
CGF_CLOSEOUT_EXPECTED_ITEMS=14
export CGF_CLOSEOUT_EXPECTED_ITEMS

sed 's/^READY$/NOT READY/' \
    "$tmp/valid.md" >"$fixture"
expect_failure not-ready 'phase is NOT READY'

cp "$tmp/valid.md" "$fixture"
echo 'closeout-gate-meta: PASS'
