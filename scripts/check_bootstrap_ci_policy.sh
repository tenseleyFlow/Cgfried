#!/bin/sh
# Keep Sprint 58's soak automatic, matching-head, and independent of the
# developer/fleet machines.  This intentionally checks the workflow as well as
# its machine-readable contract: documentation alone cannot protect a streak.

set -eu

workflow=${CGF_BOOTSTRAP_WORKFLOW:-.github/workflows/bootstrap.yml}
contract=${CGF_BOOTSTRAP_CONTRACT:-ci/bootstrap.yml}

die()
{
    echo "bootstrap-ci-policy: $*" >&2
    exit 1
}

require_literal()
{
    policy_file=$1
    policy_text=$2
    policy_error=$3
    grep -F -- "$policy_text" "$policy_file" >/dev/null || die "$policy_error"
}

reject_literal()
{
    policy_file=$1
    policy_text=$2
    policy_error=$3
    if grep -F -- "$policy_text" "$policy_file" >/dev/null; then
        die "$policy_error"
    fi
}

[ -r "$workflow" ] || die "workflow is unreadable: $workflow"
[ -r "$contract" ] || die "contract is unreadable: $contract"

policy_tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bootstrap-ci-policy.XXXXXX")
trap 'rm -rf "$policy_tmp"' EXIT HUP INT TERM

extract_job()
{
    job_name=$1
    job_output=$2
    awk -v wanted="$job_name" '
        $0 == "  " wanted ":" { found = 1; print; next }
        found && $0 ~ /^  [A-Za-z0-9_-]+:$/ { exit }
        found { print }
        END { if (!found) exit 2 }
    ' "$workflow" >"$job_output" || die "missing workflow job: $job_name"
}

extract_lane()
{
    lane_name=$1
    lane_output=$2
    awk -v wanted="$lane_name" '
        $0 == "  - id: " wanted { found = 1; print; next }
        found && $0 ~ /^  - id: / { exit }
        found { print }
        END { if (!found) exit 2 }
    ' "$contract" >"$lane_output" || die "missing contract lane: $lane_name"
}

require_literal "$workflow" "- cron: '17 3 * * *'" \
    "daily hosted cron is missing"
require_literal "$workflow" "- cron: '41 3 * * 0'" \
    "weekly hosted cron is missing"
require_literal "$workflow" 'workflow_dispatch:' \
    "hosted recovery dispatch is missing"
reject_literal "$workflow" 'self-hosted' \
    "bootstrap soak must not depend on a self-hosted runner"
reject_literal "$workflow" 'nomad' \
    "bootstrap soak must not depend on Nomad"
reject_literal "$workflow" 'kasumi' \
    "bootstrap soak must not depend on Kasumi"
reject_literal "$workflow" 'hasu' \
    "bootstrap soak must not depend on Hasu"

x86_job=$policy_tmp/x86.job
arm_job=$policy_tmp/arm.job
cross_native_job=$policy_tmp/cross-native.job
cross_x86_job=$policy_tmp/cross-x86.job
cross_compare_job=$policy_tmp/cross-compare.job
repro_step=$policy_tmp/repro.step

extract_job x86_64-linux "$x86_job"
extract_job arm64-linux-native "$arm_job"
extract_job arm64-cross-source "$cross_native_job"
extract_job x86_64-cross-source "$cross_x86_job"
extract_job arm64-cross-compare "$cross_compare_job"

require_literal "$x86_job" "github.event.schedule == '17 3 * * *'" \
    "daily cron does not launch x86 O0/O2"
require_literal "$x86_job" "github.event.schedule == '41 3 * * 0'" \
    "weekly cron does not launch the x86 input lanes"
require_literal "$x86_job" "github.event_name == 'workflow_dispatch'" \
    "recovery dispatch does not launch x86 O0/O2"
require_literal "$x86_job" 'runs-on: ubuntu-24.04' \
    "x86 soak lane is not pinned to the hosted runner pool"
