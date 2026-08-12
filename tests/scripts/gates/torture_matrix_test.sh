#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
fixtures=$repo/tests/scripts/gates/fixtures/torture-matrix
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-torture-matrix-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
tab=$(printf '\t')

fail()
{
    echo "torture_matrix_test: $*" >&2
    exit 1
}

FAKE_TORTURE_LOG=$tmp/runner.log
FAKE_TRIAGE_LOG=$tmp/triage.log
FAKE_IMPORT_LOG=$tmp/import.log
FAKE_PROVENANCE_STATE_FILE=$tmp/provenance.state
export FAKE_TORTURE_LOG FAKE_TRIAGE_LOG FAKE_IMPORT_LOG
export FAKE_PROVENANCE_STATE_FILE
printf 'a\n' >"$FAKE_PROVENANCE_STATE_FILE"
printf 'fixture receipt\n' >"$tmp/fake.receipt"

{
    echo '# cgf-torture-triage-policy-v1'
    printf '# columns=signal\tfingerprint\tphase\tvariant\thypothesis\tdisposition\n'
} >"$tmp/triage-policy.tsv"
: >"$tmp/torture-policy.tsv"
: >"$tmp/ctestsuite-policy.tsv"

run_make()
{
    make --no-print-directory -f "$repo/ci/torture.mk" \
        CGF_TORTURE_CC="$fixtures/fake-compiler.sh" \
        CGF_TORTURE_PROVENANCE_CC="$fixtures/fake-compiler.sh" \
        CGF_TORTURE_PROVENANCE_RECEIPT="$tmp/fake.receipt" \
        CGF_TORTURE_PROVENANCE="$fixtures/fake-provenance.sh" \
        CGF_TORTURE_RUNNER="$fixtures/fake-runner.sh" \
        CGF_TORTURE_TRIAGE_TOOL="$fixtures/fake-triage.sh" \
        CGF_TORTURE_IMPORT="$fixtures/fake-import-torture.sh" \
        CGF_CTESTSUITE_IMPORT="$fixtures/fake-import-ctestsuite.sh" \
        CGF_TORTURE_MANIFEST="$fixtures/torture.MANIFEST" \
        CGF_CTESTSUITE_MANIFEST="$fixtures/ctestsuite.MANIFEST" \
        CGF_TORTURE_POLICY="$tmp/torture-policy.tsv" \
        CGF_CTESTSUITE_POLICY="$tmp/ctestsuite-policy.tsv" \
        CGF_TORTURE_TRIAGE_POLICY="$tmp/triage-policy.tsv" \
        "$@"
}

emit_fixture_provenance()
{
    fixture_target=$1
    "$fixtures/fake-provenance.sh" \
        --receipt "$tmp/fake.receipt" \
        --driver "$fixtures/fake-compiler.sh" \
        --compiler "$fixtures/fake-compiler.sh" \
        --runner "$fixtures/fake-runner.sh" \
        --target "$fixture_target" \
        --torture-manifest "$fixtures/torture.MANIFEST" \
        --ctestsuite-manifest "$fixtures/ctestsuite.MANIFEST"
}

write_fixture_results()
{
    result_target=$1
    result_output=$2
    : >"$tmp/result-rows"
    for result_suite in torture-compile torture-execute \
        torture-execute-ieee ctestsuite; do
        for result_level in O0 O1 O2 O3 Os; do
            result_outcome=PASS
            result_phase=run
            result_detail=-
            if [ "$result_suite" = torture-execute ]; then
                result_outcome=SKIP
                result_phase=policy
                result_detail='fixture non-PASS row'
            fi
            printf '%s/file.c@%s@%s\t%s\tfile.c\t%s\t%s\t%s\t-\t-\t%s\t%s\n' \
                "$result_suite" "$result_level" "$result_target" \
                "$result_suite" "$result_level" "$result_target" \
                "$result_outcome" "$result_phase" "$result_detail" \
                >>"$tmp/result-rows"
        done
    done
    {
        echo '# cgf-torture-results-v2'
        printf '# columns=key\tsuite\tfile\tlevel\ttarget\toutcome\tsignal\tfingerprint\tphase\tdetail\n'
        emit_fixture_provenance "$result_target"
        LC_ALL=C sort "$tmp/result-rows"
    } >"$result_output"
}

