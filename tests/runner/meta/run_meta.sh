#!/bin/sh
# The runner testing itself: fixture "tests" driven by a fake compiler.
# Asserts cgf-test's own exit codes, output, skip discipline, and
# determinism. Usage: sh tests/runner/meta/run_meta.sh build/cgf-test
set -u

RUNNER=${1:?usage: run_meta.sh path/to/cgf-test}
here=tests/runner/meta
CGF_TEST_CC=$here/fake-cc.sh
CGF_TEST_XFAIL_LEDGER=$here/xfail-ledger.md
export CGF_TEST_CC CGF_TEST_XFAIL_LEDGER

fails=0
fail() {
    echo "META FAIL: $*" >&2
    fails=$((fails + 1))
}

# expect <fixture> <expected-exit> <must-contain>
expect() {
    out=$("$RUNNER" --profile meta "$here/$1" 2>&1)
    code=$?
    [ "$code" -eq "$2" ] || fail "$1: exit $code, expected $2"
    case $out in
    *"$3"*) ;;
    *) fail "$1: output missing '$3': $out" ;;
    esac
}

expect check_pass.c 0 "pass=1"
expect check_order.c 1 "FAIL"
expect check_target_pass.c 0 "pass=1"
expect config_check_target_mixed.c 1 "bare and target-qualified"
expect config_check_target_unknown.c 1 "unknown target selector"
expect exit_code_pass.c 0 "pass=1"
expect exit_code_fail.c 1 "exit code 3, expected 0"
expect err_expected_pass.c 0 "pass=1"
expect err_expected_fail.c 1 "ERROR_EXPECTED not satisfied"
expect warn_expected_pass.c 0 "pass=1"
expect warn_expected_fail.c 1 "WARNING_EXPECTED not satisfied"
expect warn_expected_errored.c 1 "WARNING_EXPECTED not satisfied"
expect warn_check_pass.c 0 "pass=1"
expect warn_check_bad_line.c 1 "WARN_CHECK not satisfied"
expect warn_check_bad_flag.c 1 "WARN_CHECK not satisfied"
expect warn_check_bad_message.c 1 "WARN_CHECK not satisfied"
expect warn_count_two.c 0 "pass=1"
expect warn_count_driver.c 0 "pass=1"
expect warn_count_format_suffix.c 0 "pass=1"
expect warn_count_bad_equals.c 1 "lacks mandatory [-W<flag>] suffix"
expect warn_count_one.c 1 "WARN_COUNT expected 2, got 1"
expect warn_count_zero.c 0 "pass=1"
expect warn_count_zero_fail.c 1 "WARN_COUNT expected 0, got 1"
expect warn_count_missing_suffix.c 1 "lacks mandatory [-W<flag>] suffix"
expect config_warn_check.c 1 "WARN_CHECK must be"
expect config_warn_count.c 1 "WARN_COUNT must be"
# TU-BREAK: the fixture splits into two TUs; TU0 compiles clean, TU1 fails
# with the expected text — the merged output satisfies ERROR_EXPECTED.
expect tu_break.c 0 "pass=1"
expect xfail_fail.c 0 "xfail=1"
expect xfail_pass.c 1 "XPASS"
expect skip_all.c 0 \
    'HARNESS_SKIP suite=meta test=skip_all count=1 reason="meta skip fixture"'
expect timeout.c 1 "TIMEOUT"
expect signal.c 1 "killed by signal"
expect big_stderr.c 0 "pass=1"
expect binary_out.c 0 "pass=1"
expect config_unknown_directive.c 1 "CONFIG ERROR"
expect config_unknown_selector.c 1 "unknown target selector"
expect config_reserved.c 1 "unknown OPT_EQ level '-Og'"
expect opt_eq_pass.c 0 "pass=1"
# Anti-vacuity: both level-specific runs satisfy CHECK/EXIT_CODE, but their
# full stdout differs, so OPT_EQ itself must fail and name both levels.
expect opt_eq_fail.c 1 "OPT_EQ stdout mismatch: -O0 vs -O1"
expect ofast_tagged_diverge.c 0 "pass=1"
expect ofast_tagged_nondiverge.c 0 "pass=1"
# The first listed level may be -Ofast: comparison still anchors on the first
# strict baseline, never on the licensed result.
expect ofast_tagged_order.c 0 "pass=1"
expect ofast_untagged_diverge.c 1 "OPT_EQ stdout mismatch: -O0 vs -Ofast"
# A tag licenses only -Ofast stdout bytes. Strict-level divergence, compiler
# failure, and runtime failure remain hard failures.
expect ofast_tagged_o3_diverge.c 1 "OPT_EQ stdout mismatch: -O0 vs -O3"
expect ofast_tagged_compile_fail.c 1 "compile exited 1"
expect ofast_tagged_exit_fail.c 1 "exit code 3, expected 0"
expect ofast_config_unknown.c 1 "unknown OFAST_DIVERGENCE_OK reason"
expect ofast_config_duplicate.c 1 "duplicate OFAST_DIVERGENCE_OK directive"
expect ofast_config_without_ofast.c 1 \
    "OFAST_DIVERGENCE_OK requires OPT_EQ containing -Ofast"
expect ir_check_pass.cgfir 0 "pass=1"
expect mir_check_pass.c 0 "pass=1"
# F-S22-MIRCHECK: MIR_CHECK was parsed and silently dropped through all
# of Sprint 21 — an unmatched MIR_CHECK must FAIL, forever.
expect mir_check_fail.c 1 "CHECK not matched"
expect config_bad_xfid.c 1 "XF-NNNN"
expect config_unknown_xfid.c 1 "not in the ledger"
expect flags_env_pp.c 0 "pass=1"
expect flags_dup_config.c 1 "duplicate FLAGS"

# Full-directory sweep: exact summary counts and cross-run determinism.
out1=$("$RUNNER" --profile meta "$here" 2>&1)
code1=$?
[ "$code1" -eq 1 ] || fail "full dir: exit $code1, expected 1"
case $out1 in
*"total=56 pass=20 fail=20 xfail=1 xpass=1 skip=1 config=13"*) ;;
*) fail "full dir: unexpected summary: $(printf '%s' "$out1" | tail -1)" ;;
esac
out2=$("$RUNNER" --profile meta "$here" 2>&1)
[ "$out1" = "$out2" ] || fail "runner output is nondeterministic across runs"

# Skip discipline end-to-end: the correct set passes; an injected extra
# skip, a changed count, and a missing skip must each fail check_skips.
# Log lives next to the runner so BUILD=build-san runs stay independent.
log="$(dirname "$RUNNER")/meta.log"
printf '%s\n' "$out1" >"$log"
sh ci/check_skips.sh meta "$log" >/dev/null ||
    fail "check_skips rejected the correct skip set"
cp "$log" "$log.extra"
echo 'HARNESS_SKIP suite=meta test=ghost count=1 reason="x"' >>"$log.extra"
sh ci/check_skips.sh meta "$log.extra" >/dev/null 2>&1 &&
    fail "check_skips accepted an extra skip"
sed 's/count=1/count=2/' "$log" >"$log.count"
sh ci/check_skips.sh meta "$log.count" >/dev/null 2>&1 &&
    fail "check_skips accepted a changed count"
grep -v '^HARNESS_SKIP ' "$log" >"$log.missing"
sh ci/check_skips.sh meta "$log.missing" >/dev/null 2>&1 &&
    fail "check_skips accepted a missing skip"

if [ "$fails" -ne 0 ]; then
    echo "meta: $fails failure(s)" >&2
    exit 1
fi
echo "meta: all runner self-tests passed"