require_literal "$arm_job" "github.event_name == 'schedule'" \
    "scheduled workflow does not launch native arm64 O0/O2"
require_literal "$arm_job" "github.event_name == 'workflow_dispatch'" \
    "recovery dispatch does not launch native arm64 O0/O2"
require_literal "$arm_job" 'runs-on: ubuntu-24.04-arm' \
    "arm64 soak lane is not pinned to the hosted runner pool"

for weekly_job in "$cross_native_job" "$cross_x86_job" "$cross_compare_job"; do
    require_literal "$weekly_job" "github.event.schedule == '41 3 * * 0'" \
        "cross-host probe is not scheduled weekly"
    reject_literal "$weekly_job" "github.event.schedule == '17 3 * * *'" \
        "cross-host probe must remain outside the daily soak run"
    require_literal "$weekly_job" "github.event_name == 'workflow_dispatch'" \
        "recovery dispatch does not launch the full lattice"
done

awk '
    /^[[:space:]]*- id: reproducibility$/ { found = 1 }
    found && /^[[:space:]]*- name: finish evidence manifest$/ { exit }
    found { print }
    END { if (!found) exit 2 }
' "$x86_job" >"$repro_step" || die "missing x86 reproducibility step"
require_literal "$repro_step" "github.event.schedule == '41 3 * * 0'" \
    "x86 reproducibility probe is not scheduled weekly"
require_literal "$repro_step" "github.event_name == 'workflow_dispatch'" \
    "recovery dispatch does not launch x86 reproducibility"
reject_literal "$repro_step" "github.event.schedule == '17 3 * * *'" \
    "x86 reproducibility probe must remain outside the daily soak run"

x86_lane=$policy_tmp/x86.lane
arm_lane=$policy_tmp/arm.lane
cross_lane=$policy_tmp/cross.lane
repro_lane=$policy_tmp/repro.lane
extract_lane x86_64-linux "$x86_lane"
extract_lane arm64-linux-native "$arm_lane"
extract_lane arm64-linux-cross "$cross_lane"
extract_lane x86_64-reproducibility "$repro_lane"

for daily_lane in "$x86_lane" "$arm_lane"; do
    require_literal "$daily_lane" "cadence_utc: '17 3 * * *'" \
        "daily lane cadence disagrees with the workflow"
    require_literal "$daily_lane" 'runner_pool: github-hosted' \
        "daily lane is not declared GitHub-hosted"
    require_literal "$daily_lane" 'required_for_soak_daily: true' \
        "daily lane is not required by the soak contract"
done
for weekly_lane in "$cross_lane" "$repro_lane"; do
    require_literal "$weekly_lane" "cadence_utc: '41 3 * * 0'" \
        "weekly lane cadence disagrees with the workflow"
done

require_literal "$contract" \
    'daily_required_lanes: [x86_64-linux, arm64-linux-native]' \
    "contract does not require both daily architectures"
require_literal "$contract" \
    'daily_commit_identity: same-workflow-github-sha' \
    "contract does not require a matching daily commit"
require_literal "$contract" 'daily_runner_dependency: github-hosted-only' \
    "contract permits a local runner dependency"
require_literal "$contract" 'fleet_dependency: none' \
    "contract permits a fleet-host dependency"
require_literal "$contract" 'recovery_event: workflow_dispatch' \
    "contract does not provide hosted recovery"
require_literal "$contract" 'recovery_deadline: same-utc-date' \
    "contract permits a late recovery to rewrite the streak"
require_literal "$contract" \
    'recovery_scope: hosted-infrastructure-before-bootstrap-only' \
    "contract permits compiler failures to be hidden by a rerun"
require_literal "$contract" 'compiler_or_evidence_failure: resets-streak' \
    "contract does not fail closed on compiler or evidence failures"

echo "bootstrap-ci-policy: daily matching-head hosted lanes and weekly probes verified"
