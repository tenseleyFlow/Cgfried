#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
gate=$repo/scripts/benchmark_gate.sh
fixtures=$repo/tests/bench/fixtures/gate
tmp=${TMPDIR:-/tmp}/cgf-benchmark-gate-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp"

make_evidence() {
    source_file=$1
    output_file=$2
    awk '
        /^[A-Za-z0-9_.:-]+=/ {
            key = $0
            sub(/=.*/, "", key)
            if (key == "target" || key == "host" || key == "host_class" ||
                key == "governor" || key == "load1" || key == "runs" ||
                key == "warmup" || key == "timeit_protocol" ||
                key == "lane_order" || key == "sqlite_version" ||
                key == "sqlite_sha3" || key == "sqlite_cksum" ||
                key == "sqlite3.corpus" || key == "self.corpus" ||
                key == "many-tu.corpus" || key == "self_limit" ||
                key == "sysroot_include" || key == "sysroot_crt" ||
                key == "power_profile" || key == "scaling_driver" ||
                key == "energy_performance_preference" ||
                key == "control_protocol" || key == "logical_cpus" ||
                key == "cpu_idle_pct")
                next
        }
        { print }
    ' "$source_file" >"$output_file"
    {
        echo 'target=x86_64-linux-gnu'
        echo 'host=kasumi'
        echo 'governor=performance'
        echo 'power_profile=performance'
        echo 'scaling_driver=acpi-cpufreq'
        echo 'energy_performance_preference=unavailable'
        echo 'control_protocol=fleet-control-v2'
        echo 'logical_cpus=20'
        echo 'cpu_idle_pct=90.00'
        echo 'load1=0.10'
        echo 'runs=10'
        echo 'warmup=1'
        echo 'timeit_protocol=sprint-52-compile-median-mad-v1'
        echo 'lane_order=sqlite3,self,many-tu,musl'
        echo 'sqlite_version=3500400'
        echo 'sqlite_sha3=fixture-sha3'
        echo 'sqlite_cksum=123:456'
        echo 'sqlite3.corpus=sqlite-amalgamation-3500400'
        echo 'self.corpus=cgfried-src-result:12-files'
        echo 'many-tu.corpus=cgf-many-tu-v1:500x200'
        echo 'self_limit=0'
    } >>"$output_file"
}

replace_value() {
    key=$1
    value=$2
    input=$3
    output=$4
    sed "s|^$key=.*|$key=$value|" "$input" >"$output"
}

