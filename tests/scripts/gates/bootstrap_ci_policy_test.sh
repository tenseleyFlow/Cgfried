#!/bin/sh

set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bootstrap-ci-policy-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "bootstrap ci policy test FAIL: $*" >&2
    exit 1
}

expect_rejection()
{
    rejection_expected=$1
    rejection_workflow=$2
    rejection_contract=$3
    rejection_output=$4
    if CGF_BOOTSTRAP_WORKFLOW="$rejection_workflow" \
        CGF_BOOTSTRAP_CONTRACT="$rejection_contract" \
        sh "$repo/scripts/check_bootstrap_ci_policy.sh" \
        >"$rejection_output" 2>&1; then
        fail "policy accepted $rejection_expected"
    fi
    grep -F "$rejection_expected" "$rejection_output" >/dev/null || {
        cat "$rejection_output" >&2
        fail "policy did not explain: $rejection_expected"
    }
}

workflow=$repo/.github/workflows/bootstrap.yml
contract=$repo/ci/bootstrap.yml

CGF_BOOTSTRAP_WORKFLOW="$workflow" CGF_BOOTSTRAP_CONTRACT="$contract" \
    sh "$repo/scripts/check_bootstrap_ci_policy.sh" >"$tmp/baseline.out"
grep -Fx \
    'bootstrap-ci-policy: daily matching-head hosted lanes and weekly probes verified' \
    "$tmp/baseline.out" >/dev/null || fail "baseline success was not exact"

sed "/github.event.schedule == '17 3 \* \* \*'/d" "$workflow" \
    >"$tmp/no-daily-x86.yml"
expect_rejection 'daily cron does not launch x86 O0/O2' \
    "$tmp/no-daily-x86.yml" "$contract" "$tmp/no-daily-x86.out"

sed 's/runs-on: ubuntu-24.04$/runs-on: self-hosted/' "$workflow" \
    >"$tmp/self-hosted.yml"
expect_rejection 'bootstrap soak must not depend on a self-hosted runner' \
    "$tmp/self-hosted.yml" "$contract" "$tmp/self-hosted.out"

sed 's/daily_commit_identity: same-workflow-github-sha/daily_commit_identity: unrelated-runs/' \
    "$contract" >"$tmp/unmatched-contract.yml"
expect_rejection 'contract does not require a matching daily commit' \
    "$workflow" "$tmp/unmatched-contract.yml" "$tmp/unmatched-contract.out"

sed 's/fleet_dependency: none/fleet_dependency: nomad-1/' "$contract" \
    >"$tmp/fleet-contract.yml"
expect_rejection 'contract permits a fleet-host dependency' \
    "$workflow" "$tmp/fleet-contract.yml" "$tmp/fleet-contract.out"

echo "bootstrap ci policy test: baseline and four fail-closed mutations PASS"
