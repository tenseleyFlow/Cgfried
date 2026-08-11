#!/bin/sh
# Apply a ci/gates.d state to one performance-gate command.
#
# Exit status is the command's status in blocking mode.  Trial and
# report-only modes turn a genuine regression (status 1) into a recorded
# success, but never hide a harness/configuration failure (status >=2).
# Inactive gates are named and skipped without executing the command.
set -eu

prog=perf_gate

die()
{
    echo "$prog: $*" >&2
    exit 3
}

[ "$#" -ge 1 ] || die "usage: $0 CONFIG [--] COMMAND [ARG ...]"
config=$1
shift
[ "${1:-}" != -- ] || shift
[ -r "$config" ] || die "cannot read config: $config"

config_values=$(awk '
function'" "'fail(message) {
    print "perf_gate: " FILENAME ":" FNR ": " message > "/dev/stderr"
    bad = 1
}
function'" "'trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
}
{
    line = $0
    sub(/[[:space:]]*#.*/, "", line)
    line = trim(line)
    if (line == "")
        next
    equals = index(line, "=")
    if (!equals) {
        fail("expected key=value")
        next
    }
    key = trim(substr(line, 1, equals - 1))
    value = trim(substr(line, equals + 1))
    if (key !~ /^[a-z][a-z0-9_]*$/ || value == "") {
        fail("invalid configuration entry")
        next
    }
    if (key in seen) {
        fail("duplicate key " key)
        next
    }
    seen[key] = 1
    values[key] = value
}
END {
    required[1] = "name"
    required[2] = "state"
    required[3] = "where"
    required[4] = "when"
    required[5] = "threshold"
    required[6] = "rationale"
    required[7] = "owner_sprint"
    for (i = 1; i <= 7; i++)
        if (!(required[i] in values)) {
            print "perf_gate: " FILENAME ": missing " required[i] > "/dev/stderr"
            bad = 1
        }
    if (bad)
        exit 3
    if (values["name"] !~ /^[a-z0-9][a-z0-9-]*$/) {
        print "perf_gate: " FILENAME ": invalid name" > "/dev/stderr"
        exit 3
    }
    if (values["state"] !~ /^(blocking|trial|report-only|inactive)$/) {
        print "perf_gate: " FILENAME ": invalid state " values["state"] > "/dev/stderr"
        exit 3
    }
    if (values["state"] == "inactive" && !("defer_until" in values)) {
        print "perf_gate: " FILENAME ": inactive gate lacks defer_until" > "/dev/stderr"
        exit 3
    }
    print values["name"]
    print values["state"]
    print values["threshold"]
    print values["defer_until"]
}
' "$config") || exit $?

name=$(printf '%s\n' "$config_values" | sed -n '1p')
state=$(printf '%s\n' "$config_values" | sed -n '2p')
threshold=$(printf '%s\n' "$config_values" | sed -n '3p')
defer_until=$(printf '%s\n' "$config_values" | sed -n '4p')

summary=${GITHUB_STEP_SUMMARY:-}
report()
{
    line=$1
    printf '%s\n' "$line"
    if [ -n "$summary" ]; then
        [ ! -d "$summary" ] || die "summary path is a directory: $summary"
        printf '%s\n' "$line" >>"$summary" || die "cannot append summary: $summary"
    fi
}

if [ "$state" = inactive ]; then
    [ "$#" -eq 0 ] || die "inactive gate must not receive a command"
    report "perf_gate: $name INACTIVE until $defer_until"
    exit 0
fi

[ "$#" -gt 0 ] || die "active gate requires a command"

command_status=0
"$@" || command_status=$?
case $command_status in
0)
    report "perf_gate: $name PASS ($state; $threshold)"
    ;;
1)
    case $state in
    blocking)
        message_file=${PERF_COMMIT_MESSAGE_FILE:-}
        message_tmp=
        if [ -z "$message_file" ] &&
           git rev-parse --verify HEAD >/dev/null 2>&1; then
            message_tmp=$(mktemp "${TMPDIR:-/tmp}/cgf-perf-message.XXXXXX") ||
                die "cannot create commit-message temporary file"
            git log -1 --format=%B >"$message_tmp" ||
                die "cannot read HEAD commit message"
            message_file=$message_tmp
        fi
        if [ -n "$message_file" ] && [ -r "$message_file" ] &&
           grep -Eq 'perf-override: #[1-9][0-9]*([^A-Za-z0-9_]|$)' "$message_file"; then
            policy=${PERF_POLICY_CHECKER:-$(CDPATH='' cd "$(dirname "$0")" && pwd -P)/check_bench_policy.sh}
            [ -x "$policy" ] || die "policy checker is not executable: $policy"
            set -- "$policy" --message-file "$message_file"
            if [ -n "${PERF_POLICY_DIFF_FILE:-}" ]; then
                set -- "$@" --diff-file "$PERF_POLICY_DIFF_FILE"
            fi
            [ -z "$summary" ] || set -- "$@" --summary "$summary"
            "$@" || exit $?
            [ -z "$message_tmp" ] || rm -f "$message_tmp"
            report "perf_gate: $name OVERRIDDEN (blocking; $threshold)"
            exit 0
        fi
        [ -z "$message_tmp" ] || rm -f "$message_tmp"
        report "perf_gate: $name FAIL (blocking; $threshold)"
        exit 1
        ;;
    trial|report-only)
        report "perf_gate: $name TRIP-RECORDED ($state; $threshold)"
        ;;
    esac
    ;;
*)
    report "perf_gate: $name ERROR status=$command_status ($state)"
    exit 3
    ;;
esac