for fixture in "$fixtures"/*.txt; do
    make_evidence "$fixture" "$tmp/$(basename "$fixture")"
done
replace_value self.corpus cgfried-src-baseline:10-files \
    "$tmp/baseline.txt" "$tmp/baseline.with-self.txt"
mv "$tmp/baseline.with-self.txt" "$tmp/baseline.txt"

pass_case() {
    name=$1
    shift
    if ! "$@" >"$tmp/$name.out" 2>"$tmp/$name.err"; then
        echo "benchmark_gate_test: expected pass: $name" >&2
        sed 's/^/  /' "$tmp/$name.err" >&2
        exit 1
    fi
}

fail_case() {
    name=$1
    expected_status=$2
    expected=$3
    shift 3
    set +e
    "$@" >"$tmp/$name.out" 2>"$tmp/$name.err"
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "benchmark_gate_test: expected failure: $name" >&2
        exit 1
    fi
    if [ "$status" -ne "$expected_status" ]; then
        echo "benchmark_gate_test: $name exited $status, expected $expected_status" >&2
        sed 's/^/  /' "$tmp/$name.err" >&2
        exit 1
    fi
    if ! grep -F "$expected" "$tmp/$name.err" >/dev/null; then
        echo "benchmark_gate_test: $name lacked diagnostic: $expected" >&2
        sed 's/^/  /' "$tmp/$name.err" >&2
        exit 1
    fi
}

pass_case exact-boundaries "$gate" "$tmp/baseline.txt" \
    "$tmp/pass-boundaries.txt"
pass_case pass-at-29 "$gate" "$tmp/baseline.txt" \
    "$tmp/pass-29.txt"
fail_case fail-at-31 1 "self.wall_ms_median regressed" \
    "$gate" "$tmp/baseline.txt" "$tmp/fail-31.txt"
fail_case fail-cpu-at-31 1 "self.user+sys_ms_median regressed" \
    "$gate" "$tmp/baseline.txt" "$tmp/fail-cpu-31.txt"
fail_case fail-rss-at-21 1 "self.maxrss_kb_max regressed" \
    "$gate" "$tmp/baseline.txt" "$tmp/fail-rss-21.txt"
fail_case missing-metric 3 "missing required result metric self.maxrss_kb_max" \
    "$gate" "$tmp/baseline.txt" "$tmp/missing-rss.txt"
fail_case malformed-metric 3 "must have a non-negative numeric value" \
    "$gate" "$tmp/baseline.txt" "$tmp/malformed.txt"
fail_case duplicate-metric 3 "duplicate metric self.wall_ms_median" \
    "$gate" "$tmp/baseline.txt" "$tmp/duplicate.txt"

# Shared-CI mode must ignore even a large timing regression.
pass_case skip-time env BENCH_SKIP_TIME=1 "$gate" \
    "$tmp/baseline.txt" "$tmp/skip-time.txt"
# The same mode must continue enforcing the RSS limit.
fail_case skip-time-rss 1 "sqlite3.maxrss_kb_max regressed" \
    env BENCH_SKIP_TIME=1 "$gate" "$tmp/baseline.txt" \
    "$tmp/skip-time-rss-fail.txt"

# The state wrapper applies time and RSS policy independently.
pass_case rss-kind-ignores-time env BENCH_GATE_KIND=rss "$gate" \
    "$tmp/baseline.txt" "$tmp/fail-31.txt"
pass_case time-kind-ignores-rss env BENCH_GATE_KIND=time "$gate" \
    "$tmp/baseline.txt" "$tmp/fail-rss-21.txt"

replace_value governor powersave "$tmp/pass-29.txt" "$tmp/powersave.txt"
fail_case powersave-time-fails-closed 3 "timing evidence is not controlled" \
    "$gate" "$tmp/baseline.txt" "$tmp/powersave.txt"
pass_case powersave-provenance-only env BENCH_GATE_KIND=time \
    BENCH_ALLOW_PROVENANCE_ONLY=1 "$gate" "$tmp/baseline.txt" \
    "$tmp/powersave.txt"
grep -Fx 'benchmark_gate: provenance-only (uncontrolled timing evidence)' \
    "$tmp/powersave-provenance-only.out" >/dev/null

replace_value governor powersave "$tmp/baseline.txt" "$tmp/intel-baseline.1"
replace_value scaling_driver intel_pstate "$tmp/intel-baseline.1" \
    "$tmp/intel-baseline.2"
replace_value energy_performance_preference performance "$tmp/intel-baseline.2" \
    "$tmp/intel-baseline.txt"
replace_value governor powersave "$tmp/pass-29.txt" "$tmp/intel-result.1"
replace_value scaling_driver intel_pstate "$tmp/intel-result.1" \
    "$tmp/intel-result.2"
replace_value energy_performance_preference performance "$tmp/intel-result.2" \
    "$tmp/intel-result.txt"
pass_case intel-pstate-epp-performance "$gate" "$tmp/intel-baseline.txt" \
    "$tmp/intel-result.txt"

sed '/^control_protocol=/d;/^logical_cpus=/d;/^cpu_idle_pct=/d' \
    "$tmp/baseline.txt" >"$tmp/legacy-baseline.txt"
pass_case mixed-legacy-v2-strict env BENCH_GATE_KIND=time "$gate" \
    "$tmp/legacy-baseline.txt" \
    "$tmp/pass-29.txt"
replace_value power_profile '' "$tmp/baseline.txt" "$tmp/empty-control-baseline.txt"
fail_case legacy-single-empty-control 3 \
    "bench-control:" \
    env BENCH_GATE_KIND=time BENCH_ALLOW_PROVENANCE_ONLY=1 "$gate" \
    "$tmp/empty-control-baseline.txt" "$tmp/pass-29.txt"
sed 's/^power_profile=.*/power_profile=/;s/^scaling_driver=.*/scaling_driver=/;s/^energy_performance_preference=.*/energy_performance_preference=/' \
    "$tmp/baseline.txt" >"$tmp/all-empty-control-baseline.txt"
fail_case legacy-all-empty-controls 3 \
    "bench-control:" \
    env BENCH_GATE_KIND=time BENCH_ALLOW_PROVENANCE_ONLY=1 "$gate" \
    "$tmp/all-empty-control-baseline.txt" "$tmp/pass-29.txt"
sed '/^power_profile=/d' "$tmp/pass-29.txt" >"$tmp/missing-control.txt"
fail_case current-missing-control 3 "bench-control:" \
    env BENCH_GATE_KIND=time BENCH_ALLOW_PROVENANCE_ONLY=1 "$gate" \
    "$tmp/baseline.txt" "$tmp/missing-control.txt"
