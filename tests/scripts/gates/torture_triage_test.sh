#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
triage=$root/scripts/triage-torture.sh
fixtures=$root/tests/scripts/gates/fixtures/torture-triage
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-triage-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "torture_triage_test: $*" >&2
    exit 1
}

expect_status()
{
    name=$1
    expected=$2
    diagnostic=$3
    shift 3
    status=0
    "$@" >"$tmp/$name.out" 2>"$tmp/$name.err" || status=$?
    [ "$status" -eq "$expected" ] || {
        sed 's/^/  /' "$tmp/$name.err" >&2
        fail "$name exited $status, expected $expected"
    }
    grep -F "$diagnostic" "$tmp/$name.err" >/dev/null || {
        sed 's/^/  /' "$tmp/$name.err" >&2
        fail "$name lacked diagnostic: $diagnostic"
    }
}

results="$fixtures/results-a.tsv $fixtures/results-b.tsv $fixtures/results-arm.tsv"

mkdir "$tmp/lexical-alias"
echo 'must survive lexical alias refusal' >"$tmp/lexical-alias/artifact"
echo 'must survive lexical alias refusal' >"$tmp/lexical-alias/artifact.expected"
expect_status lexical-output-alias 3 'triage-torture: --emit-passing and --output require distinct paths' \
    "$triage" --output "$tmp/lexical-alias/artifact" \
    --emit-passing "$tmp/lexical-alias/./artifact" "$fixtures/results-a.tsv"
cmp "$tmp/lexical-alias/artifact.expected" "$tmp/lexical-alias/artifact" ||
    fail "lexical output alias changed the existing artifact"

mkdir "$tmp/physical-output"
ln -s "$tmp/physical-output" "$tmp/symlink-output"
echo 'must survive symlink-directory alias refusal' >"$tmp/physical-output/artifact"
echo 'must survive symlink-directory alias refusal' >"$tmp/physical-output/artifact.expected"
expect_status symlink-directory-alias 3 'triage-torture: --emit-passing and --output require distinct paths' \
    "$triage" --output "$tmp/physical-output/artifact" \
    --emit-passing "$tmp/symlink-output/artifact" "$fixtures/results-a.tsv"
cmp "$tmp/physical-output/artifact.expected" "$tmp/physical-output/artifact" ||
    fail "symlink-directory output alias changed the existing artifact"

echo 'must survive hard-link alias refusal' >"$tmp/hardlink-output"
ln "$tmp/hardlink-output" "$tmp/hardlink-passing"
echo 'must survive hard-link alias refusal' >"$tmp/hardlink-output.expected"
expect_status hardlink-output-alias 3 'triage-torture: --emit-passing and --output require distinct paths' \
    "$triage" --output "$tmp/hardlink-output" \
    --emit-passing "$tmp/hardlink-passing" "$fixtures/results-a.tsv"
cmp "$tmp/hardlink-output.expected" "$tmp/hardlink-output" ||
    fail "hard-link output alias changed the existing artifact"

cp "$fixtures/results-a.tsv" "$tmp/result-as-report.tsv"
cp "$tmp/result-as-report.tsv" "$tmp/result-as-report.expected"
expect_status report-input-alias 3 'triage-torture: output destination aliases result stream:' \
    "$triage" --output "$tmp/./result-as-report.tsv" \
    "$tmp/result-as-report.tsv"
cmp "$tmp/result-as-report.expected" "$tmp/result-as-report.tsv" ||
    fail "report/input alias changed the result evidence"

mkdir "$tmp/result-input-physical"
ln -s "$tmp/result-input-physical" "$tmp/result-input-symlink"
cp "$fixtures/results-a.tsv" "$tmp/result-input-physical/evidence.tsv"
cp "$tmp/result-input-physical/evidence.tsv" \
    "$tmp/result-input-physical/evidence.expected"
expect_status passing-input-alias 3 'triage-torture: output destination aliases result stream:' \
    "$triage" --emit-passing "$tmp/result-input-symlink/evidence.tsv" \
    "$tmp/result-input-physical/evidence.tsv"
cmp "$tmp/result-input-physical/evidence.expected" \
    "$tmp/result-input-physical/evidence.tsv" ||
    fail "passing/input symlink alias changed the result evidence"

