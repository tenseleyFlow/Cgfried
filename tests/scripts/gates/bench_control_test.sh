#!/bin/sh
set -eu

script_dir=$(dirname -- "$0")
repo=$(CDPATH='' cd "$script_dir/../../.." && pwd -P)
control=$repo/scripts/bench-control.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bench-control-test.XXXXXX") || {
    echo 'bench_control_test: cannot create temporary directory' >&2
    exit 1
}
trap 'rm -rf "$work"' EXIT HUP INT TERM
tests=0

fail()
{
    echo "bench_control_test: $*" >&2
    exit 1
}

expect_classify()
{
    expected_status=$1
    expected_output=$2
    shift 2
    tests=$((tests + 1))
    set +e
    "$control" classify "$@" >"$work/out" 2>"$work/err"
    actual_status=$?
    set -e
    [ "$actual_status" -eq "$expected_status" ] || {
        cat "$work/out" "$work/err" >&2
        fail "expected status $expected_status, got $actual_status: $*"
    }
    actual_output=$(cat "$work/out")
    [ "$actual_output" = "$expected_output" ] ||
        fail "expected '$expected_output', got '$actual_output': $*"
}

write_hasu()
{
    file=$1
    load=$2
    idle=$3
    cat >"$file" <<EOF
host=hasu
governor=powersave
power_profile=performance
scaling_driver=intel_pstate
energy_performance_preference=performance
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=$idle
load1=$load
EOF
}

write_hasu "$work/v2-boundary" 4.0 85
expect_classify 0 controlled "$work/v2-boundary"
write_hasu "$work/v2-load-high" 4.01 100
expect_classify 1 provenance-only "$work/v2-load-high"
write_hasu "$work/v2-idle-low" 0 84.99
expect_classify 1 provenance-only "$work/v2-idle-low"

cat >"$work/legacy-boundary" <<'EOF'
host=kasumi
governor=performance
power_profile=performance
scaling_driver=acpi-cpufreq
energy_performance_preference=performance
load1=0.5
EOF
expect_classify 0 controlled "$work/legacy-boundary"
sed 's/load1=0.5/load1=0.51/' "$work/legacy-boundary" >"$work/legacy-high"
expect_classify 1 provenance-only "$work/legacy-high"
sed '/power_profile/d; /scaling_driver/d; /energy_performance_preference/d' \
    "$work/legacy-boundary" >"$work/legacy-no-power"
expect_classify 1 provenance-only "$work/legacy-no-power"
expect_classify 3 '' --require-v2 "$work/legacy-boundary"

cat >"$work/legacy-nomad-unknown" <<'EOF'
host=nomad-1
governor=unavailable
load1=unknown
EOF
expect_classify 1 provenance-only "$work/legacy-nomad-unknown"
sed 's/load1=unknown/load1=busy/' "$work/legacy-nomad-unknown" \
    >"$work/legacy-nomad-bad-load"
expect_classify 3 '' "$work/legacy-nomad-bad-load"

sed '/cpu_idle_pct/d' "$work/v2-boundary" >"$work/partial-v2"
expect_classify 3 '' "$work/partial-v2"
sed 's/fleet-control-v2/fleet-control-v3/' "$work/v2-boundary" >"$work/bad-protocol"
expect_classify 3 '' "$work/bad-protocol"
sed 's/logical_cpus=20/logical_cpus=0/' "$work/v2-boundary" >"$work/bad-cpus"
expect_classify 3 '' "$work/bad-cpus"
sed 's/logical_cpus=20/logical_cpus=020/' "$work/v2-boundary" \
    >"$work/leading-zero-cpus"
expect_classify 3 '' "$work/leading-zero-cpus"
sed 's/cpu_idle_pct=85/cpu_idle_pct=100.01/' "$work/v2-boundary" >"$work/bad-idle"
expect_classify 3 '' "$work/bad-idle"
sed 's/load1=4.0/load1=busy/' "$work/v2-boundary" >"$work/bad-load"
expect_classify 3 '' "$work/bad-load"
cp "$work/v2-boundary" "$work/duplicate-v2"
printf 'logical_cpus=20\n' >>"$work/duplicate-v2"
expect_classify 3 '' "$work/duplicate-v2"
sed '/scaling_driver/d' "$work/v2-boundary" >"$work/partial-power"
expect_classify 3 '' "$work/partial-power"
sed '/power_profile/d; /scaling_driver/d; /energy_performance_preference/d' \
    "$work/v2-boundary" >"$work/no-power"
expect_classify 1 provenance-only "$work/no-power"
expect_classify 0 controlled - <"$work/v2-boundary"

cat >"$work/nomad" <<'EOF'
host=nomad-1
governor=unavailable
power_profile=unavailable
scaling_driver=unavailable
energy_performance_preference=unavailable
control_protocol=fleet-control-v2
logical_cpus=18
cpu_idle_pct=85
load1=3.6
EOF
expect_classify 0 controlled "$work/nomad"
sed 's/governor=unavailable/governor=balanced/' "$work/nomad" >"$work/nomad-governor"
expect_classify 3 '' "$work/nomad-governor"
sed 's/power_profile=unavailable/power_profile=performance/' \
    "$work/nomad" >"$work/nomad-power"
expect_classify 3 '' "$work/nomad-power"
sed 's/governor=powersave/governor=powersave,performance/' \
    "$work/v2-boundary" >"$work/mixed-governors"
expect_classify 1 provenance-only "$work/mixed-governors"

