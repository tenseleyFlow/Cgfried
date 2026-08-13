#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
gate=$root/scripts/musl-full-build-gate.sh
bench=$root/scripts/musl-full-build-bench.sh
fleet=$root/scripts/fleet-musl-build.sh
once=$root/scripts/musl-full-build-once.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-musl-full-build-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "musl_full_build_test: $*" >&2
    exit 1
}

grep -F 'export CGF_AS_PATH CGF_LD_PATH' "$once" >/dev/null ||
    fail 'real musl build helper does not pin explicit assembler/linker routing'

expect()
{
    want=$1
    pattern=$2
    shift 2
    status=0
    "$@" >"$tmp/out" 2>"$tmp/err" || status=$?
    [ "$status" -eq "$want" ] || fail "expected status $want, got $status: $(cat "$tmp/err")"
    grep -F "$pattern" "$tmp/out" "$tmp/err" >/dev/null || fail "missing output: $pattern"
}

receipt()
{
    file=$1
    host=$2
    wall=$3
    user=$4
    sys=$5
    rss=$6
    {
        echo 'schema=cgfried.musl-full-build.v1'
        echo "host=$host"
        echo 'target=x86_64-linux-musl'
        echo 'workload=musl-full-static-hybrid'
        echo 'musl_commit=b306b16af15c89a04d8e0c55cac2dadbeb39c083'
        echo 'date=2026-08-13T12:00:00Z'
        echo 'cgf_rev=1111111111111111111111111111111111111111'
        echo 'cgf_tree=clean'
        echo 'compiler_wrapper=scripts/campaigns/musl-cc.sh'
        echo 'source_date_epoch=0'
        echo 'jobs=1'
        echo 'runs=10'
        echo 'warmup=1'
        echo 'timeit_protocol=runs=10,warmup=1;fresh-tree-per-sample;source-date-epoch=0;jobs=1'
        echo 'route.cgf_c=1254'
        echo 'route.host_complex=68'
        echo 'route.host_assembler=32'
        echo 'route.total=1354'
        echo 'control_protocol=fleet-control-v2'
        echo 'logical_cpus=20'
        echo 'cpu_idle_pct=95.0'
        echo 'load1=0.20'
        echo 'governor=performance'
        echo 'power_profile=performance'
        echo 'scaling_driver=acpi-cpufreq'
        echo 'energy_performance_preference=performance'
        echo "wall_ms_median=$wall"
        echo 'wall_ms_mad=10'
        echo "user_ms_median=$user"
        echo 'user_ms_mad=10'
        echo "sys_ms_median=$sys"
        echo 'sys_ms_mad=5'
        echo "maxrss_kb_max=$rss"
        echo 'raw.wall_ms=1,2,3,4,5,6,7,8,9,10'
        echo 'raw.user_ms=1,2,3,4,5,6,7,8,9,10'
        echo 'raw.sys_ms=1,2,3,4,5,6,7,8,9,10'
        echo 'raw.maxrss_kb=1,2,3,4,5,6,7,8,9,10'
        echo 'musl.stat.arena.ast.peak_kb_max=10'
        echo 'musl.stat.arena.ast.blocks_max=2'
        echo 'musl.stat.arena.ast.waste_pct_max=20'
        echo 'musl.stat.arena.ir.peak_kb_max=20'
        echo 'musl.stat.arena.ir.blocks_max=3'
        echo 'musl.stat.arena.ir.waste_pct_max=10'
        echo 'musl.stat.intern.lookups_sum=1000'
        echo 'musl.stat.intern.hits_sum=900'
        echo 'musl.stat.intern.hit_pct=90'
        echo 'musl.stat.pp.includes_sum=50'
        echo 'musl.stat.pp.guard_skips_sum=40'
        echo 'musl.stat.pp.tokens_sum=10000'
    } >"$file"
}

