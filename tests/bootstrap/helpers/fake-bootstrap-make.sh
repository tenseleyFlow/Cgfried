#!/bin/sh
set -eu

: "${CGF_BOOTSTRAP_TEST_MAKE_LOG:?}"
printf '%s\n' "$*" >>"$CGF_BOOTSTRAP_TEST_MAKE_LOG"
work=
host=
for argument in "$@"; do
    case $argument in
    BOOTSTRAP_WORK=*) work=${argument#BOOTSTRAP_WORK=} ;;
    BOOTSTRAP_HOST=*) host=${argument#BOOTSTRAP_HOST=} ;;
    esac
done
[ -n "$work" ] && [ -n "$host" ] || exit 2
mkdir -p "$work/O2"
cat >"$work/O2/stage1-time.txt" <<EOF
schema=cgfried.bootstrap-timing.v1
target=x86_64-linux-gnu
host=$host
governor=powersave
power_profile=performance
scaling_driver=intel_pstate
energy_performance_preference=performance
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=99.00
load1=0.20
date=2026-08-12T12:00:00Z
cgf_rev=0123456789abcdef
cgf_tree=clean
protocol=cgfried-bootstrap-v1
samples=3
level=O2
jobs=8
normalization=none
sysroot=none
compiler=/fixture/cgfried
compiler_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
stage1.O2.wall_ms_median=${CGF_BOOTSTRAP_TEST_WALL:-100}
stage1.O2.wall_ms_mad=0
stage1.O2.user_ms_median=${CGF_BOOTSTRAP_TEST_USER:-80}
stage1.O2.user_ms_mad=0
stage1.O2.sys_ms_median=${CGF_BOOTSTRAP_TEST_SYS:-20}
stage1.O2.sys_ms_mad=0
stage1.O2.cpu_ms_median=${CGF_BOOTSTRAP_TEST_CPU:-100}
stage1.O2.cpu_ms_mad=0
stage1.O2.maxrss_kb_max=1000
EOF