mkdir -p "$work/proc"
cat >"$work/proc/stat" <<'EOF'
cpu  100 0 50 800 20 10 10 10 0 0
EOF
printf '2.80 2.00 1.00 1/100 1\n' >"$work/proc/loadavg"
cat >"$work/stat.after" <<'EOF'
cpu  110 0 60 880 30 10 10 10 0 0
EOF
cat >"$work/fake-sleep" <<'EOF'
#!/bin/sh
[ "$1" = 5 ] || exit 1
cp "$CGF_BENCH_CONTROL_TEST_AFTER" "$CGF_BENCH_CONTROL_TEST_STAT"
EOF
cat >"$work/fake-getconf" <<'EOF'
#!/bin/sh
[ "$1" = _NPROCESSORS_ONLN ] || exit 1
printf '%s\n' "${CGF_BENCH_CONTROL_TEST_CPUS:-20}"
EOF
cat >"$work/fake-sysctl" <<'EOF'
#!/bin/sh
[ "$1" = -n ] || exit 1
case $2 in
    hw.logicalcpu) printf '%s\n' "${CGF_BENCH_CONTROL_TEST_SYSCTL_CPUS:-18}" ;;
    vm.loadavg) printf '{ %s 2.00 1.00 }\n' \
        "${CGF_BENCH_CONTROL_TEST_LOAD:-2.80}" ;;
    *) exit 1 ;;
esac
EOF
cat >"$work/fake-top" <<'EOF'
#!/bin/sh
[ "$*" = '-l 2 -s 5 -n 0' ] || exit 1
case ${CGF_BENCH_CONTROL_TEST_TOP_MODE:-valid} in
valid)
    printf '%s\n' \
        'CPU usage: 10.0% user, 10.0% sys, 80.0% idle' \
        'CPU usage: 4.0% user, 4.5% sys, 91.5% idle'
    ;;
one) printf '%s\n' 'CPU usage: 4.0% user, 4.5% sys, 91.5% idle' ;;
*) exit 1 ;;
esac
EOF
chmod +x "$work/fake-sleep" "$work/fake-getconf" "$work/fake-sysctl" "$work/fake-top"

tests=$((tests + 1))
CGF_BENCH_CONTROL_PROC_ROOT="$work/proc" \
CGF_BENCH_CONTROL_SLEEP_CMD="$work/fake-sleep" \
CGF_BENCH_CONTROL_GETCONF_CMD="$work/fake-getconf" \
CGF_BENCH_CONTROL_TEST_AFTER="$work/stat.after" \
CGF_BENCH_CONTROL_TEST_STAT="$work/proc/stat" \
    "$control" measure hasu >"$work/linux-measure"
cat >"$work/linux-expected" <<'EOF'
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=72.73
load1=2.80
EOF
cmp -s "$work/linux-expected" "$work/linux-measure" || {
    diff -u "$work/linux-expected" "$work/linux-measure" >&2 || true
    fail "Linux measurement output mismatch"
}

tests=$((tests + 1))
CGF_BENCH_CONTROL_TOP_CMD="$work/fake-top" \
CGF_BENCH_CONTROL_SYSCTL_CMD="$work/fake-sysctl" \
CGF_BENCH_CONTROL_GETCONF_CMD="$work/fake-getconf" \
CGF_BENCH_CONTROL_TEST_CPUS=unavailable \
    "$control" measure nomad-1 >"$work/darwin-measure"
cat >"$work/darwin-expected" <<'EOF'
control_protocol=fleet-control-v2
logical_cpus=18
cpu_idle_pct=91.5
load1=2.80
EOF
cmp -s "$work/darwin-expected" "$work/darwin-measure" || {
    diff -u "$work/darwin-expected" "$work/darwin-measure" >&2 || true
    fail "Darwin measurement output mismatch"
}

for failure in one-sample invalid-load no-logical-cpus; do
    tests=$((tests + 1))
    top_mode=valid
    load_value=2.80
    sysctl_cpus=18
    case $failure in
    one-sample) top_mode=one ;;
    invalid-load) load_value=busy ;;
    no-logical-cpus) sysctl_cpus=unavailable ;;
    esac
    set +e
    CGF_BENCH_CONTROL_TOP_CMD="$work/fake-top" \
    CGF_BENCH_CONTROL_SYSCTL_CMD="$work/fake-sysctl" \
    CGF_BENCH_CONTROL_GETCONF_CMD="$work/fake-getconf" \
    CGF_BENCH_CONTROL_TEST_CPUS=unavailable \
    CGF_BENCH_CONTROL_TEST_TOP_MODE=$top_mode \
    CGF_BENCH_CONTROL_TEST_LOAD=$load_value \
    CGF_BENCH_CONTROL_TEST_SYSCTL_CPUS=$sysctl_cpus \
        "$control" measure nomad-1 >"$work/out" 2>"$work/err"
    status=$?
    set -e
    [ "$status" -eq 3 ] ||
        fail "$failure Darwin probe returned $status, expected 3"
done

tests=$((tests + 1))
set +e
CGF_BENCH_CONTROL_PROC_ROOT="$work/missing-proc" \
CGF_BENCH_CONTROL_SLEEP_CMD="$work/fake-sleep" \
CGF_BENCH_CONTROL_GETCONF_CMD="$work/fake-getconf" \
    "$control" measure kasumi >"$work/out" 2>"$work/err"
status=$?
set -e
[ "$status" -eq 3 ] || fail "failed Linux probe returned $status, expected 3"

echo "bench_control_test: $tests tests passed"