# Exercise the real provenance helper separately from the fast orchestration
# fake: a receipt binds the exact compiler binary and becomes invalid after a
# byte change.
cp "$fixtures/fake-compiler.sh" "$tmp/real-compiler"
chmod 755 "$tmp/real-compiler"
"$repo/scripts/torture-provenance.sh" --write-receipt "$tmp/real.receipt" \
    --compiler "$tmp/real-compiler"
"$repo/scripts/torture-provenance.sh" \
    --receipt "$tmp/real.receipt" --driver "$tmp/real-compiler" \
    --compiler "$tmp/real-compiler" --runner "$fixtures/fake-runner.sh" \
    --target x86_64-linux-gnu \
    --torture-manifest "$fixtures/torture.MANIFEST" \
    --ctestsuite-manifest "$fixtures/ctestsuite.MANIFEST" \
    >"$tmp/real-provenance"
[ "$(wc -l <"$tmp/real-provenance" | tr -d ' ')" -eq 8 ] ||
    fail 'real provenance helper did not emit the fixed eight-line block'
grep -Eq '^# compiler-source-sha256=[0-9a-f]{64}$' "$tmp/real-provenance" ||
    fail 'real provenance helper emitted an invalid source digest'
printf '\n# changed binary\n' >>"$tmp/real-compiler"
status=0
"$repo/scripts/torture-provenance.sh" \
    --receipt "$tmp/real.receipt" --driver "$tmp/real-compiler" \
    --compiler "$tmp/real-compiler" --runner "$fixtures/fake-runner.sh" \
    --target x86_64-linux-gnu \
    --torture-manifest "$fixtures/torture.MANIFEST" \
    --ctestsuite-manifest "$fixtures/ctestsuite.MANIFEST" \
    >"$tmp/stale-receipt.out" 2>"$tmp/stale-receipt.err" || status=$?
[ "$status" -ne 0 ] || fail 'changed compiler was accepted with a stale receipt'
grep -F 'compiler provenance receipt is stale or tampered' \
    "$tmp/stale-receipt.err" >/dev/null ||
    fail 'stale compiler receipt rejection lacked a diagnostic'

: >"$FAKE_TORTURE_LOG"
full_results=$tmp/full-results.txt
run_make torture-run BUILD="$tmp/full-build" \
    CGF_TORTURE_RESULTS="$full_results" \
    CGF_TORTURE_WORK="$tmp/full-work"

[ "$(wc -l <"$FAKE_TORTURE_LOG" | tr -d ' ')" -eq 20 ] ||
    fail 'default matrix did not invoke all 4 suites at all 5 levels'
: >"$tmp/expected-cells"
for suite in torture-compile torture-execute torture-execute-ieee ctestsuite; do
    case $suite in
        ctestsuite) manifest=$fixtures/ctestsuite.MANIFEST ;;
        *) manifest=$fixtures/torture.MANIFEST ;;
    esac
    for level in O0 O1 O2 O3 Os; do
        printf '%s|%s|x86_64-linux-gnu|%s\n' "$suite" "$level" \
            "$manifest" >>"$tmp/expected-cells"
    done
done
cut -d '|' -f 1-4 "$FAKE_TORTURE_LOG" >"$tmp/actual-cells"
cmp "$tmp/expected-cells" "$tmp/actual-cells" >/dev/null ||
    fail 'default suite/level/target/manifest call matrix differed'
[ "$(cut -d '|' -f 5 "$FAKE_TORTURE_LOG" | sort -u | wc -l | tr -d ' ')" -eq 20 ] ||
    fail 'matrix cells shared output files'
