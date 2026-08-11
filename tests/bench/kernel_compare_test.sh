#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
compare=${1:-$repo/scripts/kernel-compare.sh}
tmp=${TMPDIR:-/tmp}/cgf-kernel-compare-test.$$
mkdir -p "$tmp/bin" "$tmp/kernels" "$tmp/work-a" "$tmp/work-b"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "kernel_compare_test: $*" >&2
    exit 1
}

cat >"$tmp/kernels/tiny.c" <<'EOF'
static volatile unsigned sink;
__attribute__((noinline)) unsigned kernel_run(void) { return 42; }
int main(void) { unsigned got = kernel_run(); sink = got; return got != 42; }
EOF

cat >"$tmp/bin/fake-cgf" <<'EOF'
#!/bin/sh
set -eu
if [ -n "${FIXTURE_CGF_LOG:-}" ]; then
    {
        echo BEGIN
        for arg do echo "$arg"; done
        echo END
    } >>"$FIXTURE_CGF_LOG"
fi
set -- "$@"
new=
for arg do
    case $arg in
    --target=*) ;;
    *)
        if [ -z "$new" ]; then
            new=$arg
        else
            new="$new
$arg"
        fi
        ;;
    esac
done
set --
old_ifs=$IFS
IFS='
'
for arg in $new; do set -- "$@" "$arg"; done
IFS=$old_ifs
exec gcc "$@"
EOF
chmod +x "$tmp/bin/fake-cgf"

cat >"$tmp/runtime.txt" <<'EOF'
# cgfried kernel runtime metrics v1
date=2026-08-10T12:00:00Z
host=fixture-host
target=x86_64-linux-gnu
x86_64-linux-gnu.tiny.O2.cgf.wall_ms_median=12.000000
x86_64-linux-gnu.tiny.O2.cgf.wall_ms_mad=0.100000
x86_64-linux-gnu.tiny.O2.gcc.wall_ms_median=8.000000
x86_64-linux-gnu.tiny.O2.gcc.wall_ms_mad=0.100000
EOF

run_compare()
{
    run_work=$1
    run_output=$2
    run_runtime=$3
    CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
    CGF_KERNEL_DIR=$tmp/kernels \
    CGF_KERNEL_COMPARE_WORK=$run_work \
    CGF_KERNEL_TARGETS=x86_64-linux-gnu \
    CGF_KERNEL_OPTS=O2 \
    CGF_KERNEL_MIN=1 \
    CGF_KERNEL_DASHBOARD_SCOPE_KIND=host_class \
    CGF_KERNEL_DASHBOARD_SCOPE=fixture-deterministic \
    CGF_KERNEL_DASHBOARD_DATE_UTC=2026-08-02T15:00:00Z \
    CGF_KERNEL_DASHBOARD_REV=fixture-dashboard-rev \
    CGF_KERNEL_DASHBOARD_TREE_STATE=clean \
    CGF_KERNEL_DASHBOARD_PROTOCOL=fixture-static-dashboard-v1 \
    CGF_KERNEL_RUNTIME_INPUT=$run_runtime \
        "$compare" "$run_output" >"$run_work/stdout.txt"
}

run_compare "$tmp/work-a" "$tmp/a.md" "$tmp/runtime.txt"
run_compare "$tmp/work-b" "$tmp/b.md" "$tmp/runtime.txt"
cmp -s "$tmp/a.md" "$tmp/b.md" || fail "static dashboard is not deterministic"
grep -F '| tiny | -O2 |' "$tmp/a.md" >/dev/null || fail "missing static row"
grep -F '| 1.500x |' "$tmp/a.md" >/dev/null || fail "runtime ratio is wrong"
grep -F 'fixture-host' "$tmp/a.md" >/dev/null || fail "runtime provenance is missing"
grep -Fx '<!-- cgf-dashboard-provenance host_class=fixture-deterministic -->' \
    "$tmp/a.md" >/dev/null || fail "dashboard scope provenance is missing"
grep -Fx '<!-- cgf-dashboard-provenance date_utc=2026-08-02T15:00:00Z -->' \
    "$tmp/a.md" >/dev/null || fail "dashboard date override is missing"
grep -Fx '<!-- cgf-dashboard-provenance cgf_rev=fixture-dashboard-rev -->' \
    "$tmp/a.md" >/dev/null || fail "dashboard revision override is missing"
grep -Fx '<!-- cgf-dashboard-provenance cgf_tree=clean -->' \
    "$tmp/a.md" >/dev/null || fail "dashboard tree override is missing"