fail_case powersave-rss-still-active 1 "sqlite3.maxrss_kb_max regressed" \
    env BENCH_GATE_KIND=rss "$gate" "$tmp/baseline.txt" \
    "$tmp/skip-time-rss-fail.txt"

replace_value host nomad-1 "$tmp/baseline.txt" "$tmp/nomad-baseline.1"
replace_value governor unavailable "$tmp/nomad-baseline.1" "$tmp/nomad-baseline.2"
replace_value power_profile unavailable "$tmp/nomad-baseline.2" "$tmp/nomad-baseline.3"
replace_value scaling_driver unavailable "$tmp/nomad-baseline.3" "$tmp/nomad-baseline.4"
replace_value energy_performance_preference unavailable "$tmp/nomad-baseline.4" \
    "$tmp/nomad-baseline.txt"
replace_value host nomad-1 "$tmp/pass-29.txt" "$tmp/nomad-result.1"
replace_value governor unavailable "$tmp/nomad-result.1" "$tmp/nomad-result.2"
replace_value power_profile unavailable "$tmp/nomad-result.2" "$tmp/nomad-result.3"
replace_value scaling_driver unavailable "$tmp/nomad-result.3" "$tmp/nomad-result.4"
replace_value energy_performance_preference unavailable "$tmp/nomad-result.4" \
    "$tmp/nomad-result.txt"
pass_case nomad-unavailable "$gate" "$tmp/nomad-baseline.txt" \
    "$tmp/nomad-result.txt"
replace_value power_profile performance "$tmp/nomad-result.txt" \
    "$tmp/nomad-bad-controls.txt"
fail_case nomad-controls-not-unavailable 3 \
    "bench-control:" \
    "$gate" "$tmp/nomad-baseline.txt" "$tmp/nomad-bad-controls.txt"
replace_value governor performance "$tmp/nomad-result.txt" \
    "$tmp/governor-mismatch.txt"
fail_case governor-mismatch 3 "governor provenance does not match baseline" \
    "$gate" "$tmp/nomad-baseline.txt" "$tmp/governor-mismatch.txt"

for mismatch in target runs warmup timeit_protocol sqlite3.corpus many-tu.corpus; do
    replace_value "$mismatch" mismatch "$tmp/pass-29.txt" "$tmp/mismatch.txt"
    fail_case "$mismatch-mismatch" 3 "$mismatch provenance does not match baseline" \
        "$gate" "$tmp/baseline.txt" "$tmp/mismatch.txt"
done
replace_value load1 4.01 "$tmp/pass-29.txt" "$tmp/high-load.txt"
fail_case high-load 3 "timing evidence is not controlled" \
    "$gate" "$tmp/baseline.txt" "$tmp/high-load.txt"
replace_value load1 0.20 "$tmp/pass-29.txt" "$tmp/different-controlled-load.txt"
pass_case different-controlled-load "$gate" "$tmp/baseline.txt" \
    "$tmp/different-controlled-load.txt"
replace_value load1 4.0 "$tmp/pass-29.txt" "$tmp/normalized-boundary.1"
replace_value cpu_idle_pct 85 "$tmp/normalized-boundary.1" \
    "$tmp/normalized-boundary.txt"
pass_case normalized-load-boundary "$gate" "$tmp/baseline.txt" \
    "$tmp/normalized-boundary.txt"
replace_value load1 4.01 "$tmp/pass-29.txt" "$tmp/normalized-overload.txt"
fail_case normalized-load-over-boundary 3 "timing evidence is not controlled" \
    "$gate" "$tmp/baseline.txt" "$tmp/normalized-overload.txt"
pass_case normalized-overload-provenance-only env BENCH_GATE_KIND=time \
    BENCH_ALLOW_PROVENANCE_ONLY=1 "$gate" "$tmp/baseline.txt" \
    "$tmp/normalized-overload.txt"
replace_value cpu_idle_pct 84.99 "$tmp/pass-29.txt" "$tmp/idle-below-boundary.txt"
fail_case idle-below-boundary 3 "timing evidence is not controlled" \
    "$gate" "$tmp/baseline.txt" "$tmp/idle-below-boundary.txt"
pass_case idle-below-boundary-provenance-only env BENCH_GATE_KIND=time \
    BENCH_ALLOW_PROVENANCE_ONLY=1 "$gate" "$tmp/baseline.txt" \
    "$tmp/idle-below-boundary.txt"
replace_value logical_cpus 24 "$tmp/pass-29.txt" "$tmp/cpu-mismatch.txt"
fail_case logical-cpus-mismatch 3 "logical_cpus provenance does not match baseline" \
    "$gate" "$tmp/baseline.txt" "$tmp/cpu-mismatch.txt"