[ "$(cut -d '|' -f 6 "$FAKE_TORTURE_LOG" | sort -u | wc -l | tr -d ' ')" -eq 20 ] ||
    fail 'matrix cells shared work directories'
[ "$(sed -n '1p' "$full_results")" = '# cgf-torture-results-v2' ] ||
    fail 'merged results lacked the v2 header'
[ "$(sed -n '8p' "$full_results")" = '# target=x86_64-linux-gnu' ] ||
    fail 'merged results provenance target was wrong'
sed -n '11,$p' "$full_results" >"$tmp/full-rows"
LC_ALL=C sort -cu "$tmp/full-rows" ||
    fail 'merged rows were not LC_ALL=C sorted and unique'
[ "$(wc -l <"$tmp/full-rows" | tr -d ' ')" -eq 20 ] ||
    fail 'merged results lost matrix rows'
awk 'BEGIN { FS = sprintf("%c", 9) } NF != 10 { exit 1 }' \
    "$tmp/full-rows" || fail 'merged results were not ten-field TSV rows'

: >"$FAKE_TORTURE_LOG"
run_make torture-run BUILD="$tmp/override-build" \
    CGF_TORTURE_LEVELS='O0 Os' CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/override-results.txt" \
    CGF_TORTURE_WORK="$tmp/override-work"
[ "$(wc -l <"$FAKE_TORTURE_LOG" | tr -d ' ')" -eq 8 ] ||
    fail 'level override did not produce a 4x2 matrix'
[ "$(cut -d '|' -f 3 "$FAKE_TORTURE_LOG" | sort -u)" = arm64-linux ] ||
    fail 'target override did not reach every runner call'

: >"$FAKE_TORTURE_LOG"
run_make torture-run BUILD="$tmp/default-build" CGF_TORTURE_LEVELS=O0 \
    CGF_TORTURE_WORK="$tmp/default-work"
[ -f "$tmp/default-build/torture/results-x86_64-linux-gnu-v2.txt" ] ||
    fail 'default v2 results filename was not target-qualified'

# Runner and provenance failures must leave an existing published result
# byte-identical.
printf 'existing result sentinel\n' >"$tmp/fail-results.txt"
cp "$tmp/fail-results.txt" "$tmp/fail-results.before"
status=0
(
    FAKE_FAIL_CELL=torture-compile/O1
    export FAKE_FAIL_CELL
    run_make torture-run CGF_TORTURE_RESULTS="$tmp/fail-results.txt" \
        CGF_TORTURE_WORK="$tmp/fail-work"
) >"$tmp/fail.out" 2>"$tmp/fail.err" || status=$?
[ "$status" -ne 0 ] || fail 'runner failure did not fail torture-run'
cmp "$tmp/fail-results.before" "$tmp/fail-results.txt" >/dev/null ||
    fail 'runner failure replaced the previously published result'

: >"$FAKE_TORTURE_LOG"
printf 'provenance sentinel\n' >"$tmp/provenance-fail-results.txt"
cp "$tmp/provenance-fail-results.txt" "$tmp/provenance-fail.before"
status=0
(
    FAKE_PROVENANCE_FAIL=1
    export FAKE_PROVENANCE_FAIL
    run_make torture-run CGF_TORTURE_LEVELS=O0 \
        CGF_TORTURE_RESULTS="$tmp/provenance-fail-results.txt" \
        CGF_TORTURE_WORK="$tmp/provenance-fail-work"
) >"$tmp/provenance-fail.out" 2>"$tmp/provenance-fail.err" || status=$?
[ "$status" -ne 0 ] || fail 'provenance preflight failure was accepted'
[ ! -s "$FAKE_TORTURE_LOG" ] ||
    fail 'provenance preflight failure invoked the matrix runner'
cmp "$tmp/provenance-fail.before" "$tmp/provenance-fail-results.txt" >/dev/null ||
    fail 'provenance preflight failure replaced the published result'