grep -Fx '<!-- cgf-dashboard-provenance protocol=fixture-static-dashboard-v1 -->' \
    "$tmp/a.md" >/dev/null || fail "dashboard protocol override is missing"
# Backticks below are Markdown literals, not shell substitutions.
# shellcheck disable=SC2016
grep -F -- '- `dashboard.cgf_rev`: `fixture-dashboard-rev`' "$tmp/a.md" \
    >/dev/null || fail "visible dashboard provenance is missing"

if CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
   CGF_KERNEL_DIR=$tmp/kernels CGF_KERNEL_COMPARE_WORK=$tmp/work-bad-scope \
   CGF_KERNEL_TARGETS=x86_64-linux-gnu CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
   CGF_KERNEL_DASHBOARD_SCOPE_KIND=machine \
       "$compare" "$tmp/bad-scope.md" >"$tmp/bad-scope.out" 2>"$tmp/bad-scope.err"; then
    fail "invalid dashboard scope kind passed"
fi
grep -F 'CGF_KERNEL_DASHBOARD_SCOPE_KIND must be host or host_class' \
    "$tmp/bad-scope.err" >/dev/null || fail "invalid dashboard scope diagnostic is missing"

if CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
   CGF_KERNEL_DIR=$tmp/kernels CGF_KERNEL_COMPARE_WORK=$tmp/work-bad-provenance \
   CGF_KERNEL_TARGETS=x86_64-linux-gnu CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
   CGF_KERNEL_DASHBOARD_PROTOCOL='bad protocol' \
       "$compare" "$tmp/bad-provenance.md" >"$tmp/bad-provenance.out" \
       2>"$tmp/bad-provenance.err"; then
    fail "unsafe dashboard provenance passed"
fi
grep -F 'dashboard protocol must be a nonempty Markdown-safe token' \
    "$tmp/bad-provenance.err" >/dev/null ||
    fail "unsafe dashboard provenance diagnostic is missing"

if CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
   CGF_KERNEL_DIR=$tmp/kernels CGF_KERNEL_COMPARE_WORK=$tmp/work-bad-date \
   CGF_KERNEL_TARGETS=x86_64-linux-gnu CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
   CGF_KERNEL_DASHBOARD_DATE_UTC=2026-02-30T12:00:00Z \
       "$compare" "$tmp/bad-date.md" >"$tmp/bad-date.out" 2>"$tmp/bad-date.err"; then
    fail "invalid dashboard UTC date passed"
fi
grep -F 'dashboard date provenance must be a valid UTC timestamp' \
    "$tmp/bad-date.err" >/dev/null || fail "invalid dashboard date diagnostic is missing"

if CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
   CGF_KERNEL_DIR=$tmp/kernels CGF_KERNEL_COMPARE_WORK=$tmp/work-bad-tree \
   CGF_KERNEL_TARGETS=x86_64-linux-gnu CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
   CGF_KERNEL_DASHBOARD_TREE_STATE=fixture-clean \
       "$compare" "$tmp/bad-tree.md" >"$tmp/bad-tree.out" 2>"$tmp/bad-tree.err"; then
    fail "invalid dashboard tree state passed"
fi
grep -F 'dashboard tree provenance is invalid' "$tmp/bad-tree.err" >/dev/null ||
    fail "invalid dashboard tree diagnostic is missing"

sed 's/12[.]000000/12.008000/' "$tmp/runtime.txt" >"$tmp/runtime-hot.txt"
mkdir -p "$tmp/work-hot"
run_compare "$tmp/work-hot" "$tmp/hot.md" "$tmp/runtime-hot.txt"
grep -F '| 1.501x ⚠ |' "$tmp/hot.md" >/dev/null ||
    fail "ratio above 1.5x is not marked for follow-up"

cat >"$tmp/runtime-incomplete.txt" <<'EOF'
date=2026-08-10T12:00:00Z
host=fixture-host
target=x86_64-linux-gnu
x86_64-linux-gnu.tiny.O2.cgf.wall_ms_median=12.000000
EOF
if CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
   CGF_KERNEL_DIR=$tmp/kernels \
   CGF_KERNEL_COMPARE_WORK=$tmp/work-incomplete \
   CGF_KERNEL_TARGETS=x86_64-linux-gnu \
   CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
   CGF_KERNEL_RUNTIME_INPUT=$tmp/runtime-incomplete.txt \
       "$compare" "$tmp/incomplete.md" >"$tmp/incomplete.out" \
       2>"$tmp/incomplete.err"; then
    fail "incomplete runtime pair passed"
