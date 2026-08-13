#!/bin/sh
# Capture a controlled, exact-commit SQLite amalgamation compile baseline.
# This transaction publishes dated evidence only.  The reviewed baseline
# policy is copied into the receipt and is never edited or accepted here.
set -eu

LC_ALL=C
export LC_ALL

prog=fleet-sqlite
git_cmd=${CGF_FLEET_GIT_CMD:-git}
make_cmd=${CGF_FLEET_MAKE_CMD:-make}
uname_cmd=${CGF_FLEET_UNAME_CMD:-uname}
date_cmd=${CGF_FLEET_DATE_CMD:-date}
repo_url=${CGF_FLEET_REPO_URL:-https://github.com/tenseleyFlow/Cgfried.git}
checkout=${CGF_FLEET_CHECKOUT:-${XDG_STATE_HOME:-$HOME/.local/state}/cgfried-fleet/sqlite}
host=${CGF_FLEET_HOST:-}
revision=${CGF_FLEET_REV:-}
synced=${CGF_FLEET_SYNCED:-0}
os_release=${CGF_FLEET_OS_RELEASE:-/etc/os-release}
nix_include=${CGF_FLEET_NIX_INCLUDE_DIR:-}
nix_crt_dir=${CGF_FLEET_NIX_CRT_DIR:-}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

sha256()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        die 'sha256sum or shasum is required'
    fi
}

config_value()
{
    key=$1
    file=$2
    awk -F= -v key="$key" '
        $1 == key { count++; value = substr($0, length(key) + 2) }
        END { if (count != 1 || value == "") exit 1; print value }
    ' "$file" || die "baseline policy has no unique $key"
}

case $host in
kasumi | hasu) ;;
*) die 'CGF_FLEET_HOST must explicitly name kasumi or hasu' ;;
esac
case $revision in
????????????????????????????????????????) ;;
*) die 'CGF_FLEET_REV must be an exact 40-character commit id' ;;
esac
case $revision in
*[!0-9a-f]*) die 'CGF_FLEET_REV must be a lowercase hexadecimal commit id' ;;
esac
case $synced in
0 | 1) ;;
*) die 'CGF_FLEET_SYNCED must be 0 or 1' ;;
esac
case ${nix_include:+include}:${nix_crt_dir:+crt} in
: | include:crt) ;;
*) die 'CGF_FLEET_NIX_INCLUDE_DIR and CGF_FLEET_NIX_CRT_DIR must be set together' ;;
esac
for command_path in "$git_cmd" "$make_cmd" "$uname_cmd" "$date_cmd"; do
    command -v "$command_path" >/dev/null 2>&1 ||
        die "required command not found: $command_path"
done

system=$($uname_cmd -s 2>/dev/null || echo unknown)
machine=$($uname_cmd -m 2>/dev/null || echo unknown)
[ "$system:$machine" = Linux:x86_64 ] ||
    die "$host topology mismatch: got $system $machine"

stamp=${CGF_FLEET_STAMP:-$($date_cmd -u '+%Y-%m-%dT%H%M%SZ')}
case $stamp in
[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9][0-9][0-9][0-9][0-9]Z) ;;
*) die "malformed UTC stamp '$stamp'" ;;
esac

if [ ! -d "$checkout/.git" ]; then
    [ ! -e "$checkout" ] ||
        die "checkout path exists but is not a Git checkout: $checkout"
    mkdir -p "$(dirname "$checkout")"
    "$git_cmd" clone --no-checkout "$repo_url" "$checkout" ||
        die 'cannot clone dedicated SQLite checkout'
