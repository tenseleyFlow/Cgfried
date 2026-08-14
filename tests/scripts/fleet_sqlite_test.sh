#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
tmp=${TMPDIR:-/tmp}/cgf-fleet-sqlite-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
checkout=$tmp/checkout
fake=$tmp/fake
mkdir -p "$checkout/.git" "$checkout/scripts" "$checkout/build" \
    "$checkout/.benchmarks/runs" "$fake"
cp "$root/scripts/fleet-sqlite.sh" "$checkout/scripts/fleet-sqlite.sh"
cp "$root/scripts/fleet-cgf-sysroot.sh" "$checkout/scripts/fleet-cgf-sysroot.sh"
chmod 755 "$checkout/scripts/fleet-sqlite.sh"
chmod 755 "$checkout/scripts/fleet-cgf-sysroot.sh"

fail()
{
    echo "fleet_sqlite_test: $*" >&2
    exit 1
}

cat >"$fake/git" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$FIXTURE_GIT_LOG"
repo=$FIXTURE_CHECKOUT
if [ "${1:-}" = -C ]; then
    repo=$2
    shift 2
fi
case ${1:-} in
status)
    if [ -e "$repo/.fresh-no-checkout" ]; then
        echo 'D  tracked-before-first-checkout'
        exit 0
    fi
    [ "${FIXTURE_GIT_DIRTY:-0}" = 0 ] || echo ' M dirty'
    ;;
fetch) ;;
checkout)
    if [ -e "$repo/.fresh-no-checkout" ]; then
        mv "$repo/.fresh-no-checkout" "$repo/.fresh-checked-out"
    fi
    ;;
clone)
    destination=
    for arg do destination=$arg; done
    mkdir -p "$destination/.git" "$destination/scripts" \
        "$destination/build" "$destination/.benchmarks/runs"
    cp "$FIXTURE_FLEET_SOURCE/fleet-sqlite.sh" \
        "$destination/scripts/fleet-sqlite.sh"
    chmod 755 "$destination/scripts/fleet-sqlite.sh"
    : >"$destination/.fresh-no-checkout"
    ;;
rev-parse)
    if [ "${FIXTURE_GIT_MISMATCH:-0}" = 1 ]; then
        printf '%040d\n' 0
    else
        printf '%s\n' "$CGF_FLEET_REV"
    fi
    ;;
*) exit 2 ;;
esac
EOF
cat >"$fake/make" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$FIXTURE_MAKE_LOG"
mkdir -p "$FIXTURE_CHECKOUT/build"
for output in cgfried timeit; do
    printf '#!/bin/sh\nexit 0\n' >"$FIXTURE_CHECKOUT/build/$output"
    chmod 755 "$FIXTURE_CHECKOUT/build/$output"
done
EOF
cat >"$fake/uname" <<'EOF'
#!/bin/sh
case ${1:-} in
-s) printf '%s\n' "${FIXTURE_SYSTEM:-Linux}" ;;
-m) printf '%s\n' "${FIXTURE_MACHINE:-x86_64}" ;;
*) printf '%s\n' "${FIXTURE_SYSTEM:-Linux}" ;;
esac
EOF
cat >"$fake/date" <<'EOF'
#!/bin/sh
printf '%s\n' 2026-08-13T120000Z
EOF
cat >"$fake/as" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$fake/control" <<'EOF'
#!/bin/sh
set -eu
[ "$#" -eq 2 ]
cat >"$2" <<EOT
host=$1
governor=performance
power_profile=performance
scaling_driver=acpi-cpufreq
energy_performance_preference=performance
control_protocol=fleet-control-v2
logical_cpus=8
cpu_idle_pct=99.00
load1=0.10
EOT
EOF
cat >"$fake/prepare" <<'EOF'
#!/bin/sh
set -eu
if [ "${FIXTURE_BAD_SOURCE:-0}" = 1 ]; then
    echo 'not sqlite' >"$1"
else
    printf '#define SQLITE_VERSION        "3.46.1"\nint sqlite3_fixture;\n' >"$1"
fi
EOF
cat >"$fake/measure" <<'EOF'
#!/bin/sh
set -eu
timeit=$1
runs=$2
warmup=$3
timeout=$4
raw=$5
receipt=$6
log=$7
shift 7
[ "$timeit" = "$FIXTURE_CHECKOUT/build/timeit" ]
[ "$runs:$warmup:$timeout" = 10:1:720 ]
[ "$1" = -- ]
level=unknown
for arg in "$@"; do
    case $arg in -O0) level=O0 ;; -O2) level=O2 ;; esac
done
[ "$level" != unknown ]
printf '%s\n' "$*" >>"$FIXTURE_MEASURE_LOG"
wall=${FIXTURE_WALL_MS:-100}
if [ "$level" = O2 ] && [ -n "${FIXTURE_O2_WALL_MS:-}" ]; then
    wall=$FIXTURE_O2_WALL_MS