receipt "$tmp/base" kasumi 1000 700 300 1000
receipt "$tmp/pass" kasumi 1300 900 400 1200
expect 0 'pass (+30% time, +20% RSS)' "$gate" "$tmp/base" "$tmp/pass"
receipt "$tmp/wall" kasumi 1300.1 700 300 1000
expect 1 'wall time regressed beyond +30%' "$gate" "$tmp/base" "$tmp/wall"
receipt "$tmp/cpu" kasumi 1000 1000 300.1 1000
expect 1 'user+sys time regressed beyond +30%' "$gate" "$tmp/base" "$tmp/cpu"
receipt "$tmp/rss" kasumi 1000 700 300 1200.1
expect 1 'maximum RSS regressed beyond +20%' "$gate" "$tmp/base" "$tmp/rss"
receipt "$tmp/host" hasu 1000 700 300 1000
expect 3 'host does not match baseline' "$gate" "$tmp/base" "$tmp/host"
sed '/^route.cgf_c=/d' "$tmp/pass" >"$tmp/routes"
expect 3 'result missing route.cgf_c' "$gate" "$tmp/base" "$tmp/routes"
sed 's/^musl_commit=.*/musl_commit=wrong/' "$tmp/pass" >"$tmp/pin"
expect 3 'wrong musl pin' "$gate" "$tmp/base" "$tmp/pin"
sed 's/^control_protocol=.*/control_protocol=legacy/' "$tmp/pass" >"$tmp/control"
expect 3 'uncontrolled evidence' "$gate" "$tmp/base" "$tmp/control"
printf 'not-a-receipt\n' >"$tmp/malformed"
expect 3 'uncontrolled evidence' "$gate" "$tmp/base" "$tmp/malformed"
sed 's/^cgf_tree=clean$/cgf_tree=dirty/' "$tmp/base" >"$tmp/dirty-base"
sed 's/^cgf_tree=clean$/cgf_tree=dirty/' "$tmp/pass" >"$tmp/dirty-result"
expect 3 'controlled evidence requires cgf_tree=clean' \
    "$gate" "$tmp/dirty-base" "$tmp/dirty-result"

cat >"$tmp/classifier" <<'EOF'
#!/bin/sh
echo controlled
EOF
cat >"$tmp/once" <<'EOF'
#!/bin/sh
out=$3
mkdir -p "$2/heavy-build-tree"
: >"$2/heavy-build-tree/object.o"
case $out in
*/warmup.txt) wall=999; user=999; sys=999; rss=9999 ;;
*/sample-01.txt) wall=100; user=70; sys=20; rss=1000 ;;
*/sample-02.txt) wall=300; user=90; sys=30; rss=1200 ;;
*) wall=200; user=80; sys=40; rss=1100 ;;
esac
mkdir -p "$(dirname "$out")"
cat >"$out" <<EOT
schema=cgfried.musl-full-build-sample.v1
musl_commit=b306b16af15c89a04d8e0c55cac2dadbeb39c083
target=x86_64-linux-musl
compiler_wrapper=scripts/campaigns/musl-cc.sh
source_date_epoch=0
jobs=1
route.cgf_c=1254
route.host_complex=68
route.host_assembler=32
route.total=1354
wall_ms_median=$wall
user_ms_median=$user
sys_ms_median=$sys
maxrss_kb_max=$rss
musl.stat.arena.ast.peak_kb_max=10
musl.stat.arena.ast.blocks_max=2
musl.stat.arena.ast.waste_pct_max=20
musl.stat.arena.ir.peak_kb_max=20
musl.stat.arena.ir.blocks_max=3
musl.stat.arena.ir.waste_pct_max=10
musl.stat.intern.lookups_sum=1000
musl.stat.intern.hits_sum=900
musl.stat.intern.hit_pct=90
musl.stat.pp.includes_sum=50
musl.stat.pp.guard_skips_sum=40
musl.stat.pp.tokens_sum=10000
EOT
EOF
chmod +x "$tmp/classifier" "$tmp/once"
cat >"$tmp/control-receipt" <<'EOF'
host=kasumi
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=95.0
load1=0.20
governor=performance
power_profile=performance
scaling_driver=acpi-cpufreq
energy_performance_preference=performance
EOF
CGF_MUSL_BUILD_ONCE="$tmp/once" CGF_MUSL_BUILD_CLASSIFIER="$tmp/classifier" \
CGF_MUSL_BUILD_CONTROL_RECEIPT="$tmp/control-receipt" CGF_MUSL_BUILD_HOST=kasumi \
CGF_MUSL_BUILD_CGF_TREE=clean \
CGF_MUSL_BUILD_DATE_UTC=2026-08-13T12:00:00Z \
CGF_MUSL_BUILD_CGF_REVISION=1111111111111111111111111111111111111111 \
    "$bench" "$tmp/source" "$tmp/bench work" "$tmp/bench.txt"