pass_case logical-cpus-mismatch-provenance-only env BENCH_GATE_KIND=time \
    BENCH_ALLOW_PROVENANCE_ONLY=1 "$gate" "$tmp/baseline.txt" \
    "$tmp/cpu-mismatch.txt"
grep -Fx 'benchmark_gate: provenance-only (uncontrolled timing evidence)' \
    "$tmp/logical-cpus-mismatch-provenance-only.out" >/dev/null
sed '/^logical_cpus=/d' "$tmp/pass-29.txt" >"$tmp/missing-v2-field.txt"
fail_case missing-v2-field 3 "bench-control:" "$gate" "$tmp/baseline.txt" \
    "$tmp/missing-v2-field.txt"
replace_value control_protocol bad-v2 "$tmp/pass-29.txt" \
    "$tmp/malformed-v2-field.txt"
fail_case malformed-v2-field 3 "bench-control:" "$gate" "$tmp/baseline.txt" \
    "$tmp/malformed-v2-field.txt"
sed '/^timeit_protocol=/d' "$tmp/pass-29.txt" >"$tmp/missing-provenance.txt"
fail_case missing-provenance 3 "missing required result provenance timeit_protocol" \
    "$gate" "$tmp/baseline.txt" "$tmp/missing-provenance.txt"
{
    printf '%s\n' 'target=duplicate-target'
    sed -n '1,$p' "$tmp/pass-29.txt"
} >"$tmp/duplicate-provenance.txt"
fail_case duplicate-provenance 3 "duplicate metric target" \
    "$gate" "$tmp/baseline.txt" "$tmp/duplicate-provenance.txt"

fail_case usage 3 "usage:" "$gate"
fail_case unreadable 3 "cannot read" "$gate" "$tmp/not-found" \
    "$tmp/pass-boundaries.txt"
fail_case bad-skip-time 3 "BENCH_SKIP_TIME must be 0 or 1" \
    env BENCH_SKIP_TIME=maybe "$gate" "$tmp/baseline.txt" \
    "$tmp/pass-boundaries.txt"
fail_case bad-gate-kind 3 "BENCH_GATE_KIND must be all, time, or rss" \
    env BENCH_GATE_KIND=maybe "$gate" "$tmp/baseline.txt" \
    "$tmp/pass-boundaries.txt"

fleet=$repo/scripts/fleet-bench.sh
fleet_root=$tmp/fleet
fleet_result=$fleet_root/.benchmarks/runs/fixture-kasumi.txt
mkdir -p "$fleet_root/.git" "$fleet_root/.benchmarks/runs"
cp "$tmp/baseline.txt" \
    "$fleet_root/.benchmarks/baseline-x86_64-linux-gnu.kasumi.txt"
cp "$fixtures/fake-fleet-bench.sh" "$tmp/fake-fleet-bench.sh"
cp "$fixtures/fake-fleet-git.sh" "$tmp/fake-fleet-git.sh"
chmod +x "$tmp/fake-fleet-bench.sh" "$tmp/fake-fleet-git.sh"
replace_value governor powersave "$tmp/fail-31.txt" "$tmp/fleet-powersave.txt"
pass_case fleet-provenance-only env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture CGF_FLEET_RESULT="$fleet_result" \
    CGF_FLEET_COMMIT=0 FIXTURE_BENCH_RESULT="$tmp/fleet-powersave.txt" \
    "$fleet"
grep -q '^fleet.rss_gate=pass$' "$fleet_result"
grep -q '^fleet.time_gate=provenance-only$' "$fleet_result"
grep -q '^fleet.gate=provenance-only$' "$fleet_result"
grep -q '^fleet.self_file_count=12$' "$fleet_result"

fleet_rss_result=$fleet_root/.benchmarks/runs/fixture-rss-kasumi.txt
replace_value governor powersave "$tmp/skip-time-rss-fail.txt" \
    "$tmp/fleet-rss-fail.txt"
fail_case fleet-rss-trip 1 "maxrss_kb_max regressed" env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-rss CGF_FLEET_RESULT="$fleet_rss_result" \
    CGF_FLEET_COMMIT=0 FIXTURE_BENCH_RESULT="$tmp/fleet-rss-fail.txt" \
    "$fleet"
grep -q '^fleet.rss_gate=trip$' "$fleet_rss_result"
grep -q '^fleet.time_gate=provenance-only$' "$fleet_rss_result"
grep -q '^fleet.gate=trip$' "$fleet_rss_result"