fi
[ -z "$("$git_cmd" -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
    die "dedicated checkout is dirty before sync: $checkout"
if [ "$synced" -eq 0 ]; then
    "$git_cmd" -C "$checkout" fetch --no-tags origin "$revision" ||
        die "cannot fetch exact commit $revision"
    "$git_cmd" -C "$checkout" checkout --detach "$revision" ||
        die "cannot check out exact commit $revision"
fi
actual_revision=$("$git_cmd" -C "$checkout" rev-parse HEAD) ||
    die 'cannot resolve the dedicated checkout revision'
[ "$actual_revision" = "$revision" ] ||
    die "exact-commit mismatch: requested $revision got $actual_revision"
[ -z "$("$git_cmd" -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
    die 'dedicated checkout is dirty after exact-commit sync'
if [ "$synced" -eq 0 ]; then
    [ -x "$checkout/scripts/fleet-sqlite.sh" ] ||
        die 'synchronized SQLite fleet runner is not executable'
    echo "$prog: re-executing the exact-commit fleet runner"
    CGF_FLEET_SYNCED=1 CGF_FLEET_STAMP=$stamp CGF_FLEET_REV=$revision \
        exec "$checkout/scripts/fleet-sqlite.sh"
fi

"$make_cmd" -C "$checkout" "CC=${CGF_FLEET_CC:-gcc}" \
    build/cgfried build/timeit || die 'portable SQLite measurement build failed'
[ -z "$("$git_cmd" -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
    die 'dedicated checkout became dirty during build'

run_dir=${CGF_FLEET_RUN_DIR:-$checkout/.benchmarks/runs}
result=$run_dir/$stamp-$host-sqlite
case $run_dir in
"$checkout"/.benchmarks/runs) ;;
*) die 'CGF_FLEET_RUN_DIR must be the checkout .benchmarks/runs directory' ;;
esac
[ ! -e "$result" ] || die "refusing to overwrite $result"

work=${CGF_FLEET_SQLITE_WORK:-$checkout/build/fleet-sqlite-$stamp-$host}
case $work in
"$checkout"/build/fleet-sqlite-????-??-??T??????Z-"$host") ;;
*) die 'SQLite measurement work directory has an unsafe path' ;;
esac
[ ! -e "$work" ] || die "refusing to reuse SQLite measurement work: $work"
mkdir -p "$work/receipts" "$work/logs" "$work/source"
trap 'rm -rf "$work/publish"' EXIT HUP INT TERM

control=${CGF_FLEET_SQLITE_CONTROL:-$checkout/scripts/bootstrap-control.sh}
classifier=${CGF_FLEET_SQLITE_CLASSIFIER:-$checkout/scripts/bench-control.sh}
measure=${CGF_FLEET_SQLITE_MEASURE:-$checkout/scripts/campaigns/sqlite-measure.sh}
baseline_check=${CGF_FLEET_SQLITE_BASELINE_CHECK:-$checkout/scripts/campaigns/sqlite-baseline-check.sh}
baseline_config=${CGF_FLEET_SQLITE_BASELINES:-$checkout/ci/campaigns/sqlite-baselines.conf}
prepare=${CGF_FLEET_SQLITE_PREPARE:-}
for helper in "$control" "$classifier" "$measure" "$baseline_check"; do
    [ -x "$helper" ] || die "required helper is not executable: $helper"
done
[ -r "$baseline_config" ] || die "baseline policy is not readable: $baseline_config"

source=$work/source/sqlite3.c
archive_sha=unknown
if [ -n "$prepare" ]; then
    [ "${CGF_FLEET_TEST_MODE:-0}" = 1 ] ||
        die 'CGF_FLEET_SQLITE_PREPARE is a test-only seam'
    [ -x "$prepare" ] || die "SQLite source preparer is not executable: $prepare"
    "$prepare" "$source" || die 'SQLite source preparation failed'