fi
grep -F 'incomplete runtime median/MAD pair' "$tmp/incomplete.err" >/dev/null ||
    fail "incomplete runtime diagnostic is missing"

cat >"$tmp/bin/uname" <<'EOF'
#!/bin/sh
case ${1:-} in
-s) echo "${FIXTURE_UNAME_SYSTEM:-Darwin}" ;;
-m) echo "${FIXTURE_UNAME_MACHINE:-arm64}" ;;
-n) echo fixture-mac ;;
*) echo "${FIXTURE_UNAME_SYSTEM:-Darwin}" ;;
esac
EOF
cat >"$tmp/bin/powerprofilesctl" <<'EOF'
#!/bin/sh
[ "${1:-}" = get ] || exit 2
echo "${FIXTURE_POWER_PROFILE:-performance}"
EOF
cat >"$tmp/bin/fake-bench-control" <<'EOF'
#!/bin/sh
set -eu
case ${1:-} in
measure)
    [ "$#" -eq 2 ] || exit 3
    cat <<MEASURE
control_protocol=fleet-control-v2
logical_cpus=${FIXTURE_LOGICAL_CPUS:-8}
cpu_idle_pct=${FIXTURE_CPU_IDLE_PCT:-99.00}
load1=${FIXTURE_LOAD1:-0.25}
MEASURE
    ;;
classify)
    [ "${2:-}" = --require-v2 ] && [ "$#" -eq 3 ] || exit 3
    [ -r "$3" ] || exit 3
    [ -z "${FIXTURE_CONTROL_LOG:-}" ] || cp "$3" "$FIXTURE_CONTROL_LOG"
    exit "${FIXTURE_CONTROL_STATUS:-0}"
    ;;
*) exit 3 ;;
esac
EOF
cat >"$tmp/bin/fake-timeit" <<'EOF'
#!/bin/sh
set -eu
raw=
while [ "$#" -gt 0 ]; do
    case $1 in
    -o) raw=$2; shift 2 ;;
    -t | -n | -w) shift 2 ;;
    --) shift; break ;;
    *) exit 2 ;;
    esac
done
"$@"
printf '1.000000\n' >"$raw"
echo 'wall_ms_median=1.000000'
echo 'wall_ms_mad=0.000000'
EOF
chmod +x "$tmp/bin/uname" "$tmp/bin/fake-timeit" \
    "$tmp/bin/powerprofilesctl" "$tmp/bin/fake-bench-control"

mkdir -p "$tmp/work-runtime"
: >"$tmp/runtime-cgf.log"
PATH=$tmp/bin:$PATH \
FIXTURE_CGF_LOG=$tmp/runtime-cgf.log \
FIXTURE_CONTROL_LOG=$tmp/macos-control.log \
CGF_KERNEL_CGF=$tmp/bin/fake-cgf \
CGF_KERNEL_CONTROL_SCRIPT=$tmp/bin/fake-bench-control \
CGF_KERNEL_GCC_ARM64_MACOS=$tmp/bin/fake-cgf \
CGF_KERNEL_TIMEIT=$tmp/bin/fake-timeit \
CGF_KERNEL_DIR=$tmp/kernels \
CGF_KERNEL_COMPARE_WORK=$tmp/work-runtime \
CGF_KERNEL_TARGETS=arm64-macos \
CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
CGF_KERNEL_RUNS=1 CGF_KERNEL_WARMUP=0 CGF_KERNEL_FORCE=1 \
CGF_KERNEL_COOLDOWN_SECONDS=0 \
CGF_KERNEL_RUNTIME_HOST=nomad-1 \
CGF_KERNEL_RUNTIME_OUTPUT=$tmp/macos-runtime.txt \
CGF_KERNEL_RUNTIME_ONLY=1 \
CGF_FLEET_SYSROOT_INCLUDE=/nix/store/fixture-glibc-dev/include \
CGF_FLEET_SYSROOT_CRT=/nix/store/fixture-glibc/lib \
    "$compare" >"$tmp/runtime-only.out"
grep -F 'target=arm64-macos' "$tmp/macos-runtime.txt" >/dev/null ||
    fail "Darwin arm64 runtime was not recorded as arm64-macos"
grep -F 'arm64-macos.tiny.O2.cgf.wall_ms_median=1.000000' \
    "$tmp/macos-runtime.txt" >/dev/null || fail "runtime-only metric is missing"
grep -F 'sysroot_include=/nix/store/fixture-glibc-dev/include' \
    "$tmp/macos-runtime.txt" >/dev/null || fail "runtime include provenance is missing"