grep -F 'wall_ms_median=200' "$tmp/bench.txt" >/dev/null || fail 'bench median is wrong'
grep -F 'wall_ms_mad=0' "$tmp/bench.txt" >/dev/null || fail 'bench MAD is wrong'
grep -F 'maxrss_kb_max=1200' "$tmp/bench.txt" >/dev/null || fail 'bench RSS maximum is wrong'
grep -F 'raw.wall_ms=100,300,200,200,200,200,200,200,200,200' "$tmp/bench.txt" >/dev/null || fail 'raw sample order is wrong'
[ ! -e "$tmp/bench work/warmup" ] || fail 'completed warmup build tree was retained'
[ ! -e "$tmp/bench work/run-10" ] || fail 'completed sample build tree was retained'
expect 3 'Cgfried tree must be clean before measurement' env \
    CGF_MUSL_BUILD_ONCE="$tmp/once" CGF_MUSL_BUILD_CLASSIFIER="$tmp/classifier" \
    CGF_MUSL_BUILD_CONTROL_RECEIPT="$tmp/control-receipt" CGF_MUSL_BUILD_HOST=kasumi \
    CGF_MUSL_BUILD_CGF_TREE=dirty \
    CGF_MUSL_BUILD_DATE_UTC=2026-08-13T12:00:00Z \
    CGF_MUSL_BUILD_CGF_REVISION=1111111111111111111111111111111111111111 \
    "$bench" "$tmp/source" "$tmp/dirty-bench-work" "$tmp/dirty-bench.txt"

cat >"$tmp/once-git" <<'EOF'
#!/bin/sh
case $* in
*'rev-parse --verify HEAD'*)
    echo b306b16af15c89a04d8e0c55cac2dadbeb39c083
    exit 0
    ;;
*) exit 2 ;;
esac
EOF
cat >"$tmp/once-timeit" <<'EOF'
#!/bin/sh
set -eu
raw=
while [ "$#" -gt 0 ]; do
    case $1 in
    -o) raw=$2; shift 2 ;;
    --) shift; break ;;
    *) shift ;;
    esac
done
: >"$raw"
work=${FIXTURE_ONCE_WORK:?}
mkdir -p "$work/routes" "$work/logs"
awk 'BEGIN {
    for (i=1;i<=1254;i++) printf "cgf\t/src path/file-%04d.c\n",i
    for (i=1;i<=68;i++) printf "host\t/src/complex/file-%04d.c\n",i
    for (i=1;i<=32;i++) printf "host\t/src/arch/file-%04d.S\n",i
}' >"$work/routes/all routes.route"
awk 'BEGIN {
    for (i=1;i<=1254;i++) {
        print "stat: arena.ast peak_kb=10 blocks=2 waste_pct=20"
        print "stat: arena.ir peak_kb=20 blocks=3 waste_pct=10"
        print "stat: intern lookups=1000 hits=900"
        print "stat: pp includes=50 guard_skips=40 tokens=10000"
    }
}' >"$work/logs/build.log"
cat <<EOT
wall_ms_median=100
user_ms_median=70
sys_ms_median=20
maxrss_kb_max=1000
EOT
EOF
cat >"$tmp/once-cgf" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$tmp/once-git" "$tmp/once-timeit" "$tmp/once-cgf"
mkdir -p "$tmp/once-source"
once_work="$tmp/once work with spaces"
FIXTURE_ONCE_WORK="$once_work" CGF_MUSL_BUILD_GIT_CMD="$tmp/once-git" \
CGF_MUSL_BUILD_TIMEIT="$tmp/once-timeit" CGF_MUSL_BUILD_CGF="$tmp/once-cgf" \
    "$once" "$tmp/once-source" "$once_work" "$tmp/once receipt.txt"
grep -F 'route.total=1354' "$tmp/once receipt.txt" >/dev/null ||
    fail 'route aggregation failed in a workspace path containing spaces'