else
    campaign=$checkout/scripts/campaigns/sqlite.sh
    archive_check=$checkout/scripts/campaigns/sqlite-archive-check.sh
    cache=${CGF_FLEET_SQLITE_CACHE:-$checkout/build/campaigns/dl}
    archive=$cache/sqlite-amalgamation-3460100.zip
    [ -x "$campaign" ] || die 'SQLite campaign runner is not executable'
    [ -x "$archive_check" ] || die 'SQLite archive checker is not executable'
    CGF_CAMPAIGN_SQLITE_CACHE=$cache "$campaign" fetch ||
        die 'cannot populate the pinned SQLite campaign cache'
    [ "$(sha256 "$archive")" = 77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784 ] ||
        die 'pinned SQLite amalgamation archive hash mismatch'
    "$archive_check" "$archive" sqlite-amalgamation-3460100 \
        >"$work/logs/archive-check.log" || die 'SQLite archive structure is unsafe'
    command -v unzip >/dev/null 2>&1 || die 'unzip is required'
    unzip -p "$archive" sqlite-amalgamation-3460100/sqlite3.c >"$source" ||
        die 'cannot extract the pinned SQLite amalgamation'
    archive_sha=$(sha256 "$archive")
fi
[ -s "$source" ] || die 'SQLite source preparer produced no source'
grep -Fq '#define SQLITE_VERSION        "3.46.1"' "$source" ||
    die 'prepared SQLite source is not release 3.46.1'

cgf=${CGF_FLEET_SQLITE_CGF:-$checkout/build/cgfried}
timeit=${CGF_FLEET_SQLITE_TIMEIT:-$checkout/build/timeit}
runs=${CGF_FLEET_SQLITE_RUNS:-10}
warmup=${CGF_FLEET_SQLITE_WARMUP:-1}
timeout_seconds=${CGF_FLEET_SQLITE_TIMEOUT:-300}
[ -x "$cgf" ] || die "compiler is not executable: $cgf"
[ -x "$timeit" ] || die "timer is not executable: $timeit"

