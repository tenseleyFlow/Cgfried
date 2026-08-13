#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

usage() {
    echo "usage: $0 PROJECT VARIANT EXPECTED ACTUAL" >&2
    exit 2
}

fail() {
    echo "campaign-capture-result: $*" >&2
    exit 1
}

[ "$#" -eq 4 ] || usage
project=$1
variant=$2
expected=$3
actual=$4

case $project in
    '' | *[!a-z0-9-]*) fail "invalid project: $project" ;;
esac
case $variant in
    '' | *[!a-z0-9._-]*) fail "invalid variant: $variant" ;;
esac
case $expected in
    ci/campaigns/*.expected) ;;
    *) fail "expected path must name ci/campaigns/*.expected: $expected" ;;
esac

root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
checker=$root/scripts/campaign-check.sh
[ -r "$root/$expected" ] || fail "expected file is not readable: $expected"

report_root=${CGF_CAMPAIGN_REPORT_ROOT:-$root/build/campaign-reports}
case $report_root in /*) ;; *) report_root=$root/$report_root ;; esac
report_dir=$report_root/$variant
mkdir -p "$report_root"
[ ! -L "$report_root" ] || fail "report root must not be a symlink"
[ ! -L "$report_dir" ] || fail "report directory must not be a symlink"
mkdir -p "$report_dir"

result=$report_dir/results.txt
[ ! -L "$result" ] || fail "result destination must not be a symlink"
[ ! -L "$report_dir/metadata.txt" ] ||
    fail "metadata destination must not be a symlink"
if [ -r "$actual" ] && "$checker" "$actual" "$actual" >/dev/null 2>&1; then
    cp "$actual" "$result"
    source=campaign
else
    # A build can die before its runner publishes a result. Capture that
    # absence as a valid, deliberately failing row so the isolated reporter
    # can page the ledger without granting issue-write authority to builds.
    # tests/scripts/campaign_reporting_test.sh pins this fail-closed path.
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        printf 'campaign.execution\tFAIL\tclassification-required,variant=%s\n' \
            "$variant"
    } >"$result"
    source=synthetic
fi

if "$checker" "$root/$expected" "$result" >/dev/null 2>&1; then
    state=match
else
    state=drift
fi
{
    printf 'project=%s\n' "$project"
    printf 'variant=%s\n' "$variant"
    printf 'expected=%s\n' "$expected"
    printf 'source=%s\n' "$source"
    printf 'state=%s\n' "$state"
} >"$report_dir/metadata.txt"

printf 'campaign-capture-result: PASS project=%s variant=%s source=%s state=%s\n' \
    "$project" "$variant" "$source" "$state"