cp "$fixtures/results-a.tsv" "$tmp/result-hardlink-source.tsv"
ln "$tmp/result-hardlink-source.tsv" "$tmp/result-hardlink-output.md"
cp "$tmp/result-hardlink-source.tsv" "$tmp/result-hardlink.expected"
expect_status hardlink-input-alias 3 'triage-torture: output destination aliases result stream:' \
    "$triage" --output "$tmp/result-hardlink-output.md" \
    "$tmp/result-hardlink-source.tsv"
cmp "$tmp/result-hardlink.expected" "$tmp/result-hardlink-source.tsv" ||
    fail "report/input hard-link alias changed the result evidence"

grep -v 'torture-execute/diagless-opt.c@O0@' "$fixtures/passing.txt" \
    >"$tmp/passing.txt"
# shellcheck disable=SC2086
CGF_TORTURE_TRIAGE_POLICY=$fixtures/policy.tsv \
    "$triage" --emit-passing "$tmp/passing.txt" --output "$tmp/report-one.md" $results
cmp "$fixtures/passing.txt" "$tmp/passing.txt" ||
    fail "--emit-passing output differed"
if find "$tmp" \( -name '.passing.txt.tmp.*' -o -name '.report-one.md.tmp.*' \) \
    -print | grep . >/dev/null; then
    fail "successful atomic publication left a staging file"
fi
grep -F -- '- source-revision: `1111111111111111111111111111111111111111`' \
    "$tmp/report-one.md" >/dev/null || fail "common source provenance was omitted"
grep -F '| arm64-linux | `2323232323232323232323232323232323232323232323232323232323232323` | `3434343434343434343434343434343434343434343434343434343434343434` |' \
    "$tmp/report-one.md" >/dev/null || fail "arm stream provenance was omitted"
grep -F '| x86_64-linux-gnu | `0000000000000000000000000000000000000000000000000000000000000000` | `1212121212121212121212121212121212121212121212121212121212121212` |' \
    "$tmp/report-one.md" >/dev/null || fail "per-stream compiler provenance was collapsed"

# Baseline refresh is monotone for each observed target and preserves target
# slices absent from a partial refresh.
sed -n '1,$p' "$fixtures/passing.txt" >"$tmp/shrink-direct.txt"
sed -n '1,$p' "$fixtures/passing.txt" >"$tmp/shrink-direct.expected"
expect_status emit-shrink 1 'regression: existing PASS key is not observed PASS: ctestsuite/00001.c@O0@x86_64-linux-gnu' \
    "$triage" --emit-passing "$tmp/shrink-direct.txt" "$fixtures/results-a.tsv"
cmp "$tmp/shrink-direct.expected" "$tmp/shrink-direct.txt" ||
    fail "direct shrinking refresh changed passing bytes"

sed -n '1,$p' "$fixtures/passing.txt" >"$tmp/shrink-combined.txt"
sed -n '1,$p' "$fixtures/passing.txt" >"$tmp/shrink-combined.expected"
echo 'must survive combined staging failure' >"$tmp/shrink-combined.md"
echo 'must survive combined staging failure' >"$tmp/shrink-combined.expected.md"
expect_status combined-shrink 1 'regression: existing PASS key is not observed PASS: ctestsuite/00001.c@O0@x86_64-linux-gnu' env \
    CGF_TORTURE_TRIAGE_POLICY="$fixtures/policy.tsv" \
    "$triage" --emit-passing "$tmp/shrink-combined.txt" \
    --output "$tmp/shrink-combined.md" "$fixtures/results-a.tsv"
cmp "$tmp/shrink-combined.expected" "$tmp/shrink-combined.txt" ||
    fail "combined shrinking refresh changed passing bytes"
cmp "$tmp/shrink-combined.expected.md" "$tmp/shrink-combined.md" ||
    fail "combined staging failure published the report before both artifacts validated"

