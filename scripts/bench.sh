#!/bin/sh
# Sprint 52 compile-speed and memory measurement protocol.
#
# Noise-floor contract:
#   * require the v2 capacity/idle control classification unless
#     CGF_BENCH_FORCE=1 records a provenance-only sample;
#   * record the Linux profile/driver/governor/EPP state and warn when it is
#     not an effective performance configuration;
#   * run lanes in the fixed sqlite3, self, many-tu, musl order;
#   * on nomad-1, cap each batch at 5 minutes and cool down for 2 minutes
#     between corpora because the arm64 laptop thermally throttles;
#   * preserve median/MAD plus raw samples; never gate below observed MAD;
#   * record host/date/revision/tool/corpus provenance in every result.
#
# CGF_STATS numbers diagnose a regression detected by the timer; they are not
# timings themselves.  The compiler is exec'd directly by timeit (no sh -c).
set -eu
LC_ALL=C
export LC_ALL

ROOT=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
BUILD=${BUILD:-$ROOT/build}
CGF=${CGF_BENCH_CGF:-$BUILD/cgfried}
TIMEIT=${CGF_BENCH_TIMEIT:-$BUILD/timeit}
CONTROL=${CGF_BENCH_CONTROL_SCRIPT:-$ROOT/scripts/bench-control.sh}
WORK=${CGF_BENCH_WORK:-$BUILD/bench}
RESULTS=${CGF_BENCH_RESULTS:-$WORK/results.txt}
RUNS=${CGF_BENCH_RUNS:-10}
WARMUP=${CGF_BENCH_WARMUP:-1}
SELF_LIMIT=${CGF_BENCH_SELF_LIMIT:-0}
SQLITE_VERSION=${CGF_SQLITE_VERSION:-3500400}
SQLITE_SHA3=${CGF_SQLITE_SHA3:-9145255e83da6529e70121ee4d7a4c88fe83ca4511da0c9ed13d10842df36782}
SQLITE_CKSUM=${CGF_SQLITE_CKSUM:-2703132855:9282866}
SYSROOT_INCLUDE=${CGF_FLEET_SYSROOT_INCLUDE:-}
SYSROOT_CRT=${CGF_FLEET_SYSROOT_CRT:-}

die()
{
    echo "bench: $*" >&2
    exit 3
}

case $RUNS:$WARMUP in
    *[!0-9:]* | :* | *:) die "runs and warmup must be nonnegative integers" ;;
esac
case $SELF_LIMIT in
    *[!0-9]* | '') die "CGF_BENCH_SELF_LIMIT must be a nonnegative integer" ;;
esac
case ${SYSROOT_INCLUDE:+include}:${SYSROOT_CRT:+crt} in
    : | include:crt) ;;
    *) die "CGF_FLEET_SYSROOT_INCLUDE and CGF_FLEET_SYSROOT_CRT must be set together" ;;
esac
[ "$RUNS" -ge 1 ] || die "CGF_BENCH_RUNS must be >=1"
[ "$SQLITE_VERSION" = 3500400 ] || die "unsupported SQLite corpus pin: $SQLITE_VERSION"
[ "$SQLITE_SHA3" = 9145255e83da6529e70121ee4d7a4c88fe83ca4511da0c9ed13d10842df36782 ] ||
    die "SQLite digest does not match the vendored corpus"
[ "$SQLITE_CKSUM" = 2703132855:9282866 ] ||
    die "SQLite POSIX checksum does not match the vendored corpus pin"
[ -x "$CGF" ] || die "compiler is not executable: $CGF"
[ -x "$TIMEIT" ] || die "timer is not executable: $TIMEIT"
[ -x "$CONTROL" ] || die "control helper is not executable: $CONTROL"
SQLITE=$ROOT/tests/bench/corpus/sqlite3/sqlite3.c
[ -f "$SQLITE" ] || die "missing pinned SQLite amalgamation: $SQLITE"
sqlite_cksum=$(cksum "$SQLITE" | awk '{ print $1 ":" $2 }')
[ "$sqlite_cksum" = "$SQLITE_CKSUM" ] ||
    die "vendored SQLite bytes do not match the pinned corpus"

governor=unavailable
scaling_driver=unavailable
energy_performance_preference=unavailable
if [ -d /sys/devices/system/cpu ]; then
    governors=$(for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
        [ -r "$f" ] && cat "$f"
    done | sort -u | paste -sd, -)
    [ -n "$governors" ] && governor=$governors
    scaling_drivers=$(for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_driver; do
        [ -r "$f" ] && cat "$f"
    done | sort -u | paste -sd, -)
    [ -n "$scaling_drivers" ] && scaling_driver=$scaling_drivers
    energy_preferences=$(for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/energy_performance_preference; do
        [ -r "$f" ] && cat "$f"
    done | sort -u | paste -sd, -)
    [ -n "$energy_preferences" ] &&
        energy_performance_preference=$energy_preferences
