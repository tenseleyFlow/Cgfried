#!/bin/sh
# Compare a Sprint 52 benchmark result with its committed baseline.
#
# Usage: benchmark_gate.sh BASELINE RESULT
# BENCH_SKIP_TIME=1 disables wall and user+sys gates (shared CI), but the
# max-RSS gate remains active.  Metric lane prefixes are deliberately opaque:
# only the final metric-name suffix is interpreted here.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 BASELINE RESULT" >&2
    exit 3
fi

baseline=$1
result=$2
skip_time=${BENCH_SKIP_TIME:-0}
gate_kind=${BENCH_GATE_KIND:-all}

case "$skip_time" in
0|1) ;;
*)
    echo "benchmark_gate: BENCH_SKIP_TIME must be 0 or 1" >&2
    exit 3
    ;;
esac
case "$gate_kind" in
all|time|rss) ;;
*)
    echo "benchmark_gate: BENCH_GATE_KIND must be all, time, or rss" >&2
    exit 3
    ;;
esac

for file in "$baseline" "$result"; do
    if [ ! -r "$file" ]; then
        echo "benchmark_gate: cannot read $file" >&2
        exit 3
    fi
done

set +e
awk -v baseline_file="$baseline" -v skip_time="$skip_time" \
    -v gate_kind="$gate_kind" '
function'" "'schema_fail(message) {
    print "benchmark_gate: " message > "/dev/stderr"
    schema_failed = 1
}

function'" "'regression(message) {
    print "benchmark_gate: " message > "/dev/stderr"
    regressed = 1
}

function'" "'trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
}

function'" "'read_metric(line, file, line_no,    equals, key, value) {
    sub(/[[:space:]]*#.*/, "", line)
    line = trim(line)
    if (line == "")
        return

    equals = index(line, "=")
    if (!equals) {
        schema_fail(file ":" line_no ": expected metric=value")
        return
    }
    key = trim(substr(line, 1, equals - 1))
    value = trim(substr(line, equals + 1))
    if (key !~ /^[A-Za-z0-9_.:-]+$/) {
        schema_fail(file ":" line_no ": invalid metric name " key)
        return
    }

    # Provenance and report-only stats may contain strings.  Gated metrics
    # must be non-negative decimal numbers.
    if (key ~ /(wall_ms_median|user_ms_median|sys_ms_median|maxrss_kb_max)$/ &&
        value !~ /^[0-9]+([.][0-9]+)?$/) {
        schema_fail(file ":" line_no ": gated metric " key \
             " must have a non-negative numeric value")
        return
    }

    if (file == baseline_file) {
        if (key in baseline)
            schema_fail(file ":" line_no ": duplicate metric " key)
        baseline[key] = value
    } else {
        if (key in current)
            schema_fail(file ":" line_no ": duplicate metric " key)
        current[key] = value
    }
}

function'" "'require_metric(key) {
    if (!(key in current)) {
        schema_fail("missing required result metric " key)
        return 0
    }
    return 1
}

function'" "'check_limit(label, baseline_value, current_value, percent,
                     baseline_total, limit) {
    baseline_total = baseline_value + 0
    limit = baseline_total * (100 + percent) / 100
    if ((current_value + 0) > limit) {
        regression(label " regressed: baseline=" baseline_value \
             " result=" current_value " limit=+" percent "%")
    } else {
        passed++
    }
}

{
    read_metric($0, FILENAME, FNR)
}

END {
    for (key in baseline) {
        if (key ~ /maxrss_kb_max$/ &&
            (gate_kind == "all" || gate_kind == "rss")) {
            gated++
            if (require_metric(key))
                check_limit(key, baseline[key], current[key], 20)
        } else if (!skip_time && key ~ /wall_ms_median$/ &&
                   (gate_kind == "all" || gate_kind == "time")) {
            gated++
            if (require_metric(key))
                check_limit(key, baseline[key], current[key], 30)
        } else if (!skip_time && key ~ /user_ms_median$/ &&
                   (gate_kind == "all" || gate_kind == "time")) {
            prefix = key
            sub(/user_ms_median$/, "", prefix)
            sys_key = prefix "sys_ms_median"
            gated++
            have_user = require_metric(key)
            if (!(sys_key in baseline)) {
                schema_fail("baseline metric " key " requires paired metric " sys_key)
                have_sys = 0
            } else {
                have_sys = require_metric(sys_key)
            }
            if (have_user && have_sys)
                check_limit(prefix "user+sys_ms_median",
                            baseline[key] + baseline[sys_key],
                            current[key] + current[sys_key], 30)
        } else if (!skip_time && key ~ /sys_ms_median$/ &&
                   (gate_kind == "all" || gate_kind == "time")) {
            prefix = key
            sub(/sys_ms_median$/, "", prefix)
            user_key = prefix "user_ms_median"
            if (!(user_key in baseline))
                schema_fail("baseline metric " key " requires paired metric " user_key)
        }
    }

    if (!gated)
        schema_fail("baseline contains no active gated metrics")
    if (schema_failed)
        exit 3
    if (regressed)
        exit 1
    print "benchmark_gate: pass (" passed " comparisons)"
}
' "$baseline" "$result"
status=$?
set -e

case "$status" in
0|1|3) exit "$status" ;;
*)
    echo "benchmark_gate: metric parser failed with status $status" >&2
    exit 3
    ;;
esac