# If the authoritative passing-file rename fails after the report rename, the
# report is rolled back so the two committed artifacts remain one generation.
mkdir "$tmp/fail-mv-bin"
cp "$fixtures/fail-mv.sh" "$tmp/fail-mv-bin/mv"
chmod 755 "$tmp/fail-mv-bin/mv"
cp "$fixtures/passing.txt" "$tmp/pair-passing.txt"
cp "$tmp/pair-passing.txt" "$tmp/pair-passing.expected"
echo 'must survive second-publication failure' >"$tmp/pair-report.md"
cp "$tmp/pair-report.md" "$tmp/pair-report.expected"
real_mv=$(command -v mv)
expect_status second-publication-failure 3 'triage-torture: cannot publish passing output:' env \
    PATH="$tmp/fail-mv-bin:$PATH" CGF_REAL_MV="$real_mv" \
    CGF_FAIL_MV_DEST="$tmp/pair-passing.txt" \
    CGF_TORTURE_TRIAGE_POLICY="$fixtures/policy.tsv" \
    "$triage" --emit-passing "$tmp/pair-passing.txt" \
    --output "$tmp/pair-report.md" $results
cmp "$tmp/pair-passing.expected" "$tmp/pair-passing.txt" ||
    fail "second-publication failure changed the passing ratchet"
cmp "$tmp/pair-report.expected" "$tmp/pair-report.md" ||
    fail "second-publication failure did not roll back the report"

sed -n '1,3p' "$fixtures/passing.txt" >"$tmp/header-only-passing.txt"
"$triage" --emit-passing "$tmp/header-only-passing.txt" \
    "$fixtures/results-arm.tsv"
grep -F 'torture-execute/arm-ok.c@O0@arm64-linux' \
    "$tmp/header-only-passing.txt" >/dev/null ||
    fail "header-only baseline did not accept initial growth"

{
    sed -n '1,3p' "$fixtures/passing.txt"
    grep '@arm64-linux$' "$fixtures/passing.txt"
} >"$tmp/preserve-arm-passing.txt"
"$triage" --emit-passing "$tmp/preserve-arm-passing.txt" \
    "$fixtures/results-a.tsv"
grep -F 'torture-execute/arm-ok.c@O0@arm64-linux' \
    "$tmp/preserve-arm-passing.txt" >/dev/null ||
    fail "x86 refresh discarded the unobserved arm target slice"
grep -F 'torture-execute/diagless-opt.c@O0@x86_64-linux-gnu' \
    "$tmp/preserve-arm-passing.txt" >/dev/null ||
    fail "partial target refresh did not add a new observed pass"

# Input stream order cannot affect either generated artifact.
CGF_TORTURE_TRIAGE_POLICY=$fixtures/policy.tsv \
    "$triage" --emit-passing "$tmp/passing-reversed.txt" \
    --output "$tmp/report-two.md" "$fixtures/results-arm.tsv" \
    "$fixtures/results-b.tsv" "$fixtures/results-a.tsv"
cmp "$tmp/passing.txt" "$tmp/passing-reversed.txt" ||
    fail "pass set depended on stream order"
cmp "$tmp/report-one.md" "$tmp/report-two.md" ||
    fail "triage report was not byte-identical"

grep -F '| torture-compile | O0 | x86_64-linux-gnu | 4 | 1 | 0 | 1 | 2 | 25.00% |' \
    "$tmp/report-one.md" >/dev/null || fail "baseline counts were wrong"
grep -F '| torture-execute | O0 | arm64-linux | 1 | 1 | 0 | 0 | 0 | 100.00% |' \
    "$tmp/report-one.md" >/dev/null || fail "arm baseline was omitted"
grep -F -- '- Count: 4' "$tmp/report-one.md" >/dev/null ||
    fail "largest bucket count was wrong"
grep -F 'torture-compile/alpha.c@O2@x86_64-linux-gnu, torture-compile/beta.c@O1@x86_64-linux-gnu, torture-compile/delta.c@Os@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null || fail "exemplars were not sorted/capped"
if grep -F 'torture-compile/gamma.c@O3@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null; then
    fail "bucket emitted more than three exemplars"
fi
grep -F -- '- Labels: optdiv=1/4' "$tmp/report-one.md" >/dev/null ||
    fail "mixed bucket overclaimed optdiv membership"
grep -F -- '- Optdiv members: 1 of 4' "$tmp/report-one.md" >/dev/null ||
    fail "mixed bucket lacked exact optdiv count"
grep -F -- '- Optdiv exemplars: torture-compile/alpha.c@O2@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null || fail "optdiv member identity was omitted"
grep -F -- '- Runtime split: `optdiv`' "$tmp/report-one.md" >/dev/null ||
    fail "diagnostic-less optdiv runtime bucket was not split"
grep -F -- '- Runtime split: `non-optdiv`' "$tmp/report-one.md" >/dev/null ||
    fail "diagnostic-less non-optdiv runtime bucket was not split"