cat >"$tmp/control-helper" <<'EOF'
#!/bin/sh
cat <<EOT
control_protocol=fleet-control-v2
logical_cpus=20
cpu_idle_pct=95.0
load1=0.20
governor=performance
power_profile=performance
scaling_driver=acpi-cpufreq
energy_performance_preference=performance
EOT
EOF
cat >"$tmp/bench-helper" <<'EOF'
#!/bin/sh
mkdir -p "$2/heavy-build-tree"
: >"$2/heavy-build-tree/object.o"
cp "$CGF_FLEET_TEST_RECEIPT" "$3"
EOF
cat >"$tmp/git-helper" <<'EOF'
#!/bin/sh
if [ "${1:-}" = -C ]; then shift 2; fi
case ${1:-} in
status) printf '%s' "${FIXTURE_MUSL_TREE_STATUS:-}" ;;
*) exit 2 ;;
esac
EOF
cat >"$tmp/should-not-run" <<'EOF'
#!/bin/sh
exit 99
EOF
chmod +x "$tmp/control-helper" "$tmp/bench-helper" "$tmp/git-helper" "$tmp/should-not-run"
mkdir -p "$tmp/fleet-root/.git" "$tmp/fleet-root/.benchmarks/runs" "$tmp/fleet-root/build"
CGF_FLEET_MUSL_GIT_CMD=$tmp/git-helper
export CGF_FLEET_MUSL_GIT_CMD
CGF_FLEET_MUSL_ROOT="$tmp/fleet-root" CGF_FLEET_HOST=kasumi \
CGF_FLEET_STAMP=2026-08-13T120000Z CGF_FLEET_MUSL_SOURCE="$tmp/source" \
CGF_FLEET_MUSL_CONTROL="$tmp/control-helper" CGF_FLEET_MUSL_BENCH="$tmp/bench-helper" \
CGF_FLEET_MUSL_GATE="$tmp/should-not-run" CGF_FLEET_MUSL_PERF_GATE="$tmp/should-not-run" \
CGF_FLEET_MUSL_CONFIG="$tmp/base" CGF_FLEET_TEST_RECEIPT="$tmp/pass" \
    "$fleet" >"$tmp/fleet.out"
artifact=$tmp/fleet-root/.benchmarks/runs/2026-08-13T120000Z-kasumi-musl-full-build.txt
grep -F 'fleet.gate=warmup' "$artifact" >/dev/null || fail 'missing-baseline warmup was not recorded'
grep -F 'fleet.baseline_mutated=no' "$artifact" >/dev/null || fail 'baseline immutability was not recorded'
[ ! -e "$tmp/fleet-root/build/fleet-musl-build-2026-08-13T120000Z-kasumi" ] ||
    fail 'successful fleet publication retained its heavy work directory'
expect 3 'refusing to overwrite immutable artifact' env \
    CGF_FLEET_MUSL_ROOT="$tmp/fleet-root" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=2026-08-13T120000Z CGF_FLEET_MUSL_CONTROL="$tmp/control-helper" \
    CGF_FLEET_MUSL_BENCH="$tmp/bench-helper" CGF_FLEET_MUSL_GATE="$tmp/should-not-run" \
    CGF_FLEET_MUSL_PERF_GATE="$tmp/should-not-run" CGF_FLEET_MUSL_CONFIG="$tmp/base" \
    "$fleet"

expect 3 'Cgfried tree must be clean before measurement' env \
    FIXTURE_MUSL_TREE_STATUS='?? local-change' \
    CGF_FLEET_MUSL_ROOT="$tmp/fleet-root" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=2026-08-13T121000Z CGF_FLEET_MUSL_SOURCE="$tmp/source" \
    CGF_FLEET_MUSL_CONTROL="$tmp/control-helper" CGF_FLEET_MUSL_BENCH="$tmp/bench-helper" \
    CGF_FLEET_MUSL_GATE="$tmp/should-not-run" CGF_FLEET_MUSL_PERF_GATE="$tmp/should-not-run" \
    CGF_FLEET_MUSL_CONFIG="$tmp/base" CGF_FLEET_TEST_RECEIPT="$tmp/pass" \
    "$fleet"
[ ! -e "$tmp/fleet-root/build/fleet-musl-build-2026-08-13T121000Z-kasumi" ] ||
    fail 'dirty-tree rejection created a measurement work directory'

cat >"$tmp/bench-fail" <<'EOF'
#!/bin/sh
mkdir -p "$2/heavy-build-tree"
printf '%s\n' "$CGF_FLEET_STAMP" >"$2/heavy-build-tree/failure-stamp"
exit 3
EOF
chmod +x "$tmp/bench-fail"
expect 3 'musl full-build measurement failed' env \
    CGF_FLEET_MUSL_ROOT="$tmp/fleet-root" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=2026-08-13T122000Z CGF_FLEET_MUSL_SOURCE="$tmp/source" \
    CGF_FLEET_MUSL_CONTROL="$tmp/control-helper" CGF_FLEET_MUSL_BENCH="$tmp/bench-fail" \
    CGF_FLEET_MUSL_GATE="$tmp/should-not-run" CGF_FLEET_MUSL_PERF_GATE="$tmp/should-not-run" \
    CGF_FLEET_MUSL_CONFIG="$tmp/base" "$fleet"