fleet_missing_result=$fleet_root/.benchmarks/runs/fixture-missing-kasumi.txt
sed '/^governor=/d' "$tmp/pass-29.txt" >"$tmp/fleet-missing-governor.txt"
fail_case fleet-missing-governor 3 "bench-control:" env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-missing CGF_FLEET_RESULT="$fleet_missing_result" \
    CGF_FLEET_COMMIT=0 FIXTURE_BENCH_RESULT="$tmp/fleet-missing-governor.txt" \
    "$fleet"

fleet_bad_load_result=$fleet_root/.benchmarks/runs/fixture-bad-load-kasumi.txt
replace_value load1 unknown "$tmp/pass-29.txt" "$tmp/fleet-bad-load.txt"
fail_case fleet-malformed-load 3 "bench-control:" env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-bad-load CGF_FLEET_RESULT="$fleet_bad_load_result" \
    CGF_FLEET_COMMIT=0 FIXTURE_BENCH_RESULT="$tmp/fleet-bad-load.txt" \
    "$fleet"

rm -f "$fleet_root/.benchmarks/baseline-x86_64-linux-gnu.kasumi.txt"
fleet_warmup_result=$fleet_root/.benchmarks/runs/fixture-warmup-kasumi.txt
pass_case fleet-controlled-warmup env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-warmup CGF_FLEET_RESULT="$fleet_warmup_result" \
    CGF_FLEET_COMMIT=0 FIXTURE_BENCH_RESULT="$tmp/pass-29.txt" \
    "$fleet"
grep -q '^fleet.rss_gate=warmup$' "$fleet_warmup_result"
grep -q '^fleet.time_gate=warmup$' "$fleet_warmup_result"
grep -q '^fleet.gate=warmup$' "$fleet_warmup_result"

fleet_uncontrolled_result=$fleet_root/.benchmarks/runs/fixture-uncontrolled-kasumi.txt
pass_case fleet-uncontrolled-warmup env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-uncontrolled \
    CGF_FLEET_RESULT="$fleet_uncontrolled_result" CGF_FLEET_COMMIT=0 \
    FIXTURE_BENCH_RESULT="$tmp/high-load.txt" "$fleet"
grep -q '^fleet.rss_gate=warmup$' "$fleet_uncontrolled_result"
grep -q '^fleet.time_gate=provenance-only$' "$fleet_uncontrolled_result"
grep -q '^fleet.gate=provenance-only$' "$fleet_uncontrolled_result"

fleet_no_baseline_missing=$fleet_root/.benchmarks/runs/fixture-no-baseline-missing-kasumi.txt
fail_case fleet-no-baseline-missing-control 3 \
    "bench-control:" env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-no-baseline-missing \
    CGF_FLEET_RESULT="$fleet_no_baseline_missing" CGF_FLEET_COMMIT=0 \
    FIXTURE_BENCH_RESULT="$tmp/missing-control.txt" "$fleet"

sed '/^control_protocol=/d;/^logical_cpus=/d;/^cpu_idle_pct=/d' \
    "$tmp/pass-29.txt" >"$tmp/missing-v2.txt"
fleet_no_baseline_missing_v2=$fleet_root/.benchmarks/runs/fixture-no-baseline-missing-v2-kasumi.txt
fail_case fleet-no-baseline-missing-v2 3 "bench-control:" env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-no-baseline-missing-v2 \
    CGF_FLEET_RESULT="$fleet_no_baseline_missing_v2" CGF_FLEET_COMMIT=0 \
    FIXTURE_BENCH_RESULT="$tmp/missing-v2.txt" "$fleet"

replace_value energy_performance_preference bad/value "$tmp/pass-29.txt" \
    "$tmp/malformed-control.txt"
fleet_no_baseline_malformed=$fleet_root/.benchmarks/runs/fixture-no-baseline-malformed-kasumi.txt
fail_case fleet-no-baseline-malformed-control 3 \
    "bench-control:" env \
    CGF_FLEET_ROOT="$fleet_root" \
    CGF_FLEET_GIT_CMD="$tmp/fake-fleet-git.sh" \
    CGF_FLEET_BENCH_SCRIPT="$tmp/fake-fleet-bench.sh" \
    CGF_FLEET_BENCHMARK_GATE="$gate" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=fixture-no-baseline-malformed \
    CGF_FLEET_RESULT="$fleet_no_baseline_malformed" CGF_FLEET_COMMIT=0 \
    FIXTURE_BENCH_RESULT="$tmp/malformed-control.txt" "$fleet"

echo "benchmark_gate_test: 56 cases passed"