fi
: >"$raw"
i=1
while [ "$i" -le "$runs" ]; do
    printf 'sample=%s wall_ms=%s user_ms=80 sys_ms=20 maxrss_kb=100\n' \
        "$i" "$wall" >>"$raw"
    i=$((i + 1))
done
cat >"$receipt" <<EOT
wall_ms_median=$wall
wall_ms_mad=1
user_ms_median=80
sys_ms_median=20
maxrss_kb_max=100
EOT
: >"$log"
EOF
chmod 755 "$fake"/*

awk '
    /^(kasumi|hasu)\.(O0|O2)\.(wall_ms_median|maxrss_kb_max)=/ {
        sub(/=.*/, "=UNMEASURED")
    }
    { print }
' "$root/ci/campaigns/sqlite-baselines.conf" >"$tmp/baselines.conf"
cp "$tmp/baselines.conf" "$tmp/baselines.before"
revision=0123456789abcdef0123456789abcdef01234567

run_sqlite()
{
    run_stamp=$1
    shift
    run_checkout=${CGF_FLEET_TEST_CHECKOUT:-$checkout}
    FIXTURE_CHECKOUT=$run_checkout FIXTURE_FLEET_SOURCE=$root/scripts \
    FIXTURE_GIT_LOG=$tmp/git.log \
    FIXTURE_MAKE_LOG=$tmp/make.log FIXTURE_MEASURE_LOG=$tmp/measure.log \
    PATH=$fake:$PATH CGF_AS_PATH='' \
    CGF_FLEET_HOST=kasumi \
    CGF_FLEET_REV=$revision CGF_FLEET_STAMP=$run_stamp \
    CGF_FLEET_CHECKOUT=$run_checkout CGF_FLEET_GIT_CMD=$fake/git \
    CGF_FLEET_MAKE_CMD=$fake/make CGF_FLEET_UNAME_CMD=$fake/uname \
    CGF_FLEET_DATE_CMD=$fake/date CGF_FLEET_SQLITE_CONTROL=$fake/control \
    CGF_FLEET_TEST_MODE=1 \
    CGF_FLEET_SQLITE_CLASSIFIER=$root/scripts/bench-control.sh \
    CGF_FLEET_SQLITE_PREPARE=$fake/prepare \
    CGF_FLEET_SQLITE_MEASURE=$fake/measure \
    CGF_FLEET_SQLITE_BASELINE_CHECK=$root/scripts/campaigns/sqlite-baseline-check.sh \
    CGF_FLEET_SQLITE_BASELINES=$tmp/baselines.conf \
        "$@" "$root/scripts/fleet-sqlite.sh"
}

: >"$tmp/git.log"
: >"$tmp/make.log"
: >"$tmp/measure.log"
run_sqlite 2026-08-13T120000Z env >"$tmp/pass.out" 2>"$tmp/pass.err"
result=$checkout/.benchmarks/runs/2026-08-13T120000Z-kasumi-sqlite
for file in manifest.txt control.txt baseline-policy.conf baseline-check.log \
    sqlite3.O0.txt sqlite3.O0.raw.txt sqlite3.O2.txt sqlite3.O2.raw.txt; do
    [ -s "$result/$file" ] || fail "successful capture omitted $file"
done
grep -Fq 'fleet.sqlite_commit=0123456789abcdef0123456789abcdef01234567' \
    "$result/manifest.txt" || fail 'manifest omitted exact commit provenance'
grep -Fq "fleet.sqlite_assembler=$fake/as" "$result/manifest.txt" ||
    fail 'manifest omitted the resolved native assembler provenance'
grep -Fq 'fleet.sqlite_control_class=controlled' "$result/manifest.txt" ||
    fail 'manifest omitted controlled classification'
grep -Fq 'fleet.sqlite_compile_profile=sprint-52-sqlite-scale-v1' \
    "$result/manifest.txt" || fail 'manifest omitted the compile profile'
grep -Fq -- '-Wno-attributes -Wno-mem -Wno-return-type' "$tmp/measure.log" ||
    fail 'measurement did not use the Sprint 52 SQLite scale flags'
grep -Fq 'fleet.sqlite_relative_gate=unmeasured' "$result/manifest.txt" ||
    fail 'first capture did not defer only the relative gate'
grep -Fq 'host=kasumi absolute-o2=passed relative-gate=deferred-for-initial-controlled-capture' \
    "$result/baseline-check.log" || fail 'first capture did not enforce the absolute gate'
cmp "$tmp/baselines.before" "$tmp/baselines.conf" >/dev/null ||
    fail 'capture mutated the reviewed baseline policy'
cmp "$tmp/baselines.before" "$result/baseline-policy.conf" >/dev/null ||
    fail 'receipt did not preserve the exact baseline policy'