printf 'a\n' >"$FAKE_PROVENANCE_STATE_FILE"
printf 'mid-run sentinel\n' >"$tmp/mid-run-results.txt"
cp "$tmp/mid-run-results.txt" "$tmp/mid-run.before"
status=0
(
    FAKE_MUTATE_CELL=torture-compile/O0
    FAKE_MUTATE_FILE=$FAKE_PROVENANCE_STATE_FILE
    FAKE_MUTATE_REPLACE=1
    export FAKE_MUTATE_CELL FAKE_MUTATE_FILE FAKE_MUTATE_REPLACE
    run_make torture-run CGF_TORTURE_LEVELS=O0 \
        CGF_TORTURE_RESULTS="$tmp/mid-run-results.txt" \
        CGF_TORTURE_WORK="$tmp/mid-run-work"
) >"$tmp/mid-run.out" 2>"$tmp/mid-run.err" || status=$?
[ "$status" -ne 0 ] || fail 'mid-matrix provenance change was accepted'
grep -F 'provenance changed while the matrix was running' \
    "$tmp/mid-run.err" >/dev/null ||
    fail 'mid-matrix provenance change lacked a diagnostic'
cmp "$tmp/mid-run.before" "$tmp/mid-run-results.txt" >/dev/null ||
    fail 'mid-matrix provenance change replaced the published result'
printf 'a\n' >"$FAKE_PROVENANCE_STATE_FILE"

status=0
(
    FAKE_OMIT_CELL=torture-execute/O0
    export FAKE_OMIT_CELL
    run_make torture-run CGF_TORTURE_LEVELS=O0 \
        CGF_TORTURE_RESULTS="$tmp/missing-results.txt" \
        CGF_TORTURE_WORK="$tmp/missing-work"
) >"$tmp/missing.out" 2>"$tmp/missing.err" || status=$?
[ "$status" -ne 0 ] || fail 'missing manifest cell was accepted'
grep -F 'missing result key: torture-execute/file.c@O0@x86_64-linux-gnu' \
    "$tmp/missing.err" >/dev/null || fail 'missing cell lacked a diagnostic'

for invalid_kind in outcome signal phase; do
    status=0
    (
        FAKE_INVALID_SCHEMA_CELL=torture-compile/O0
        FAKE_INVALID_SCHEMA_KIND=$invalid_kind
        export FAKE_INVALID_SCHEMA_CELL FAKE_INVALID_SCHEMA_KIND
        run_make torture-run CGF_TORTURE_LEVELS=O0 \
            CGF_TORTURE_RESULTS="$tmp/invalid-$invalid_kind.txt" \
            CGF_TORTURE_WORK="$tmp/invalid-$invalid_kind-work"
    ) >"$tmp/invalid-$invalid_kind.out" \
        2>"$tmp/invalid-$invalid_kind.err" || status=$?
    [ "$status" -ne 0 ] || fail "invalid $invalid_kind row was accepted"
done

status=0
(
    FAKE_DUPLICATE=1
    export FAKE_DUPLICATE
    run_make torture-run CGF_TORTURE_LEVELS=O0 \
        CGF_TORTURE_RESULTS="$tmp/duplicate-results.txt" \
        CGF_TORTURE_WORK="$tmp/duplicate-work"
) >"$tmp/duplicate.out" 2>"$tmp/duplicate.err" || status=$?
[ "$status" -ne 0 ] || fail 'duplicate result cell keys were accepted'
grep -F 'duplicate result cell keys' "$tmp/duplicate.err" >/dev/null ||
    fail 'duplicate rejection lacked a diagnostic'

# A publishable baseline always contains complete, provenance-matched streams
# for both targets.  One-target, stale, truncated, or mismatched attempts must
# preserve both committed artifacts.
write_fixture_results x86_64-linux-gnu "$tmp/x86-results.txt"
printf 'passing sentinel\n' >"$tmp/combined-passing.txt"
printf 'report sentinel\n' >"$tmp/combined-triage.md"
cp "$tmp/combined-passing.txt" "$tmp/combined-passing.before"
cp "$tmp/combined-triage.md" "$tmp/combined-triage.before"
status=0
run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/one-target-arm.txt" \
    CGF_TORTURE_WORK="$tmp/one-target-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
    CGF_TORTURE_TRIAGE="$tmp/combined-triage.md" \
    >"$tmp/one-target.out" 2>"$tmp/one-target.err" || status=$?