grep -F -- '- Exemplars: torture-execute/diagless-opt.c@O2@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null || fail "optdiv runtime member was not isolated"
grep -F -- '- Exemplars: torture-execute/diagless-base.c@O0@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null || fail "non-optdiv runtime member was not isolated"
if grep -F -- '- Optdiv exemplars: torture-execute/diagless-base.c@O0@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null; then
    fail "non-optdiv peer was mislabeled as optdiv"
fi
grep -F -- '- Hypothesis: Diagnostic-less O0 runtime failures require semantic investigation.' \
    "$tmp/report-one.md" >/dev/null || fail "non-optdiv policy decision was not distinct"
grep -F -- '- Disposition: `fix-sprint:s56.5-runtime-opt`' \
    "$tmp/report-one.md" >/dev/null || fail "optdiv policy disposition was not distinct"
grep -F -- '- Hypothesis: O0-stable signal family needs runtime investigation.' \
    "$tmp/report-one.md" >/dev/null || fail "stable-fingerprint O0 signal was not non-optdiv"
grep -F -- '- Disposition: `fix-sprint:s56.5-signal-semantic`' \
    "$tmp/report-one.md" >/dev/null || fail "non-optdiv signal policy key was not applied"
grep -F -- '- Hypothesis: Optimized-only signal family is an optimizer divergence.' \
    "$tmp/report-one.md" >/dev/null || fail "stable-fingerprint optimized signal was not optdiv"
grep -F -- '- Disposition: `fix-sprint:s56.5-signal-opt`' \
    "$tmp/report-one.md" >/dev/null || fail "optdiv signal policy key was not applied"
grep -F -- '- Exemplars: torture-execute/signal-base.c@O0@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null || fail "non-optdiv signal exemplar was not isolated"
grep -F -- '- Exemplars: torture-execute/signal-opt.c@O2@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null || fail "optdiv signal exemplar was not isolated"
grep -F -- '- Disposition: `wontfix-0.1.0`' "$tmp/report-one.md" >/dev/null ||
    fail "Sprint 55 refusal was not pre-dispositioned"
grep -F 'torture-compile/builtin.c@O0@x86_64-linux-gnu' \
    "$tmp/report-one.md" >/dev/null || fail "XFAIL cell was not bucketed"
grep -F -- '- Disposition: `out-of-scope`' "$tmp/report-one.md" >/dev/null ||
    fail "_Complex was not pre-dispositioned"
grep -F -- '- Disposition: `xfail:TORT-055`' "$tmp/report-one.md" >/dev/null ||
    fail "runner XFAIL metadata was not preserved"
grep -F -- '- Disposition: `fix-sprint:s56.5-runtime`' "$tmp/report-one.md" >/dev/null ||
    fail "human runtime disposition was not applied"
grep -F -- '- Tags: arithmetic, integer' "$tmp/report-one.md" >/dev/null ||
    fail "c-testsuite tags were not surfaced deterministically"
grep -F '| computed-goto | 0 | `wontfix-0.1.0` |' \
    "$tmp/report-one.md" >/dev/null || fail "zero-count known class was omitted"
grep -F '| vector-mode-attribute | 0 | `wontfix-0.1.0` |' \
    "$tmp/report-one.md" >/dev/null || fail "combined vector/mode class was omitted"
grep -F '| trampolines | skip-unless:trampolines | 1 |' \
    "$tmp/report-one.md" >/dev/null || fail "SKIP policy count was omitted"
grep -F -- '- Failed cells: 14' "$tmp/report-one.md" >/dev/null ||
    fail "coverage failed count was wrong"
grep -F -- '- Bucketed cells: 14' "$tmp/report-one.md" >/dev/null ||
    fail "coverage bucket count was wrong"
grep -F -- '- Unbucketed cells: 0' "$tmp/report-one.md" >/dev/null ||
    fail "coverage did not prove total bucketing"
grep -F -- '- Bucket coverage: 100.00%' "$tmp/report-one.md" >/dev/null ||
    fail "coverage percentage was omitted"
grep -F -- '- Misc bucket share: 0.00% (no misc bucket is emitted)' \
    "$tmp/report-one.md" >/dev/null || fail "zero-misc proof was omitted"
grep -F 'at least 95% of applicable execute cells passing' \
    "$tmp/report-one.md" >/dev/null || fail "phase-end target was omitted"