grep -Fq "fetch --no-tags origin $revision" "$tmp/git.log" ||
    fail 'transaction did not fetch the requested exact commit'
grep -Fq "checkout --detach $revision" "$tmp/git.log" ||
    fail 'transaction did not use a detached exact-commit checkout'
grep -Fq 're-executing the exact-commit fleet runner' "$tmp/pass.out" ||
    fail 'transaction did not re-execute the synchronized runner'

fresh_checkout=$tmp/fresh-checkout
CGF_FLEET_TEST_CHECKOUT=$fresh_checkout \
    run_sqlite 2026-08-13T120100Z env >"$tmp/fresh.out" 2>"$tmp/fresh.err"
fresh_result=$fresh_checkout/.benchmarks/runs/2026-08-13T120100Z-kasumi-sqlite
[ -s "$fresh_result/manifest.txt" ] ||
    fail 'fresh clone did not complete the first exact-commit transaction'
grep -Fq "clone --no-checkout https://github.com/tenseleyFlow/Cgfried.git $fresh_checkout" \
    "$tmp/git.log" || fail 'fresh transaction did not use the dedicated clone'
grep -Fq 're-executing the exact-commit fleet runner' "$tmp/fresh.out" ||
    fail 'fresh clone did not re-execute its checked-out runner'

mkdir -p "$tmp/nix/glibc-dev/include" "$tmp/nix/glibc/lib"
: >"$tmp/nix/glibc/lib/crt1.o"
cp "$tmp/baselines.before" "$tmp/baselines.conf"
run_sqlite 2026-08-13T120500Z env CGF_FLEET_SYNCED=1 CGF_FLEET_HOST=hasu \
    CGF_FLEET_NIX_INCLUDE_DIR=$tmp/nix/glibc-dev/include \
    CGF_FLEET_NIX_CRT_DIR=$tmp/nix/glibc/lib \
    >"$tmp/nix.out" 2>"$tmp/nix.err"
nix_result=$checkout/.benchmarks/runs/2026-08-13T120500Z-hasu-sqlite
nix_sysroot=$checkout/build/fleet-sysroots/glibc-dev--glibc
grep -Fq "fleet.sqlite_sysroot=$nix_sysroot" "$nix_result/manifest.txt" ||
    fail 'NixOS capture omitted the coherent sysroot provenance'
grep -Fq "$checkout/scripts/fleet-cgf-sysroot.sh -std=gnu11 -O0" \
    "$tmp/measure.log" || fail 'NixOS capture did not route measurements through the sysroot wrapper'
[ "$(readlink "$nix_sysroot/usr/include")" = "$tmp/nix/glibc-dev/include" ] ||
    fail 'NixOS include sysroot link does not match the selected toolchain'
[ "$(readlink "$nix_sysroot/usr/lib/x86_64-linux-gnu")" = "$tmp/nix/glibc/lib" ] ||
    fail 'NixOS library sysroot link does not match the selected toolchain'

set +e
run_sqlite 2026-08-13T120000Z env CGF_FLEET_SYNCED=1 \
    >"$tmp/overwrite.out" 2>"$tmp/overwrite.err"
overwrite_status=$?
set -e
[ "$overwrite_status" -eq 3 ] || fail 'overwrite refusal did not return status 3'
grep -Fq 'refusing to overwrite' "$tmp/overwrite.err" ||
    fail 'overwrite refusal diagnostic is missing'

set +e
run_sqlite 2026-08-13T121000Z env CGF_FLEET_SYNCED=1 FIXTURE_O2_WALL_MS=60001 \
    >"$tmp/trip.out" 2>"$tmp/trip.err"
trip_status=$?
set -e
[ "$trip_status" -eq 1 ] || fail 'absolute O2 trip did not return status 1'
trip=$checkout/.benchmarks/runs/2026-08-13T121000Z-kasumi-sqlite
[ -s "$trip/sqlite3.O2.raw.txt" ] || fail 'absolute trip discarded raw evidence'
grep -Fq 'fleet.sqlite_gate=trip' "$trip/manifest.txt" ||
    fail 'absolute trip was not recorded truthfully'
grep -Fq 'exceeds absolute gate' "$trip/baseline-check.log" ||
    fail 'absolute trip receipt omitted the gate diagnostic'

sed 's/UNMEASURED/100/g' "$tmp/baselines.before" >"$tmp/baselines.conf"
run_sqlite 2026-08-13T122000Z env CGF_FLEET_SYNCED=1 \
    >"$tmp/numeric.out" 2>"$tmp/numeric.err"
numeric=$checkout/.benchmarks/runs/2026-08-13T122000Z-kasumi-sqlite
grep -Fq 'fleet.sqlite_relative_gate=numeric' "$numeric/manifest.txt" ||
    fail 'numeric baseline did not activate the relative gate'