[ "$status" -ne 0 ] || fail 'initial one-target baseline was accepted'
grep -F 'require exactly arm64-linux and x86_64-linux-gnu' \
    "$tmp/one-target.err" >/dev/null ||
    fail 'one-target rejection lacked a diagnostic'
cmp "$tmp/combined-passing.before" "$tmp/combined-passing.txt" >/dev/null ||
    fail 'one-target rejection changed the passing ratchet'
cmp "$tmp/combined-triage.before" "$tmp/combined-triage.md" >/dev/null ||
    fail 'one-target rejection changed the triage report'

status=0
run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/fresh-omitted-arm.txt" \
    CGF_TORTURE_BASELINE_RESULTS="$tmp/x86-results.txt" \
    CGF_TORTURE_WORK="$tmp/fresh-omitted-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
    CGF_TORTURE_TRIAGE="$tmp/combined-triage.md" \
    >"$tmp/fresh-omitted.out" 2>"$tmp/fresh-omitted.err" || status=$?
[ "$status" -ne 0 ] || fail 'explicit baseline omitted the fresh result'
grep -F 'explicit baseline results omit freshly generated result' \
    "$tmp/fresh-omitted.err" >/dev/null ||
    fail 'fresh-result omission lacked a diagnostic'

: >"$FAKE_TRIAGE_LOG"
run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/arm-results.txt" \
    CGF_TORTURE_BASELINE_RESULTS="$tmp/x86-results.txt $tmp/arm-results.txt" \
    CGF_TORTURE_WORK="$tmp/combined-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
    CGF_TORTURE_TRIAGE="$tmp/combined-triage.md"
[ "$(grep -c '^publish|' "$FAKE_TRIAGE_LOG")" -eq 1 ] ||
    fail 'combined baseline did not invoke one atomic triage publication'
grep -F '@x86_64-linux-gnu' "$tmp/combined-passing.txt" >/dev/null ||
    fail 'combined baseline omitted x86_64 passes'
grep -F '@arm64-linux' "$tmp/combined-passing.txt" >/dev/null ||
    fail 'combined baseline omitted arm64 passes'
grep -F '# fixture triage' "$tmp/combined-triage.md" >/dev/null ||
    fail 'combined baseline omitted the triage report'

cp "$tmp/combined-passing.txt" "$tmp/combined-passing.good"
cp "$tmp/combined-triage.md" "$tmp/combined-triage.good"

# Complementary partial streams for one target must not be unioned into a
# counterfeit complete stream, even when their combined keys are complete.
{
    sed -n '1,10p' "$tmp/x86-results.txt"
    awk -F "$tab" '$4 == "O0" || $4 == "O1"' "$tmp/x86-results.txt"
} >"$tmp/x86-part-a.txt"
{
    sed -n '1,10p' "$tmp/x86-results.txt"
    awk -F "$tab" '$4 == "O2" || $4 == "O3" || $4 == "Os"' \
        "$tmp/x86-results.txt"
} >"$tmp/x86-part-b.raw"
sed '9c\# compiler-binary-sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff' \
    "$tmp/x86-part-b.raw" >"$tmp/x86-part-b.txt"
status=0
run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/duplicate-target-arm.txt" \
    CGF_TORTURE_BASELINE_RESULTS="$tmp/x86-part-a.txt $tmp/x86-part-b.txt $tmp/duplicate-target-arm.txt" \
    CGF_TORTURE_WORK="$tmp/duplicate-target-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
    CGF_TORTURE_TRIAGE="$tmp/combined-triage.md" \
    >"$tmp/duplicate-target.out" 2>"$tmp/duplicate-target.err" || status=$?
