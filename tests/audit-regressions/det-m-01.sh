#!/bin/sh
# RESOLVED(audit): DET-M-01 blocking compile gates have no per-metric noise evidence
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT_INPUT=${1:-$SCRIPT_DIR/../..}
ROOT=$(CDPATH= cd -- "$ROOT_INPUT" && pwd) || exit 2
GATE=$ROOT/scripts/benchmark_gate.sh
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cgf-det-m-01.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

[ -x "$GATE" ] || exit 2

cat >"$TMP/baseline" <<'EOF'
target=x86_64-linux-gnu
host=kasumi
governor=performance
power_profile=performance
scaling_driver=acpi-cpufreq
energy_performance_preference=unavailable
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=90.00
load1=0.10
runs=10
warmup=1
timeit_protocol=sprint-52-compile-median-mad-v1
lane_order=self
sqlite_version=3500400
sqlite_sha3=fixture-sha3
sqlite_cksum=123:456
sqlite3.corpus=sqlite-amalgamation-3500400
self.corpus=cgfried-src-fixture:12-files
many-tu.corpus=cgf-many-tu-v1:500x200
self_limit=0
self.wall_ms_median=100
self.wall_ms_mad=10
self.user_ms_median=60
self.user_ms_mad=6
self.sys_ms_median=40
self.sys_ms_mad=4
self.cpu_ms_median=100
self.cpu_ms_mad=10
self.maxrss_kb_median=900
self.maxrss_kb_mad=100
self.maxrss_kb_max=1000
EOF

sed -e 's/self.wall_ms_median=100/self.wall_ms_median=131/' \
    -e 's/self.user_ms_median=60/self.user_ms_median=80/' \
    -e 's/self.sys_ms_median=40/self.sys_ms_median=51/' \
    -e 's/self.cpu_ms_median=100/self.cpu_ms_median=131/' \
    -e 's/self.maxrss_kb_median=900/self.maxrss_kb_median=1100/' \
    -e 's/self.maxrss_kb_max=1000/self.maxrss_kb_max=1210/' \
    "$TMP/baseline" >"$TMP/noisy-current" || exit 2

status=0
"$GATE" "$TMP/baseline" "$TMP/noisy-current" >"$TMP/out" 2>"$TMP/err" || status=$?
if [ "$status" -eq 1 ]; then
    echo 'DET-M-01 reproduced: a change inside four MADs is still blocked'
    exit 0
fi
[ "$status" -eq 0 ] || exit 2

sed 's/_mad=[0-9][0-9]*/_mad=0/' "$TMP/baseline" >"$TMP/quiet-baseline" || exit 2
sed 's/_mad=[0-9][0-9]*/_mad=0/' "$TMP/noisy-current" >"$TMP/quiet-current" || exit 2
status=0
"$GATE" "$TMP/quiet-baseline" "$TMP/quiet-current" >"$TMP/out" 2>"$TMP/err" || status=$?
if [ "$status" -eq 0 ]; then
    echo 'DET-M-01 reproduced: a real low-noise regression was accepted'
    exit 0
fi
[ "$status" -eq 1 ] || exit 2
for metric in self.wall_ms_median self.cpu_ms_median self.maxrss_kb_max; do
    grep -Fq "$metric regressed" "$TMP/err" || exit 2
done

# Paired CPU observations, rather than a sum of independently selected user
# and sys medians, govern the CPU gate.
sed 's/self.user_ms_median=60/self.user_ms_median=999/' \
    "$TMP/quiet-baseline" >"$TMP/paired-current" || exit 2
status=0
"$GATE" "$TMP/quiet-baseline" "$TMP/paired-current" >"$TMP/out" 2>"$TMP/err" || status=$?
if [ "$status" -eq 1 ]; then
    echo 'DET-M-01 reproduced: marginal user time still drives the CPU gate'
    exit 0
fi
[ "$status" -eq 0 ] || exit 2

for metric in wall_ms_mad cpu_ms_mad maxrss_kb_mad; do
    sed "/self[.]$metric=/d" "$TMP/noisy-current" >"$TMP/missing" || exit 2
    status=0
    "$GATE" "$TMP/baseline" "$TMP/missing" >"$TMP/out" 2>"$TMP/err" || status=$?
    if [ "$status" -eq 0 ] || [ "$status" -eq 1 ]; then
        echo "DET-M-01 reproduced: missing self.$metric was not rejected as invalid evidence"
        exit 0
    fi
    [ "$status" -eq 3 ] || exit 2
    grep -Fq "missing required result metric self.$metric" "$TMP/err" || exit 2
done

exit 1