fi
power_profile=unavailable
if command -v powerprofilesctl >/dev/null 2>&1; then
    detected_power_profile=$(powerprofilesctl get 2>/dev/null |
        sed -n '1p' || true)
    [ -z "$detected_power_profile" ] || power_profile=$detected_power_profile
fi

host=${CGF_FLEET_HOST:-$(hostname -s 2>/dev/null || uname -n)}
if [ "$host" = nomad-1 ]; then
    power_profile=unavailable
    scaling_driver=unavailable
    energy_performance_preference=unavailable
fi

control_tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-bench-control.XXXXXX") ||
    die "cannot create control provenance workspace"
trap 'rm -rf "$control_tmp"' EXIT HUP INT TERM
control_measure=$control_tmp/measure.txt
control_input=$control_tmp/input.txt
"$CONTROL" measure "$host" >"$control_measure" ||
    die "capacity/idle measurement failed"
{
    echo "host=$host"
    echo "governor=$governor"
    echo "power_profile=$power_profile"
    echo "scaling_driver=$scaling_driver"
    echo "energy_performance_preference=$energy_performance_preference"
    cat "$control_measure"
} >"$control_input"
control_status=0
"$CONTROL" classify --require-v2 "$control_input" >/dev/null ||
    control_status=$?
case $control_status in
0) ;;
1)
    if [ "${CGF_BENCH_FORCE:-0}" = 1 ]; then
        echo "bench: WARNING: capacity/idle controls are provenance-only; forced recording enabled" >&2
    else
        die "capacity/idle controls are provenance-only (set CGF_BENCH_FORCE=1 to record)"
    fi
    ;;
3) die "malformed v2 capacity/idle provenance" ;;
*) die "control classifier failed with status $control_status" ;;
esac
control_value()
{
    awk -F= -v key="$1" '$1 == key { print substr($0, length(key) + 2) }' \
        "$control_measure"
}
control_protocol=$(control_value control_protocol)
logical_cpus=$(control_value logical_cpus)
cpu_idle_pct=$(control_value cpu_idle_pct)
loadavg=$(control_value load1)
host_class=${CGF_BENCH_HOST_CLASS:-}
date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
if [ -n "${CGF_BENCH_REV:-}" ]; then
    rev=$CGF_BENCH_REV
    tree_state=${CGF_BENCH_TREE_STATE:-exported-commit}
elif git -C "$ROOT" rev-parse HEAD >/dev/null 2>&1; then
    rev=$(git -C "$ROOT" rev-parse HEAD)
    if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
        tree_state=dirty
    else
        tree_state=clean
    fi
else
    rev=unknown
    tree_state=unavailable
fi
target=$("$CGF" -dumpmachine 2>/dev/null || echo unknown)
mkdir -p "$WORK/raw" "$WORK/stats" "$(dirname "$RESULTS")"
: >"$RESULTS"
{
    echo "host=$host"
    [ -z "$host_class" ] || echo "host_class=$host_class"
    echo "date=$date_utc"
    echo "governor=$governor"
    echo "power_profile=$power_profile"
    echo "scaling_driver=$scaling_driver"
    echo "energy_performance_preference=$energy_performance_preference"
    echo "control_protocol=$control_protocol"
    echo "logical_cpus=$logical_cpus"
    echo "cpu_idle_pct=$cpu_idle_pct"
    echo "load1=$loadavg"
    echo "cgf_rev=$rev"
    echo "cgf_tree=$tree_state"
    echo "target=$target"
    echo "compiler=$CGF"
    echo "timeit=$TIMEIT"
    echo "runs=$RUNS"
    echo "warmup=$WARMUP"
    echo 'timeit_protocol=sprint-52-compile-median-mad-v1'
    echo "self_limit=$SELF_LIMIT"
    echo "sqlite_version=$SQLITE_VERSION"
    echo "sqlite_sha3=$SQLITE_SHA3"
    echo "sqlite_cksum=$SQLITE_CKSUM"
    [ -z "$SYSROOT_INCLUDE" ] || echo "sysroot_include=$SYSROOT_INCLUDE"
    [ -z "$SYSROOT_CRT" ] || echo "sysroot_crt=$SYSROOT_CRT"
    echo 'lane_order=sqlite3,self,many-tu,musl'
} >>"$RESULTS"

