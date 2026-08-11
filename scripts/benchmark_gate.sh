#!/bin/sh
# Compare a Sprint 52 benchmark result with its committed baseline.
#
# Usage: benchmark_gate.sh BASELINE RESULT
# BENCH_SKIP_TIME=1 disables wall and user+sys gates (shared CI), but the
# max-RSS gate remains active.  Timing comparisons otherwise require controlled
# fleet evidence; provenance and workload identity are checked for every gate.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 BASELINE RESULT" >&2
    exit 3
fi

baseline=$1
result=$2
control_script=${BENCH_CONTROL_SCRIPT:-$(CDPATH='' cd "$(dirname "$0")" && pwd -P)/bench-control.sh}
skip_time=${BENCH_SKIP_TIME:-0}
gate_kind=${BENCH_GATE_KIND:-all}
allow_provenance_only=${BENCH_ALLOW_PROVENANCE_ONLY:-0}

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
case "$allow_provenance_only" in
0|1) ;;
*)
    echo "benchmark_gate: BENCH_ALLOW_PROVENANCE_ONLY must be 0 or 1" >&2
    exit 3
    ;;
esac
if [ "$allow_provenance_only" -eq 1 ] &&
   { [ "$skip_time" -ne 0 ] || [ "$gate_kind" != time ]; }; then
    echo "benchmark_gate: BENCH_ALLOW_PROVENANCE_ONLY requires BENCH_GATE_KIND=time and BENCH_SKIP_TIME=0" >&2
    exit 3
fi

for file in "$baseline" "$result"; do
    if [ ! -r "$file" ]; then
        echo "benchmark_gate: cannot read $file" >&2
        exit 3
    fi
done

initial_provenance_only=0
if [ "$skip_time" -eq 0 ] &&
   { [ "$gate_kind" = all ] || [ "$gate_kind" = time ]; }; then
    [ -x "$control_script" ] || {
        echo "benchmark_gate: control helper is not executable: $control_script" >&2
        exit 3
    }
    for file in "$baseline" "$result"; do
        control_status=0
        control_output=$("$control_script" classify "$file") ||
            control_status=$?
        case $control_status:$control_output in
        0:controlled) ;;
        1:provenance-only)
            if [ "$allow_provenance_only" -eq 1 ]; then
                initial_provenance_only=1
            else
                echo "benchmark_gate: timing evidence is not controlled: $file" >&2
                exit 3
            fi
            ;;
        3:*) exit 3 ;;
        *)
            echo "benchmark_gate: control helper failed with status $control_status" >&2
            exit 3
            ;;
        esac
    done
fi

set +e
awk -v baseline_file="$baseline" -v skip_time="$skip_time" \
    -v gate_kind="$gate_kind" -v allow_provenance_only="$allow_provenance_only" \
    -v initial_provenance_only="$initial_provenance_only" '
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

function'" "'require_provenance(key, values, label) {
    if (!(key in values) || values[key] == "") {
        schema_fail("missing required " label " provenance " key)
        return 0
    }
    return 1
}

function'" "'check_same(key,    have_baseline, have_current) {
    have_baseline = require_provenance(key, baseline, "baseline")
    have_current = require_provenance(key, current, "result")
    if (have_baseline && have_current && baseline[key] != current[key])
        schema_fail(key " provenance does not match baseline (baseline=" \
             baseline[key] " result=" current[key] ")")
}

function'" "'check_optional_pair(key,    in_baseline, in_current) {
    in_baseline = key in baseline
    in_current = key in current
    if (in_baseline != in_current) {
        schema_fail(key " provenance must be present in both baseline and result")
    } else if (in_baseline && baseline[key] != current[key]) {
        schema_fail(key " provenance does not match baseline (baseline=" \
             baseline[key] " result=" current[key] ")")
    }
}

function'" "'check_self_corpus(values, label,    value) {
    if (!require_provenance("self.corpus", values, label))
        return
    value = values["self.corpus"]
    if (value !~ /^cgfried-src-[^:]+:[0-9]+-files(:[^:]*)*$/)
        schema_fail(label " self.corpus must expose its revision/content and file count")
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
    provenance_only = initial_provenance_only
    check_same("target")
    if (("host_class" in baseline) || ("host_class" in current)) {
        check_same("host_class")
        require_provenance("host", baseline, "baseline")
        require_provenance("host", current, "result")
    } else {
        check_same("host")
    }
    check_same("runs")
    check_same("warmup")
    check_same("timeit_protocol")
    check_same("lane_order")
    check_same("sqlite_version")
    check_same("sqlite_sha3")
    check_same("sqlite_cksum")
    check_same("sqlite3.corpus")
    check_same("many-tu.corpus")
    check_same("self_limit")
    check_optional_pair("sysroot_include")
    check_optional_pair("sysroot_crt")
    if (("sysroot_include" in baseline) != ("sysroot_crt" in baseline))
        schema_fail("baseline sysroot_include and sysroot_crt must be present together")
    if (("sysroot_include" in current) != ("sysroot_crt" in current))
        schema_fail("result sysroot_include and sysroot_crt must be present together")
    check_self_corpus(baseline, "baseline")
    check_self_corpus(current, "result")

    if (!skip_time && (gate_kind == "all" || gate_kind == "time")) {
        baseline_shared = baseline["host_class"] == "ci" ||
                          baseline["host_class"] == "shared-ci" ||
                          baseline["host_class"] == "arm64-ci"
        current_shared = current["host_class"] == "ci" ||
                         current["host_class"] == "shared-ci" ||
                         current["host_class"] == "arm64-ci"
        if (baseline_shared || current_shared)
            schema_fail("shared-CI timing evidence is ineligible; set BENCH_SKIP_TIME=1")
        control_keys[1] = "governor"
        control_keys[2] = "power_profile"
        control_keys[3] = "scaling_driver"
        control_keys[4] = "energy_performance_preference"
        for (control_i = 1; control_i <= 4; control_i++) {
            control_key = control_keys[control_i]
            if ((control_key in baseline) && (control_key in current) &&
                baseline[control_key] != current[control_key]) {
                if (allow_provenance_only)
                    provenance_only = 1
                else
                    schema_fail(control_key " provenance does not match baseline (baseline=" \
                         baseline[control_key] " result=" current[control_key] ")")
            }
        }
        baseline_v2 = "control_protocol" in baseline
        current_v2 = "control_protocol" in current
        if (baseline_v2 && current_v2) {
            if (baseline["control_protocol"] != current["control_protocol"] ||
                baseline["logical_cpus"] != current["logical_cpus"]) {
                if (allow_provenance_only) {
                    provenance_only = 1
                } else if (baseline["control_protocol"] != current["control_protocol"]) {
                    schema_fail("control_protocol provenance does not match baseline")
                } else {
                    schema_fail("logical_cpus provenance does not match baseline (baseline=" \
                         baseline["logical_cpus"] " result=" current["logical_cpus"] ")")
                }
            }
        }
    }

    for (key in baseline) {
        if (key ~ /maxrss_kb_max$/ &&
            (gate_kind == "all" || gate_kind == "rss")) {
            gated++
            if (require_metric(key))
                check_limit(key, baseline[key], current[key], 20)
        } else if (!skip_time && key ~ /wall_ms_median$/ &&
                   (gate_kind == "all" || gate_kind == "time")) {
            gated++
            have_metric = require_metric(key)
            if (have_metric && !provenance_only)
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
            if (have_user && have_sys && !provenance_only)
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
    if (provenance_only)
        print "benchmark_gate: provenance-only (uncontrolled timing evidence)"
    else
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