grep -Fq 'scope=designated levels=O0,O2' "$numeric/baseline-check.log" ||
    fail 'numeric baseline did not run the designated-host gate'

cp "$tmp/baselines.before" "$tmp/baselines.conf"
sed -i 's/^kasumi\.O0\.wall_ms_median=UNMEASURED$/kasumi.O0.wall_ms_median=100/' \
    "$tmp/baselines.conf"
set +e
run_sqlite 2026-08-13T123000Z env CGF_FLEET_SYNCED=1 \
    >"$tmp/partial.out" 2>"$tmp/partial.err"
partial_status=$?
set -e
[ "$partial_status" -eq 3 ] || fail 'partial baseline did not fail closed'
grep -Fq 'partial numeric baseline' "$tmp/partial.err" ||
    fail 'partial baseline diagnostic is missing'
[ ! -e "$checkout/.benchmarks/runs/2026-08-13T123000Z-kasumi-sqlite" ] ||
    fail 'malformed baseline evidence was published'

cp "$tmp/baselines.before" "$tmp/baselines.conf"
set +e
run_sqlite 2026-08-13T124000Z env CGF_FLEET_SYNCED=1 FIXTURE_BAD_SOURCE=1 \
    >"$tmp/source.out" 2>"$tmp/source.err"
source_status=$?
set -e
[ "$source_status" -eq 3 ] || fail 'wrong SQLite release did not fail closed'
grep -Fq 'not release 3.46.1' "$tmp/source.err" ||
    fail 'wrong SQLite release diagnostic is missing'

set +e
FIXTURE_CHECKOUT=$checkout FIXTURE_GIT_LOG=$tmp/git.log \
FIXTURE_MAKE_LOG=$tmp/make.log CGF_FLEET_HOST=kasumi CGF_FLEET_REV=$revision \
CGF_FLEET_STAMP=2026-08-13T125000Z CGF_FLEET_SYNCED=1 \
CGF_FLEET_CHECKOUT=$checkout CGF_FLEET_GIT_CMD=$fake/git \
CGF_FLEET_MAKE_CMD=$fake/make CGF_FLEET_UNAME_CMD=$fake/uname \
CGF_FLEET_DATE_CMD=$fake/date FIXTURE_GIT_DIRTY=1 \
    "$root/scripts/fleet-sqlite.sh" >"$tmp/dirty.out" 2>"$tmp/dirty.err"
dirty_status=$?
set -e
[ "$dirty_status" -eq 3 ] || fail 'dirty checkout did not fail closed'
grep -Fq 'dirty before sync' "$tmp/dirty.err" || fail 'dirty checkout diagnostic is missing'

set +e
FIXTURE_CHECKOUT=$checkout FIXTURE_GIT_LOG=$tmp/git.log \
FIXTURE_MAKE_LOG=$tmp/make.log CGF_FLEET_HOST=hasu CGF_FLEET_REV=$revision \
CGF_FLEET_STAMP=2026-08-13T130000Z CGF_FLEET_SYNCED=1 \
CGF_FLEET_CHECKOUT=$checkout CGF_FLEET_GIT_CMD=$fake/git \
CGF_FLEET_MAKE_CMD=$fake/make CGF_FLEET_UNAME_CMD=$fake/uname \
CGF_FLEET_DATE_CMD=$fake/date FIXTURE_MACHINE=aarch64 \
    "$root/scripts/fleet-sqlite.sh" >"$tmp/topology.out" 2>"$tmp/topology.err"
topology_status=$?
set -e
[ "$topology_status" -eq 3 ] || fail 'host topology mismatch did not fail closed'
grep -Fq 'topology mismatch' "$tmp/topology.err" ||
    fail 'host topology mismatch diagnostic is missing'

set +e
run_sqlite 2026-08-13T131000Z env CGF_FLEET_SYNCED=1 FIXTURE_GIT_MISMATCH=1 \
    >"$tmp/revision.out" 2>"$tmp/revision.err"
revision_status=$?
set -e
[ "$revision_status" -eq 3 ] || fail 'revision mismatch did not fail closed'
grep -Fq 'exact-commit mismatch' "$tmp/revision.err" ||
    fail 'revision mismatch diagnostic is missing'

for bad_stamp in '../x-08-13T125000Z' '202x-08-13T125000Z'; do
    set +e
    run_sqlite "$bad_stamp" env CGF_FLEET_SYNCED=1 \
        >"$tmp/stamp.out" 2>"$tmp/stamp.err"
    stamp_status=$?
    set -e
    [ "$stamp_status" -eq 3 ] || fail 'malformed timestamp did not fail closed'
    grep -Fq 'malformed UTC stamp' "$tmp/stamp.err" ||
        fail 'malformed timestamp diagnostic is missing'
done

printf 'fleet_sqlite_test: PASS\n'