grep -F 'sysroot_crt=/nix/store/fixture-glibc/lib' \
    "$tmp/macos-runtime.txt" >/dev/null || fail "runtime CRT provenance is missing"
grep -F 'cgf_sdk_compat=arm64-macos-kernel-runtime-v1' \
    "$tmp/macos-runtime.txt" >/dev/null || fail "runtime SDK compatibility provenance is missing"
for field in power_profile scaling_driver energy_performance_preference; do
    [ "$(grep -c "^$field=unavailable$" "$tmp/macos-runtime.txt")" -eq 1 ] ||
        fail "Darwin runtime did not record exactly one unavailable $field"
done
for field_value in control_protocol=fleet-control-v2 \
    logical_cpus=8 cpu_idle_pct=99.00 load1=0.25; do
    [ "$(grep -Fxc "$field_value" "$tmp/macos-runtime.txt")" -eq 1 ] ||
        fail "Darwin v2 control provenance missing: $field_value"
done
grep -Fx 'host=nomad-1' "$tmp/macos-control.log" >/dev/null ||
    fail "Darwin classifier input lacks host provenance"
[ "$(grep -Fxc "$repo/tests/bench/compat/arm64-macos-self-syntax.h" \
    "$tmp/runtime-cgf.log")" -eq 1 ] ||
    fail "macOS compatibility header was not scoped to the cgf runtime compile"
[ "$(grep -Fxc "$repo/tests/bench/compat/arm64-macos-self-overlay" \
    "$tmp/runtime-cgf.log")" -eq 1 ] ||
    fail "macOS SDK overlay was not scoped to the cgf runtime compile"
[ ! -e "$tmp/work-runtime/static.txt" ] ||
    fail "runtime-only mode traversed static measurement"
[ ! -e "$tmp/work-runtime/dashboard.tmp.md" ] ||
    fail "runtime-only mode rendered a static dashboard"

mkdir -p "$tmp/sys-cpu/cpu0/cpufreq" \
    "$tmp/sys-cpu/cpu1/cpufreq" "$tmp/work-linux-runtime"
for cpu in cpu0 cpu1; do
    printf 'powersave\n' >"$tmp/sys-cpu/$cpu/cpufreq/scaling_governor"
    printf 'intel_pstate\n' >"$tmp/sys-cpu/$cpu/cpufreq/scaling_driver"
    printf 'performance\n' \
        >"$tmp/sys-cpu/$cpu/cpufreq/energy_performance_preference"
done
PATH=$tmp/bin:$PATH \
FIXTURE_UNAME_SYSTEM=Linux FIXTURE_UNAME_MACHINE=x86_64 \
FIXTURE_POWER_PROFILE=performance \
FIXTURE_CONTROL_LOG=$tmp/linux-control.log \
CGF_KERNEL_SYS_CPU_ROOT=$tmp/sys-cpu \
CGF_KERNEL_CONTROL_SCRIPT=$tmp/bin/fake-bench-control \
CGF_KERNEL_CGF=$tmp/bin/fake-cgf CGF_KERNEL_TIMEIT=$tmp/bin/fake-timeit \
CGF_KERNEL_DIR=$tmp/kernels CGF_KERNEL_COMPARE_WORK=$tmp/work-linux-runtime \
CGF_KERNEL_TARGETS=x86_64-linux-gnu CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
CGF_KERNEL_RUNS=1 CGF_KERNEL_WARMUP=0 CGF_KERNEL_COOLDOWN_SECONDS=0 \
CGF_KERNEL_RUNTIME_HOST=kasumi CGF_KERNEL_RUNTIME_OUTPUT=$tmp/linux-runtime.txt \
CGF_KERNEL_RUNTIME_ONLY=1 "$compare" >"$tmp/linux-runtime.out" \
    2>"$tmp/linux-runtime.err"
grep -Fx 'load1=0.25' "$tmp/linux-runtime.txt" >/dev/null ||
    fail "Linux runtime load provenance is missing"
grep -Fx 'governor=powersave' "$tmp/linux-runtime.txt" >/dev/null ||
    fail "intel_pstate raw powersave governor was mislabeled"
for field_value in power_profile=performance scaling_driver=intel_pstate \
    energy_performance_preference=performance; do
    [ "$(grep -Fxc "$field_value" "$tmp/linux-runtime.txt")" -eq 1 ] ||
        fail "Linux runtime did not record exactly one $field_value"
done
if grep -F 'runtime controls are not performance-controlled' \
    "$tmp/linux-runtime.err" >/dev/null; then
    fail "controlled intel_pstate powersave mode was marked provenance-only"