if grep -F '### Misc' "$tmp/report-one.md" >/dev/null; then
    fail "triage report emitted a forbidden misc bucket"
fi

# Human decisions live outside the generated report. Applied and stale rows
# both survive repeated regeneration, including reversed result stream order.
# shellcheck disable=SC2086
CGF_TORTURE_TRIAGE_POLICY=$fixtures/policy.tsv \
    "$triage" --output "$tmp/policy-one.md" $results
CGF_TORTURE_TRIAGE_POLICY=$fixtures/policy.tsv \
    "$triage" --output "$tmp/policy-two.md" "$fixtures/results-arm.tsv" \
    "$fixtures/results-b.tsv" "$fixtures/results-a.tsv"
cmp "$tmp/policy-one.md" "$tmp/policy-two.md" ||
    fail "policy-backed report was not byte-identical"
grep -F -- '- Hypothesis: Human-confirmed parser family.' \
    "$tmp/policy-one.md" >/dev/null || fail "human hypothesis was erased"
grep -F -- '- Disposition: `xfail:TORT-123`' \
    "$tmp/policy-one.md" >/dev/null || fail "human XFAIL decision was erased"
grep -F -- '- Disposition: `fix-sprint:s56.5-output`' \
    "$tmp/policy-one.md" >/dev/null || fail "human fix decision was erased"
if awk 'BEGIN { RS = "" }
        /Fingerprint: `stdout mismatch`/ && /Runtime split:/ { found = 1 }
        END { exit found ? 0 : 1 }' "$tmp/policy-one.md"; then
    fail "genuine output mismatch was split as a runtime control failure"
fi
grep -F -- '- Applied decisions: 8' "$tmp/policy-one.md" >/dev/null ||
    fail "applied policy count was wrong"
grep -F -- '- Stale decisions: 1' "$tmp/policy-one.md" >/dev/null ||
    fail "stale policy was not reported"
grep -F '| - | `retired fingerprint` | cg | all | Retired bucket retained for review. | `fix-sprint:s56.9-retired` |' \
    "$tmp/policy-one.md" >/dev/null || fail "stale decision was erased"
CGF_TORTURE_TRIAGE_POLICY=$fixtures/policy.tsv \
    "$triage" --output "$tmp/no-skips.md" "$fixtures/results-arm.tsv"
grep -F '| - | - | 0 |' "$tmp/no-skips.md" >/dev/null ||
    fail "no-SKIP report lacked its deterministic zero row"

expect_status duplicate-policy 3 'duplicate policy bucket' env \
    CGF_TORTURE_TRIAGE_POLICY="$fixtures/policy-duplicate.tsv" \
    "$triage" --output "$tmp/duplicate-policy.md" "$fixtures/results-a.tsv"
expect_status malformed-policy 3 'invalid policy disposition: fix-later' env \
    CGF_TORTURE_TRIAGE_POLICY="$fixtures/policy-malformed.tsv" \
    "$triage" --output "$tmp/malformed-policy.md" "$fixtures/results-a.tsv"
sed 's/\tnon-optdiv\t/\tbad-variant\t/' "$fixtures/policy.tsv" \
    >"$tmp/bad-variant-policy.tsv"
expect_status bad-policy-variant 3 'invalid policy variant: bad-variant' env \
    CGF_TORTURE_TRIAGE_POLICY="$tmp/bad-variant-policy.tsv" \
    "$triage" --output "$tmp/bad-variant-policy.md" "$fixtures/results-a.tsv"

expect_status unresolved 1 '4 failure bucket(s) lack durable policy decisions' \
    env CGF_TORTURE_TRIAGE_POLICY="$fixtures/policy-empty.tsv" \
    "$triage" --output "$tmp/unresolved.md" "$fixtures/results-a.tsv"
grep -F -- '- Disposition: `UNRESOLVED`' "$tmp/unresolved.md" >/dev/null ||
    fail "unknown cluster did not render UNRESOLVED"
grep -F 'UNRESOLVED — no durable policy hypothesis exists' \
    "$tmp/unresolved.md" >/dev/null || fail "unknown hypothesis was not explicit"
grep -F -- '- Unresolved buckets: 4' "$tmp/unresolved.md" >/dev/null ||
    fail "unresolved coverage count was omitted"