run_lane()
{
    lane=$1
    corpus=$2
    shift 2
    raw=$WORK/raw/$lane.txt
    stderr_file=$WORK/stats/$lane.stderr
    stats=$WORK/stats/$lane.txt
    metrics=$WORK/$lane.metrics
    lane_start=$(date +%s)

    # CGF_STATS is inherited through timeit by the directly-exec'd compiler.
    if [ "$host" = nomad-1 ]; then
        timeit_status=0
        CGF_STATS=1 "$TIMEIT" -t 300 -n "$RUNS" -w "$WARMUP" \
            -o "$raw" -- "$CGF" "$@" >"$metrics" 2>"$stderr_file" ||
            timeit_status=$?
    else
        timeit_status=0
        CGF_STATS=1 "$TIMEIT" -n "$RUNS" -w "$WARMUP" -o "$raw" -- \
            "$CGF" "$@" >"$metrics" 2>"$stderr_file" || timeit_status=$?
    fi
    if [ "$timeit_status" -ne 0 ]; then
        echo "bench: timed lane '$lane' failed; see $stderr_file" >&2
        if [ -s "$stderr_file" ]; then
            sed -n '1,120p' "$stderr_file" >&2
        fi
        return 1
    fi
    if ! grep '^stat: ' "$stderr_file" >"$stats.all"; then
        echo "bench: lane '$lane' produced no CGF_STATS records" >&2
        return 1
    fi
    sort -u "$stats.all" >"$stats"
    if [ "$(wc -l < "$stats")" -ne 4 ] ||
       [ "$(grep -c '^stat: arena\.ast ' "$stats")" -ne 1 ] ||
       [ "$(grep -c '^stat: arena\.ir ' "$stats")" -ne 1 ] ||
       [ "$(grep -c '^stat: intern ' "$stats")" -ne 1 ] ||
       [ "$(grep -c '^stat: pp ' "$stats")" -ne 1 ]; then
        echo "bench: lane '$lane' did not produce one deterministic record per CGF_STATS category" >&2
        return 1
    fi
    {
        echo "$lane.status=measured"
        echo "$lane.corpus=$corpus"
        sed -n "s/^\([a-z][a-z0-9_]*\)=/$lane.\1=/p" "$metrics"
        awk -v lane="$lane" '
            /^stat: / {
                category = $2
                for (i = 3; i <= NF; i++) {
                    equals = index($i, "=")
                    if (equals)
                        print lane ".stat." category "." \
                            substr($i, 1, equals - 1) "=" substr($i, equals + 1)
                }
            }
        ' "$stats"
        echo "$lane.raw_samples=$raw"
    } >>"$RESULTS"
    if [ "$host" = nomad-1 ]; then
        lane_seconds=$(($(date +%s) - lane_start))
        [ "$lane_seconds" -le 300 ] || {
            echo "bench: nomad-1 lane '$lane' exceeded the 5-minute thermal batch limit" >&2
            return 1
        }
    fi
}

set -- -std=gnu17 -DSQLITE_DISABLE_INTRINSIC=1
sqlite_corpus="sqlite-amalgamation-$SQLITE_VERSION"
if [ "$target" = arm64-macos ]; then
    # Current Apple SDK headers use compiler extensions intentionally outside
    # Cgfried v0.1.  This lane measures the pinned SQLite TU in syntax-only
    # mode, so force a narrow compatibility header rather than weakening the
    # corpus, dropping the lane, or pretending those extensions are shipped.
    set -- "$@" -include "$ROOT/tests/bench/compat/arm64-macos-syntax.h"
    sqlite_corpus="$sqlite_corpus:arm64-macos-sdk-syntax-v1"
fi
run_lane sqlite3 "$sqlite_corpus" "$@" -Wno-attributes -Wno-mem \
    -Wno-return-type -fsyntax-only "$SQLITE"

nomad_cooldown()
{
    if [ "$host" = nomad-1 ]; then
        echo 'bench: nomad-1 thermal cooldown (120 seconds)' >&2
        sleep 120
    fi
}

nomad_cooldown

set --
seen=0
SELF_MANIFEST=$WORK/self-sources.txt
find "$ROOT/src" -type f -name '*.c' ! -path "$ROOT/src/rt/*" | sort >"$SELF_MANIFEST"
while IFS= read -r source; do
    if [ "$SELF_LIMIT" -ne 0 ] && [ "$seen" -ge "$SELF_LIMIT" ]; then
        break
    fi
    set -- "$@" "$source"
    seen=$((seen + 1))
done <"$SELF_MANIFEST"
self_corpus="cgfried-src-$rev:$seen-files"
case $target in
arm64-linux)
    # The AArch64 OS headers spell vector-register fields as __uint128_t.
    # Integer-128 is deliberately outside the v0.1 front end, but this lane is
    # syntax-only and never observes those fields. Keep their declaration
    # parseable with an explicit compatibility spelling and record the shim in
    # corpus provenance instead of silently dropping a self source file.
    set -- '-D__uint128_t=unsigned long long' "$@"
    self_corpus="$self_corpus:u128-syntax-shim=ull"
    ;;