[ "$status" -ne 0 ] || fail 'complementary duplicate-target streams were accepted'
grep -F 'duplicate baseline result target: x86_64-linux-gnu' \
    "$tmp/duplicate-target.err" >/dev/null ||
    fail 'duplicate target stream rejection lacked a diagnostic'
cmp "$tmp/combined-passing.good" "$tmp/combined-passing.txt" >/dev/null ||
    fail 'duplicate target stream changed the passing ratchet'
cmp "$tmp/combined-triage.good" "$tmp/combined-triage.md" >/dev/null ||
    fail 'duplicate target stream changed the report'

status=0
run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/output-alias-arm.txt" \
    CGF_TORTURE_BASELINE_RESULTS="$tmp/x86-results.txt $tmp/output-alias-arm.txt" \
    CGF_TORTURE_WORK="$tmp/output-alias-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
    CGF_TORTURE_TRIAGE="$tmp/./output-alias-arm.txt" \
    >"$tmp/output-alias.out" 2>"$tmp/output-alias.err" || status=$?
[ "$status" -ne 0 ] || fail 'baseline output/result alias was accepted'
grep -F 'baseline output aliases result input:' "$tmp/output-alias.err" >/dev/null ||
    fail 'baseline output/result alias lacked a diagnostic'
[ "$(sed -n '1p' "$tmp/output-alias-arm.txt")" = '# cgf-torture-results-v2' ] ||
    fail 'baseline output/result alias destroyed the fresh result evidence'
cmp "$tmp/combined-passing.good" "$tmp/combined-passing.txt" >/dev/null ||
    fail 'baseline output/result alias changed the passing ratchet'

status=0
(
    FAKE_TRIAGE_FAIL=after-output
    export FAKE_TRIAGE_FAIL
    run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
        CGF_TORTURE_RESULTS="$tmp/atomic-arm.txt" \
        CGF_TORTURE_BASELINE_RESULTS="$tmp/x86-results.txt $tmp/atomic-arm.txt" \
        CGF_TORTURE_WORK="$tmp/atomic-work" \
        CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
        CGF_TORTURE_TRIAGE="$tmp/combined-triage.md"
) >"$tmp/atomic.out" 2>"$tmp/atomic.err" || status=$?
[ "$status" -ne 0 ] || fail 'staged triage failure was accepted'
cmp "$tmp/combined-passing.good" "$tmp/combined-passing.txt" >/dev/null ||
    fail 'staged triage failure changed the passing ratchet'
cmp "$tmp/combined-triage.good" "$tmp/combined-triage.md" >/dev/null ||
    fail 'staged triage failure changed the report'

sed '5c\# harness-sha256=9999999999999999999999999999999999999999999999999999999999999999' \
    "$tmp/x86-results.txt" >"$tmp/mismatched-x86.txt"
status=0
run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/mismatch-arm.txt" \
    CGF_TORTURE_BASELINE_RESULTS="$tmp/mismatched-x86.txt $tmp/mismatch-arm.txt" \
    CGF_TORTURE_WORK="$tmp/mismatch-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
    CGF_TORTURE_TRIAGE="$tmp/combined-triage.md" \
    >"$tmp/mismatch.out" 2>"$tmp/mismatch.err" || status=$?
[ "$status" -ne 0 ] || fail 'mismatched common provenance was accepted'
grep -F 'mismatched common provenance' "$tmp/mismatch.err" >/dev/null ||
    fail 'mismatched provenance lacked a diagnostic'
cmp "$tmp/combined-passing.good" "$tmp/combined-passing.txt" >/dev/null ||
    fail 'mismatched provenance changed the passing ratchet'

sed -n '1,11p' "$tmp/x86-results.txt" >"$tmp/truncated-x86.txt"
status=0
run_make torture-baseline CGF_TORTURE_TARGET=arm64-linux \
    CGF_TORTURE_RESULTS="$tmp/truncated-arm.txt" \
    CGF_TORTURE_BASELINE_RESULTS="$tmp/truncated-x86.txt $tmp/truncated-arm.txt" \
    CGF_TORTURE_WORK="$tmp/truncated-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt" \
    CGF_TORTURE_TRIAGE="$tmp/combined-triage.md" \
    >"$tmp/truncated.out" 2>"$tmp/truncated.err" || status=$?