echo 'must survive unresolved report' >"$tmp/preserved-passing.txt"
echo 'must survive unresolved report' >"$tmp/preserved-passing.expected"
expect_status unresolved-combined 1 '4 failure bucket(s) lack durable policy decisions' \
    env CGF_TORTURE_TRIAGE_POLICY="$fixtures/policy-empty.tsv" \
    "$triage" --emit-passing "$tmp/preserved-passing.txt" \
    --output "$tmp/unresolved-combined.md" "$fixtures/results-a.tsv"
cmp "$tmp/preserved-passing.expected" "$tmp/preserved-passing.txt" ||
    fail "unresolved combined report rewrote the existing passing file"
grep -F -- '- Disposition: `UNRESOLVED`' \
    "$tmp/unresolved-combined.md" >/dev/null ||
    fail "combined failure did not preserve the useful unresolved report"

# The committed combined pass list is checked only for targets represented by
# this lane, allowing x86 PR and arm64 nightly streams to gate independently.
# shellcheck disable=SC2086
"$triage" --gate "$fixtures/passing.txt" $results
"$triage" --gate "$fixtures/passing.txt" "$fixtures/results-a.tsv" \
    "$fixtures/results-b.tsv"
"$triage" --gate "$fixtures/passing.txt" "$fixtures/results-arm.tsv"

grep -v '^ctestsuite/00001.c@' "$fixtures/passing.txt" >"$tmp/missing-pass.txt"
expect_status new-pass 1 'new pass: observed PASS key is not committed: ctestsuite/00001.c@O0@x86_64-linux-gnu' \
    "$triage" --gate "$tmp/missing-pass.txt" "$fixtures/results-a.tsv" \
    "$fixtures/results-b.tsv"
{
    sed -n '1,3p' "$fixtures/passing.txt"
    sed -n '4,$p' "$fixtures/passing.txt"
    echo 'torture-execute/runbad.c@O0@x86_64-linux-gnu'
} >"$tmp/extra-pass.unsorted"
{
    sed -n '1,3p' "$tmp/extra-pass.unsorted"
    sed -n '4,$p' "$tmp/extra-pass.unsorted" | sort
} >"$tmp/extra-pass.txt"
expect_status regression 1 'regression: expected PASS key is not PASS: torture-execute/runbad.c@O0@x86_64-linux-gnu' \
    "$triage" --gate "$tmp/extra-pass.txt" "$fixtures/results-a.tsv" \
    "$fixtures/results-b.tsv"

expect_status duplicate 3 'duplicate result key: torture-compile/alpha.c@O0@x86_64-linux-gnu' \
    "$triage" --output "$tmp/duplicate.md" "$fixtures/results-a.tsv" \
    "$fixtures/duplicate.tsv"
expect_status malformed 3 'expected 10 tab-separated fields, got 9' \
    "$triage" --output "$tmp/malformed.md" "$fixtures/malformed.tsv"
expect_status preamble 3 'expected exact tab-separated columns header' \
    "$triage" --output "$tmp/preamble.md" "$fixtures/wrong-preamble.tsv"
expect_status empty 3 'result streams contain zero TSV rows' \
    "$triage" --output "$tmp/empty.md" "$fixtures/header-only.tsv"

sed '1s/results-v2/results-v1/' "$fixtures/results-a.tsv" >"$tmp/v1-header.tsv"
expect_status v1-header 3 'expected # cgf-torture-results-v2 header' \
    "$triage" --output "$tmp/v1-header.md" "$tmp/v1-header.tsv"
sed '3s/1111111111111111111111111111111111111111/ABC/' \
    "$fixtures/results-a.tsv" >"$tmp/bad-revision.tsv"
expect_status bad-revision 3 'source-revision must be 40 or 64 lowercase hexadecimal digits, or unversioned' \
    "$triage" --output "$tmp/bad-revision.md" "$tmp/bad-revision.tsv"
sed '5s/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/abc/' \
    "$fixtures/results-a.tsv" >"$tmp/bad-sha.tsv"
expect_status bad-sha 3 'harness-sha256 must be 64 lowercase hexadecimal digits' \
    "$triage" --output "$tmp/bad-sha.md" "$tmp/bad-sha.tsv"
sed '3s/1111111111111111111111111111111111111111/unversioned/' \
    "$fixtures/results-arm.tsv" >"$tmp/unversioned.tsv"