arm64-macos)
    # Keep the self corpus complete while insulating its syntax-only parse
    # from Apple SDK declarations that require Clang preprocessor extensions
    # or redeclare the va_list supplied by Cgfried's shipped stdarg.h.  The
    # overlay intervenes only when a source naturally reaches those SDK
    # headers; the forced header itself includes no unrelated SDK surface.
    set -- -include "$ROOT/tests/bench/compat/arm64-macos-self-syntax.h" \
        -I "$ROOT/tests/bench/compat/arm64-macos-self-overlay" "$@"
    self_corpus="$self_corpus:arm64-macos-self-sdk-syntax-v2"
    ;;
esac
run_lane self "$self_corpus" -Wno-mem -Wno-return-type \
    -fsyntax-only -I "$ROOT/src" "$@"
nomad_cooldown

MANY=$WORK/corpus/many-tu
"$ROOT/scripts/gen-tu-corpus.sh" "$MANY" >/dev/null
set --
for source in "$MANY"/tu_*.c; do
    set -- "$@" "$source"
done
run_lane many-tu "cgf-many-tu-v1:${CGF_BENCH_TU_COUNT:-500}x${CGF_BENCH_TU_LINES:-200}" \
    -Wno-mem -Wno-return-type -fsyntax-only "$@"
nomad_cooldown

# The upstream musl clone is local-only reference material today.  Sprint 52
# defines the lane and records its present reach; Sprint 57 activates its gate.
MUSL_REF=${CGF_MUSL_REF:-$ROOT/.docs/refs/musl}
MUSL_LOG=$WORK/musl-build.log
: >"$MUSL_LOG"
if [ "${CGF_BENCH_SKIP_MUSL:-0}" = 1 ]; then
    musl_status=smoke-skipped
    musl_detail=CGF_BENCH_SKIP_MUSL
elif [ ! -x "$MUSL_REF/configure" ]; then
    musl_status=deferred
    musl_detail=reference-clone-unavailable
else
    MUSL_BUILD=$(mktemp -d "$WORK/musl-build.XXXXXX")
    musl_build_status=0
    if [ "$host" = nomad-1 ]; then
        # The measured child expands these deliberately exported variables.
        # shellcheck disable=SC2016
        CGF_MUSL_BUILD=$MUSL_BUILD CGF_MUSL_CC=$CGF \
            CGF_MUSL_CONFIGURE=$MUSL_REF/configure \
            CGF_MUSL_LOG=$MUSL_LOG "$TIMEIT" -t 300 -n 1 -w 0 -- \
            /bin/sh -c '
                cd "$CGF_MUSL_BUILD" &&
                CC="$CGF_MUSL_CC" "$CGF_MUSL_CONFIGURE" --disable-shared \
                    --prefix="$CGF_MUSL_BUILD/install" \
                    >>"$CGF_MUSL_LOG" 2>&1 &&
                make -j1 CC="$CGF_MUSL_CC" >>"$CGF_MUSL_LOG" 2>&1
            ' >"$WORK/musl-time.metrics" 2>>"$MUSL_LOG" || musl_build_status=$?
    else
        (cd "$MUSL_BUILD" && CC="$CGF" "$MUSL_REF/configure" \
            --disable-shared --prefix="$MUSL_BUILD/install" \
            >>"$MUSL_LOG" 2>&1 && make -j1 CC="$CGF" \
            >>"$MUSL_LOG" 2>&1) || musl_build_status=$?
    fi
    if [ "$musl_build_status" -eq 0 ]; then
        musl_status=build-green-ungated
        musl_detail=full-build
    else
        musl_status=deferred
        if grep -q '^timeit: timeout after 300 seconds$' "$MUSL_LOG"; then
            musl_detail=timeout-300s
        else
            musl_detail=$(sed -n '/error:/ { s/[[:space:]][[:space:]]*/ /g; p; q; }' "$MUSL_LOG")
        fi
        [ -n "$musl_detail" ] || musl_detail=build-failed-see-log
    fi
fi
{
    echo "musl.status=$musl_status"
    echo 'musl.gate=deferred-until-sprint-57'
    echo "musl.reach=$musl_detail"
    if [ -d "$MUSL_REF/.git" ]; then
        echo "musl.commit=$(git -C "$MUSL_REF" rev-parse HEAD 2>/dev/null || echo unknown)"
    fi
    echo "musl.log=$MUSL_LOG"
} >>"$RESULTS"

echo "bench: wrote $RESULTS"