# Hasu is NixOS and deliberately has no FHS /usr/include. Reuse the fleet
# deployment boundary established by fleet-nightly: discover the active GCC
# wrapper's glibc inputs, build an ignored coherent sysroot, and inject it
# through the argv-preserving wrapper. This is host deployment provenance,
# not a compiler default or a relaxed measurement lane.
cc=${CGF_FLEET_CC:-gcc}
if [ -z "$nix_include" ] && [ -r "$os_release" ] &&
   grep -Eq '^ID=("?nixos"?)$' "$os_release"; then
    nix_include=$(
        "$cc" -E -v -xc /dev/null -o /dev/null 2>&1 |
            awk '$1 ~ /^\/nix\/store\/[^/]+-glibc-[^/]+-dev\/include$/ {
                     found[$1] = 1
                 }
                 END {
                     for (path in found) { count++; result = path }
                     if (count != 1) exit 3
                     print result
                 }'
    ) || die 'cannot discover the unique NixOS glibc include directory'
    nix_crt=$("$cc" -print-file-name=crt1.o) ||
        die 'cannot query the NixOS crt directory'
    case $nix_crt in
    /*/crt1.o) nix_crt_dir=$(dirname "$nix_crt") ;;
    *) die "GCC did not resolve the NixOS crt1.o path: $nix_crt" ;;
    esac
fi
fleet_sysroot=none
if [ -n "$nix_include" ]; then
    [ -d "$nix_include" ] || die "NixOS include directory is missing: $nix_include"
    [ -r "$nix_crt_dir/crt1.o" ] || die "NixOS crt1.o is missing: $nix_crt_dir/crt1.o"
    include_store=$(basename "$(dirname "$nix_include")")
    crt_store=$(basename "$(dirname "$nix_crt_dir")")
    fleet_sysroot=$checkout/build/fleet-sysroots/$include_store--$crt_store
    mkdir -p "$fleet_sysroot/usr/lib"
    if [ ! -e "$fleet_sysroot/usr/include" ]; then
        ln -s "$nix_include" "$fleet_sysroot/usr/include" ||
            die 'cannot link the NixOS include directory into the fleet sysroot'
    fi
    if [ ! -e "$fleet_sysroot/usr/lib/x86_64-linux-gnu" ]; then
        ln -s "$nix_crt_dir" "$fleet_sysroot/usr/lib/x86_64-linux-gnu" ||
            die 'cannot link the NixOS library directory into the fleet sysroot'
    fi
    [ "$(readlink "$fleet_sysroot/usr/include")" = "$nix_include" ] ||
        die 'fleet sysroot include link does not match the active NixOS toolchain'
    [ "$(readlink "$fleet_sysroot/usr/lib/x86_64-linux-gnu")" = "$nix_crt_dir" ] ||
        die 'fleet sysroot library link does not match the active NixOS toolchain'
    CGF_FLEET_REAL_CGF=$cgf
    CGF_FLEET_SYSROOT=$fleet_sysroot
    export CGF_FLEET_REAL_CGF CGF_FLEET_SYSROOT
    cgf=$checkout/scripts/fleet-cgf-sysroot.sh
    [ -x "$cgf" ] || die 'fleet sysroot compiler wrapper is not executable'
fi
case $runs:$warmup:$timeout_seconds in
*[!0-9:]* | :* | *:) die 'runs, warmup, and timeout must be integers' ;;
esac
[ "$runs" -ge 1 ] && [ "$timeout_seconds" -ge 1 ] ||
    die 'runs and timeout must be positive'

# Capture the load and power state only after all setup work is complete, so
# the fleet-control-v2 receipt describes the measurement pair that follows.
control_receipt=$work/receipts/control.txt
"$control" "$host" "$control_receipt" || die 'controlled-host capture failed'
control_status=0
control_class=$($classifier classify --require-v2 "$control_receipt") ||
    control_status=$?
[ "$control_status:$control_class" = 0:controlled ] ||
    die "fleet-control-v2 classification is not controlled (status $control_status)"

for level in O0 O2; do
    raw=$work/receipts/sqlite3.$level.raw.txt
    receipt=$work/receipts/sqlite3.$level.txt
    log=$work/logs/sqlite3.$level.log
    # Use the same reviewed Sprint 52 scale profile as the campaign runner;
    # warning analysis is measured and gated separately from emitted code.
    "$measure" "$timeit" "$runs" "$warmup" "$timeout_seconds" \
        "$raw" "$receipt" "$log" -- \
        "$cgf" -std=gnu11 "-$level" -Wno-attributes -Wno-mem \
        -Wno-return-type -DSQLITE_THREADSAFE=0 \
        -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_HAVE_READLINE=0 \
        -DSQLITE_DISABLE_INTRINSIC -c "$source" \
        -o "$work/sqlite3.$level.o" || die "$level measurement failed"
done

policy_before=$(sha256 "$baseline_config")
[ "$(config_value version "$baseline_config")" = 1 ] ||
    die 'unsupported SQLite baseline policy version'
[ "$(config_value sqlite_release "$baseline_config")" = 3.46.1 ] ||
    die 'SQLite baseline policy release does not match the pinned source'
[ "$(config_value designated_hosts "$baseline_config")" = kasumi,hasu ] ||
    die 'SQLite baseline policy designated_hosts must be kasumi,hasu'
for key in wall_regression_pct rss_regression_pct o2_absolute_wall_ms; do
    value=$(config_value "$key" "$baseline_config")
    case $value in '' | *[!0-9]*) die "baseline policy $key must be an integer" ;; esac
done

baseline_mode=
for fleet_host in kasumi hasu; do
    have_numeric=0
    have_unmeasured=0
    for level in O0 O2; do
        for metric in wall_ms_median maxrss_kb_max; do
            value=$(config_value "$fleet_host.$level.$metric" "$baseline_config")
            case $value in
            UNMEASURED) have_unmeasured=1 ;;
            *)
                printf '%s\n' "$value" | awk '
                    /^[0-9]+([.][0-9]+)?$/ { ok = 1 }
                    END { exit !ok }
                ' || die "malformed baseline $fleet_host.$level.$metric=$value"
                have_numeric=1
                ;;
            esac
        done
    done
    [ "$have_numeric:$have_unmeasured" != 1:1 ] ||
        die "$fleet_host has a partial numeric baseline"
    if [ "$have_numeric" -eq 1 ]; then
        host_mode=numeric
    else
        host_mode=unmeasured
    fi
    [ "$fleet_host" != "$host" ] || baseline_mode=$host_mode
done
[ -n "$baseline_mode" ] || die 'could not classify the host baseline state'

gate_log=$work/logs/baseline-check.log
gate_status=0
if [ "$baseline_mode" = numeric ]; then
    "$baseline_check" "$baseline_config" "$host" \
        "$work/receipts/sqlite3.O0.txt" "$work/receipts/sqlite3.O2.txt" \
        >"$gate_log" 2>&1 || gate_status=$?
else
    "$baseline_check" --absolute-only "$baseline_config" "$host" \
        "$work/receipts/sqlite3.O0.txt" "$work/receipts/sqlite3.O2.txt" \
        >"$gate_log" 2>&1 || gate_status=$?
fi
case $gate_status in
0) gate_result=pass ;;
1)
    if grep -Eq 'exceeds absolute gate| regressed:' "$gate_log"; then
        gate_result=trip
    else
        sed 's/^/fleet-sqlite: baseline checker: /' "$gate_log" >&2
        die 'baseline checker rejected malformed evidence'
    fi
    ;;
*) die "baseline checker infrastructure failed (status $gate_status)" ;;
esac
[ "$(sha256 "$baseline_config")" = "$policy_before" ] ||
    die 'SQLite baseline policy changed during measurement'
[ -z "$("$git_cmd" -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
    die 'dedicated checkout became dirty before evidence publication'

mkdir -p "$run_dir"
publish=$work/publish
mkdir "$publish"
cp "$control_receipt" "$publish/control.txt"
cp "$baseline_config" "$publish/baseline-policy.conf"
cp "$work/receipts/sqlite3.O0.txt" "$publish/sqlite3.O0.txt"
cp "$work/receipts/sqlite3.O0.raw.txt" "$publish/sqlite3.O0.raw.txt"
cp "$work/receipts/sqlite3.O2.txt" "$publish/sqlite3.O2.txt"
cp "$work/receipts/sqlite3.O2.raw.txt" "$publish/sqlite3.O2.raw.txt"
cp "$gate_log" "$publish/baseline-check.log"
cp "$work/logs/sqlite3.O0.log" "$publish/sqlite3.O0.log"
cp "$work/logs/sqlite3.O2.log" "$publish/sqlite3.O2.log"
source_sha=$(sha256 "$source")
{
    echo 'fleet.sqlite_protocol=sprint-59-sqlite-baseline-v1'
    echo 'fleet.sqlite_compile_profile=sprint-52-sqlite-scale-v1'
    echo 'fleet.sqlite_compile_flags=-Wno-attributes,-Wno-mem,-Wno-return-type'
    echo "fleet.sqlite_stamp=$stamp"
    echo "fleet.sqlite_host=$host"
    echo "fleet.sqlite_system=$system"
    echo "fleet.sqlite_machine=$machine"
    echo "fleet.sqlite_commit=$revision"
    echo 'fleet.sqlite_release=3.46.1'
    echo "fleet.sqlite_archive_sha256=$archive_sha"
    echo "fleet.sqlite_source_sha256=$source_sha"
    echo 'fleet.sqlite_control_protocol=fleet-control-v2'
    echo "fleet.sqlite_control_class=$control_class"
    echo "fleet.sqlite_runs=$runs"
    echo "fleet.sqlite_warmup=$warmup"
    echo "fleet.sqlite_timeout_seconds=$timeout_seconds"
    echo "fleet.sqlite_sysroot=$fleet_sysroot"
    echo "fleet.sqlite_baseline_policy_sha256=$policy_before"
    echo 'fleet.sqlite_baseline_mutated=no'
    echo "fleet.sqlite_relative_gate=$baseline_mode"
    echo "fleet.sqlite_gate=$gate_result"
} >"$publish/manifest.txt"
mv "$publish" "$result" || die 'cannot publish immutable SQLite evidence'
trap - EXIT HUP INT TERM

echo "$prog: wrote $result (gate=$gate_result; relative-baseline=$baseline_mode; policy unchanged)"
[ "$gate_status" -eq 0 ] || exit 1