failure_dir=$tmp/fleet-root/build/fleet-musl-failure-kasumi
[ "$(cat "$failure_dir/bench/heavy-build-tree/failure-stamp")" = 2026-08-13T122000Z ] ||
    fail 'failed fleet measurement evidence was not retained'
[ ! -e "$tmp/fleet-root/build/fleet-musl-build-2026-08-13T122000Z-kasumi" ] ||
    fail 'failed fleet measurement left an unbounded active work directory'
expect 3 'musl full-build measurement failed' env \
    CGF_FLEET_MUSL_ROOT="$tmp/fleet-root" CGF_FLEET_HOST=kasumi \
    CGF_FLEET_STAMP=2026-08-13T123000Z CGF_FLEET_MUSL_SOURCE="$tmp/source" \
    CGF_FLEET_MUSL_CONTROL="$tmp/control-helper" CGF_FLEET_MUSL_BENCH="$tmp/bench-fail" \
    CGF_FLEET_MUSL_GATE="$tmp/should-not-run" CGF_FLEET_MUSL_PERF_GATE="$tmp/should-not-run" \
    CGF_FLEET_MUSL_CONFIG="$tmp/base" "$fleet"
[ "$(cat "$failure_dir/bench/heavy-build-tree/failure-stamp")" = 2026-08-13T123000Z ] ||
    fail 'new failure did not replace the prior bounded failure bundle'

cat >"$tmp/trial.conf" <<'EOF'
name=musl-full-build
state=trial
where=fleet
when=nightly
threshold=+30%-time-and-+20%-rss
rationale=Fixture
owner_sprint=Sprint-54
EOF
cp "$tmp/base" "$tmp/fleet-root/.benchmarks/baseline-musl-full-build-x86_64-linux-musl.kasumi.txt"
CGF_FLEET_MUSL_ROOT="$tmp/fleet-root" CGF_FLEET_HOST=kasumi \
CGF_FLEET_STAMP=2026-08-13T130000Z CGF_FLEET_MUSL_SOURCE="$tmp/source" \
CGF_FLEET_MUSL_CONTROL="$tmp/control-helper" CGF_FLEET_MUSL_BENCH="$tmp/bench-helper" \
CGF_FLEET_MUSL_GATE="$gate" CGF_FLEET_MUSL_PERF_GATE="$root/scripts/perf_gate.sh" \
CGF_FLEET_MUSL_CONFIG="$tmp/trial.conf" CGF_FLEET_TEST_RECEIPT="$tmp/pass" \
    "$fleet" >"$tmp/trial-pass.out"
grep -F 'fleet.gate=trial-pass' \
    "$tmp/fleet-root/.benchmarks/runs/2026-08-13T130000Z-kasumi-musl-full-build.txt" >/dev/null ||
    fail 'trial pass was not recorded'
CGF_FLEET_MUSL_ROOT="$tmp/fleet-root" CGF_FLEET_HOST=kasumi \
CGF_FLEET_STAMP=2026-08-13T140000Z CGF_FLEET_MUSL_SOURCE="$tmp/source" \
CGF_FLEET_MUSL_CONTROL="$tmp/control-helper" CGF_FLEET_MUSL_BENCH="$tmp/bench-helper" \
CGF_FLEET_MUSL_GATE="$gate" CGF_FLEET_MUSL_PERF_GATE="$root/scripts/perf_gate.sh" \
CGF_FLEET_MUSL_CONFIG="$tmp/trial.conf" CGF_FLEET_TEST_RECEIPT="$tmp/wall" \
    "$fleet" >"$tmp/trial-trip.out"
grep -F 'fleet.gate=trial-trip' \
    "$tmp/fleet-root/.benchmarks/runs/2026-08-13T140000Z-kasumi-musl-full-build.txt" >/dev/null ||
    fail 'trial trip was not recorded'
[ ! -e "$tmp/fleet-root/build/fleet-musl-build-2026-08-13T140000Z-kasumi" ] ||
    fail 'trial publication retained its heavy work directory'

echo 'musl_full_build_test: clean provenance, bounded work, gate boundaries, aggregation, and immutable publication passed'