[ "$status" -ne 0 ] || fail 'truncated cross-target result was accepted'
grep -F 'baseline result keys do not match the complete five-level matrix' \
    "$tmp/truncated.err" >/dev/null ||
    fail 'truncated result lacked a completeness diagnostic'

: >"$FAKE_TRIAGE_LOG"
run_make torture-gate CGF_TORTURE_LEVELS=O0 \
    CGF_TORTURE_RESULTS="$tmp/gate-results.txt" \
    CGF_TORTURE_WORK="$tmp/gate-work" \
    CGF_TORTURE_PASSING="$tmp/combined-passing.txt"
grep -F "gate|$tmp/combined-passing.txt|$tmp/gate-results.txt" \
    "$FAKE_TRIAGE_LOG" >/dev/null || fail 'gate compared the wrong artifacts'

status=0
(
    FAKE_TRIAGE_FAIL=gate
    export FAKE_TRIAGE_FAIL
    run_make torture-gate CGF_TORTURE_LEVELS=O0 \
        CGF_TORTURE_RESULTS="$tmp/gate-fail-results.txt" \
        CGF_TORTURE_WORK="$tmp/gate-fail-work" \
        CGF_TORTURE_PASSING="$tmp/combined-passing.txt"
) >"$tmp/gate-fail.out" 2>"$tmp/gate-fail.err" || status=$?
[ "$status" -ne 0 ] || fail 'triage gate failure did not propagate'

: >"$FAKE_IMPORT_LOG"
run_make torture-import-verify
printf 'torture|--verify\ntorture-policy|%s\nctestsuite|--verify\nctestsuite-policy|%s\n' \
    "$tmp/torture-policy.tsv" "$tmp/ctestsuite-policy.tsv" \
    >"$tmp/expected-import.log"
cmp "$tmp/expected-import.log" "$FAKE_IMPORT_LOG" >/dev/null ||
    fail 'import verification did not call both importers'

: >"$FAKE_IMPORT_LOG"
run_make torture-import
printf 'torture|import\ntorture-policy|%s\nctestsuite|import\nctestsuite-policy|%s\n' \
    "$tmp/torture-policy.tsv" "$tmp/ctestsuite-policy.tsv" \
    >"$tmp/expected-import.log"
cmp "$tmp/expected-import.log" "$FAKE_IMPORT_LOG" >/dev/null ||
    fail 'torture-import did not call both importers'

mv "$tmp/triage-policy.tsv" "$tmp/triage-policy.saved"
status=0
run_make torture-import-verify >"$tmp/missing-policy.out" \
    2>"$tmp/missing-policy.err" || status=$?
[ "$status" -ne 0 ] || fail 'missing triage policy was accepted'
grep -F 'triage policy is not a readable regular file' \
    "$tmp/missing-policy.err" >/dev/null ||
    fail 'missing triage policy lacked a diagnostic'
mv "$tmp/triage-policy.saved" "$tmp/triage-policy.tsv"

cp "$tmp/triage-policy.tsv" "$tmp/triage-policy.valid"
printf '# wrong-policy-schema\n' >"$tmp/triage-policy.tsv"
status=0
run_make torture-import-verify >"$tmp/malformed-policy.out" \
    2>"$tmp/malformed-policy.err" || status=$?
[ "$status" -ne 0 ] || fail 'malformed triage policy was accepted'
grep -F 'malformed triage policy' "$tmp/malformed-policy.err" >/dev/null ||
    fail 'malformed triage policy lacked a diagnostic'
mv "$tmp/triage-policy.valid" "$tmp/triage-policy.tsv"

echo 'torture_matrix_test: provenance, matrix, merge, gate, baseline, and import cases passed'