CGF_TORTURE_TRIAGE_POLICY=$fixtures/policy.tsv \
    "$triage" --output "$tmp/unversioned.md" "$tmp/unversioned.tsv"
grep -F -- '- source-revision: `unversioned`' "$tmp/unversioned.md" >/dev/null ||
    fail "unversioned source revision was not accepted and rendered"
sed '3s/1111111111111111111111111111111111111111/1111111111111111111111111111111111111111111111111111111111111111/' \
    "$fixtures/results-arm.tsv" >"$tmp/revision64.tsv"
CGF_TORTURE_TRIAGE_POLICY=$fixtures/policy.tsv \
    "$triage" --output "$tmp/revision64.md" "$tmp/revision64.tsv"

sed '5s/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/abababababababababababababababababababababababababababababababab/' \
    "$fixtures/results-b.tsv" >"$tmp/provenance-mismatch.tsv"
expect_status provenance-mismatch 3 'shared provenance mismatch' \
    "$triage" --output "$tmp/provenance-mismatch.md" \
    "$fixtures/results-a.tsv" "$tmp/provenance-mismatch.tsv"
sed '8s/x86_64-linux-gnu/arm64-linux/' "$fixtures/results-a.tsv" \
    >"$tmp/row-target-mismatch.tsv"
expect_status row-target-mismatch 3 'row target does not match header target: x86_64-linux-gnu' \
    "$triage" --output "$tmp/row-target-mismatch.md" "$tmp/row-target-mismatch.tsv"

echo 'must survive malformed input' >"$tmp/preserved-report.md"
echo 'must survive malformed input' >"$tmp/preserved-report.expected.md"
expect_status preserve-report 3 'expected 10 tab-separated fields, got 9' \
    "$triage" --output "$tmp/preserved-report.md" "$fixtures/malformed.tsv"
cmp "$tmp/preserved-report.expected.md" "$tmp/preserved-report.md" ||
    fail "validation failure replaced the existing report"

sed 's/\tSIGNAL\t11\t/\tSIGNAL\tSIGSEGV\t/' "$fixtures/results-b.tsv" \
    >"$tmp/bad-signal.tsv"
expect_status signal 3 'SIGNAL requires canonical decimal signal 1..127' \
    "$triage" --output "$tmp/signal.md" "$tmp/bad-signal.tsv"
sed 's/\tOUTPUT_FAIL\t/\tBAD_OUTCOME\t/' "$fixtures/results-b.tsv" \
    >"$tmp/bad-outcome.tsv"
expect_status outcome 3 'invalid outcome: BAD_OUTCOME' \
    "$triage" --output "$tmp/outcome.md" "$tmp/bad-outcome.tsv"

{
    sed -n '1,3p' "$fixtures/passing.txt"
    sed -n '5p' "$fixtures/passing.txt"
    sed -n '4p' "$fixtures/passing.txt"
    sed -n '6,$p' "$fixtures/passing.txt"
} >"$tmp/unsorted-passing.txt"
expect_status unsorted 3 'passing keys are not LC_ALL=C sorted' \
    "$triage" --gate "$tmp/unsorted-passing.txt" "$fixtures/results-a.tsv"
{
    sed -n '1,4p' "$fixtures/passing.txt"
    sed -n '4,$p' "$fixtures/passing.txt"
} >"$tmp/duplicate-passing.txt"
expect_status duplicate-passing 3 'duplicate passing key' \
    "$triage" --gate "$tmp/duplicate-passing.txt" "$fixtures/results-a.tsv"

sed 's/\[tags=arithmetic,integer\]/[tags=integer,arithmetic]/' \
    "$fixtures/results-b.tsv" >"$tmp/unsorted-tags.tsv"
expect_status unsorted-tags 3 'tags metadata is not sorted unique' \
    "$triage" --output "$tmp/unsorted-tags.md" "$tmp/unsorted-tags.tsv"

sed 's/xfail:TORT-055 //' "$fixtures/results-b.tsv" >"$tmp/missing-xfail-id.tsv"
expect_status missing-xfail-id 3 'XFAIL row requires exactly one TORT-NNN metadata ID' \
    "$triage" --output "$tmp/missing-xfail-id.md" "$tmp/missing-xfail-id.tsv"

echo 'torture_triage_test: 49 triage/ratchet/policy/provenance cases passed'