fi
for field_value in host=kasumi governor=powersave power_profile=performance \
    scaling_driver=intel_pstate energy_performance_preference=performance \
    control_protocol=fleet-control-v2 logical_cpus=8 \
    cpu_idle_pct=99.00 load1=0.25; do
    [ "$(grep -Fxc "$field_value" "$tmp/linux-control.log")" -eq 1 ] ||
        fail "Linux classifier input missing: $field_value"
done

mkdir "$tmp/work-linux-refused"
set +e
PATH=$tmp/bin:$PATH \
FIXTURE_UNAME_SYSTEM=Linux FIXTURE_UNAME_MACHINE=x86_64 \
FIXTURE_POWER_PROFILE=performance FIXTURE_CONTROL_STATUS=1 \
CGF_KERNEL_SYS_CPU_ROOT=$tmp/sys-cpu \
CGF_KERNEL_CONTROL_SCRIPT=$tmp/bin/fake-bench-control \
CGF_KERNEL_CGF=$tmp/bin/fake-cgf CGF_KERNEL_TIMEIT=$tmp/bin/fake-timeit \
CGF_KERNEL_DIR=$tmp/kernels CGF_KERNEL_COMPARE_WORK=$tmp/work-linux-refused \
CGF_KERNEL_TARGETS=x86_64-linux-gnu CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
CGF_KERNEL_RUNS=1 CGF_KERNEL_WARMUP=0 CGF_KERNEL_COOLDOWN_SECONDS=0 \
CGF_KERNEL_RUNTIME_HOST=kasumi \
CGF_KERNEL_RUNTIME_OUTPUT=$tmp/linux-refused.txt \
CGF_KERNEL_RUNTIME_ONLY=1 "$compare" >"$tmp/linux-refused.out" \
    2>"$tmp/linux-refused.err"
refused_status=$?
set -e
[ "$refused_status" -eq 3 ] ||
    fail "unforced provenance-only kernel runtime was not refused"
grep -F 'capacity/idle controls are provenance-only (set CGF_KERNEL_FORCE=1 to record)' \
    "$tmp/linux-refused.err" >/dev/null ||
    fail "kernel capacity/idle refusal diagnostic is missing"
[ ! -e "$tmp/linux-refused.txt" ] ||
    fail "refused kernel runtime emitted an artifact"

printf 'balance_performance\n' \
    >"$tmp/sys-cpu/cpu0/cpufreq/energy_performance_preference"
printf 'balance_performance\n' \
    >"$tmp/sys-cpu/cpu1/cpufreq/energy_performance_preference"
mkdir "$tmp/work-linux-uncontrolled"
PATH=$tmp/bin:$PATH \
FIXTURE_UNAME_SYSTEM=Linux FIXTURE_UNAME_MACHINE=x86_64 \
FIXTURE_POWER_PROFILE=performance FIXTURE_CONTROL_STATUS=1 \
CGF_KERNEL_SYS_CPU_ROOT=$tmp/sys-cpu \
CGF_KERNEL_CONTROL_SCRIPT=$tmp/bin/fake-bench-control CGF_KERNEL_FORCE=1 \
CGF_KERNEL_CGF=$tmp/bin/fake-cgf CGF_KERNEL_TIMEIT=$tmp/bin/fake-timeit \
CGF_KERNEL_DIR=$tmp/kernels CGF_KERNEL_COMPARE_WORK=$tmp/work-linux-uncontrolled \
CGF_KERNEL_TARGETS=x86_64-linux-gnu CGF_KERNEL_OPTS=O2 CGF_KERNEL_MIN=1 \
CGF_KERNEL_RUNS=1 CGF_KERNEL_WARMUP=0 CGF_KERNEL_COOLDOWN_SECONDS=0 \
CGF_KERNEL_RUNTIME_HOST=kasumi \
CGF_KERNEL_RUNTIME_OUTPUT=$tmp/linux-uncontrolled.txt \
CGF_KERNEL_RUNTIME_ONLY=1 "$compare" >"$tmp/linux-uncontrolled.out" \
    2>"$tmp/linux-uncontrolled.err"
grep -F 'WARNING: capacity/idle controls are provenance-only; forced recording enabled' \
    "$tmp/linux-uncontrolled.err" >/dev/null ||
    fail "uncontrolled intel_pstate mode warning is incomplete"

echo "kernel_compare_test: static comparison, runtime provenance, Darwin compatibility, and Linux power-control classification passed"
