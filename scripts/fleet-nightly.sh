#!/bin/sh
# Reproducible per-host Sprint 52+54 nightly measurement transaction.
# A scheduler supplies CGF_FLEET_HOST explicitly; fleet inventory names are
# intentionally independent of local hostnames (notably nomad-1).
set -eu

LC_ALL=C
export LC_ALL

prog=fleet-nightly
git_cmd=${CGF_FLEET_GIT_CMD:-git}
make_cmd=${CGF_FLEET_MAKE_CMD:-make}
uname_cmd=${CGF_FLEET_UNAME_CMD:-uname}
date_cmd=${CGF_FLEET_DATE_CMD:-date}
repo_url=${CGF_FLEET_REPO_URL:-https://github.com/tenseleyFlow/Cgfried.git}
checkout=${CGF_FLEET_CHECKOUT:-${XDG_STATE_HOME:-$HOME/.local/state}/cgfried-fleet/trunk}
push=${CGF_FLEET_PUSH:-0}
synced=${CGF_FLEET_SYNCED:-0}
host=${CGF_FLEET_HOST:-}
os_release=${CGF_FLEET_OS_RELEASE:-/etc/os-release}
nix_include=${CGF_FLEET_NIX_INCLUDE_DIR:-}
nix_crt_dir=${CGF_FLEET_NIX_CRT_DIR:-}
musl_source_override=${CGF_FLEET_MUSL_SOURCE:-}
musl_url=${CGF_FLEET_MUSL_URL:-https://git.musl-libc.org/git/musl}
musl_pin=b306b16af15c89a04d8e0c55cac2dadbeb39c083

die()
{
    echo "$prog: $*" >&2
    exit 3
}

[ -n "$host" ] || die "CGF_FLEET_HOST must explicitly name kasumi, hasu, or nomad-1"
case $push in
0 | 1) ;;
*) die "CGF_FLEET_PUSH must be 0 or 1" ;;
esac
case $synced in
0 | 1) ;;
*) die "CGF_FLEET_SYNCED must be 0 or 1" ;;
esac
case ${nix_include:+include}:${nix_crt_dir:+crt} in
: | include:crt) ;;
*) die "CGF_FLEET_NIX_INCLUDE_DIR and CGF_FLEET_NIX_CRT_DIR must be set together" ;;
esac
for command_path in "$git_cmd" "$make_cmd" "$uname_cmd" "$date_cmd"; do
    command -v "$command_path" >/dev/null 2>&1 || die "required command not found: $command_path"
done

system=$($uname_cmd -s 2>/dev/null || echo unknown)
machine=$($uname_cmd -m 2>/dev/null || echo unknown)
case $host:$system:$machine in
kasumi:Linux:x86_64 | hasu:Linux:x86_64) cc=${CGF_FLEET_CC:-gcc} ;;
nomad-1:Darwin:arm64 | nomad-1:Darwin:aarch64) cc=${CGF_FLEET_CC:-clang} ;;
kasumi:* | hasu:* | nomad-1:*)
    die "$host topology mismatch: got $system $machine"
    ;;
*) die "unsupported fleet host '$host'" ;;
esac
command -v "$cc" >/dev/null 2>&1 || die "C compiler not found: $cc"

stamp=${CGF_FLEET_STAMP:-$($date_cmd -u '+%Y-%m-%dT%H%M%SZ')}
case $stamp in
????-??-??T??????Z) ;;
*) die "malformed UTC stamp '$stamp'" ;;
esac

if [ ! -d "$checkout/.git" ]; then
    [ ! -e "$checkout" ] || die "checkout path exists but is not a Git checkout: $checkout"
    mkdir -p "$(dirname "$checkout")"
    "$git_cmd" clone --branch trunk --single-branch "$repo_url" "$checkout" ||
        die "cannot clone dedicated trunk checkout"
fi
[ -z "$($git_cmd -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
    die "dedicated checkout is dirty before sync: $checkout"
"$git_cmd" -C "$checkout" switch trunk >/dev/null 2>&1 ||
    die "cannot switch dedicated checkout to trunk"
if [ "$synced" -eq 0 ]; then
    "$git_cmd" -C "$checkout" pull --rebase --no-gpg-sign origin trunk ||
        die "cannot update dedicated trunk checkout"
    [ -z "$($git_cmd -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
        die "dedicated checkout is dirty after sync"
    [ -x "$checkout/scripts/fleet-nightly.sh" ] ||
        die "synchronized fleet runner is not executable"
    echo "$prog: re-executing the synchronized fleet runner"
    CGF_FLEET_SYNCED=1 CGF_FLEET_STAMP=$stamp \
        exec "$checkout/scripts/fleet-nightly.sh"
fi

set -- build/cgfried build/timeit
if [ "$system" = Darwin ]; then
    "$git_cmd" -C "$checkout" submodule update --init afs-as afs-ld ||
        die "cannot initialize macOS tool submodules"
    # Cargo writes target/ and may materialize Cargo.lock inside each tool
    # checkout. They are build products in this dedicated clone, not source
    # mutations, and must not poison the clean-tree measurement contract.
    "$git_cmd" -C "$checkout" config submodule.afs-as.ignore untracked
    "$git_cmd" -C "$checkout" config submodule.afs-ld.ignore untracked
    set -- "$@" tools
fi
"$make_cmd" -C "$checkout" "CC=$cc" "$@" || die "portable fleet build failed"
[ -z "$($git_cmd -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
    die "dedicated checkout became dirty during build"

# Cache the immutable musl input below ignored build/ so the scheduled lane is
# self-provisioning without making network access part of every nightly sample.
# An explicit source override supports local mirrors and the transaction
# fixture; the measurement helper independently verifies the exact pin.
musl_source=
if [ "$system" = Linux ]; then
    if [ -n "$musl_source_override" ]; then
        musl_source=$musl_source_override
    else
        musl_source=$checkout/build/fleet-refs/musl-$musl_pin
        if [ ! -d "$musl_source/.git" ]; then
            if [ -e "$musl_source" ] || [ -L "$musl_source" ]; then
                [ "$musl_source" = "$checkout/build/fleet-refs/musl-$musl_pin" ] &&
                    [ -d "$musl_source" ] && [ ! -L "$musl_source" ] ||
                    die "unsafe partial musl cache path: $musl_source"
                # A clone interrupted before .git became usable is generated
                # cache state, not source evidence. Remove only this exact,
                # pin-derived directory so the next scheduled run can recover.
                rm -rf "$musl_source" ||
                    die "cannot remove partial musl cache: $musl_source"
            fi
            mkdir -p "$(dirname "$musl_source")"
            if ! "$git_cmd" clone --no-checkout "$musl_url" "$musl_source"; then
                if [ -d "$musl_source" ] && [ ! -L "$musl_source" ]; then
                    rm -rf "$musl_source" ||
                        die "cannot clean failed musl clone: $musl_source"
                fi
                die "cannot clone the pinned musl input"
            fi
        fi
        if ! "$git_cmd" -C "$musl_source" cat-file -e "$musl_pin^{commit}"; then
            "$git_cmd" -C "$musl_source" fetch --depth=1 origin "$musl_pin" ||
                die "cannot fetch the pinned musl input"
        fi
        "$git_cmd" -C "$musl_source" checkout --detach "$musl_pin" >/dev/null 2>&1 ||
            die "cannot check out the pinned musl input"
        [ "$($git_cmd -C "$musl_source" rev-parse --verify HEAD)" = "$musl_pin" ] ||
            die "musl cache did not resolve to the required pin"
        [ -z "$($git_cmd -C "$musl_source" status --porcelain --untracked-files=normal)" ] ||
            die "pinned musl cache is dirty: $musl_source"
    fi
fi

# NixOS deliberately has no FHS /usr/include or /usr/lib. The compiler must
# still see one coherent target root: construct an ignored symlink sysroot from
# the active GCC wrapper's glibc inputs, then route both measurement lanes
# through a tiny argv-preserving wrapper. Store-hash-specific directories make
# system upgrades additive instead of silently retargeting an old baseline.
if [ "$host:$system" = hasu:Linux ]; then
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
        ) || die "cannot discover the unique NixOS glibc include directory"
        nix_crt=$("$cc" -print-file-name=crt1.o) ||
            die "cannot query the NixOS crt directory"
        case $nix_crt in
        /*/crt1.o) nix_crt_dir=$(dirname "$nix_crt") ;;
        *) die "GCC did not resolve the NixOS crt1.o path: $nix_crt" ;;
        esac
    fi
    if [ -n "$nix_include" ]; then
        [ -d "$nix_include" ] || die "NixOS include directory is missing: $nix_include"
        [ -r "$nix_crt_dir/crt1.o" ] || die "NixOS crt1.o is missing: $nix_crt_dir/crt1.o"
        include_store=$(basename "$(dirname "$nix_include")")
        crt_store=$(basename "$(dirname "$nix_crt_dir")")
        fleet_sysroot=$checkout/build/fleet-sysroots/$include_store--$crt_store
        mkdir -p "$fleet_sysroot/usr/lib"
        if [ ! -e "$fleet_sysroot/usr/include" ]; then
            ln -s "$nix_include" "$fleet_sysroot/usr/include" ||
                die "cannot link the NixOS include directory into the fleet sysroot"
        fi
        if [ ! -e "$fleet_sysroot/usr/lib/x86_64-linux-gnu" ]; then
            ln -s "$nix_crt_dir" "$fleet_sysroot/usr/lib/x86_64-linux-gnu" ||
                die "cannot link the NixOS library directory into the fleet sysroot"
        fi
        [ "$(readlink "$fleet_sysroot/usr/include")" = "$nix_include" ] ||
            die "fleet sysroot include link does not match the active NixOS toolchain"
        [ "$(readlink "$fleet_sysroot/usr/lib/x86_64-linux-gnu")" = "$nix_crt_dir" ] ||
            die "fleet sysroot library link does not match the active NixOS toolchain"
        CGF_FLEET_REAL_CGF=$checkout/build/cgfried
        CGF_FLEET_SYSROOT=$fleet_sysroot
        CGF_FLEET_SYSROOT_INCLUDE=$nix_include
        CGF_FLEET_SYSROOT_CRT=$nix_crt_dir
        CGF_BENCH_CGF=$checkout/scripts/fleet-cgf-sysroot.sh
        CGF_KERNEL_CGF=$checkout/scripts/fleet-cgf-sysroot.sh
        export CGF_FLEET_REAL_CGF CGF_FLEET_SYSROOT
        export CGF_FLEET_SYSROOT_INCLUDE CGF_FLEET_SYSROOT_CRT
        export CGF_BENCH_CGF CGF_KERNEL_CGF
        echo "$prog: using NixOS fleet sysroot $fleet_sysroot"
    fi
fi

bench=${CGF_FLEET_BENCH:-$checkout/scripts/fleet-bench.sh}
perf=${CGF_FLEET_PERF:-$checkout/scripts/fleet-perf.sh}
bootstrap=${CGF_FLEET_BOOTSTRAP:-$checkout/scripts/fleet-bootstrap.sh}
musl=${CGF_FLEET_MUSL:-$checkout/scripts/fleet-musl-build.sh}
[ -x "$bench" ] || die "fleet benchmark wrapper is not executable: $bench"
[ -x "$perf" ] || die "fleet runtime wrapper is not executable: $perf"
if [ "$system" = Linux ]; then
    [ -x "$bootstrap" ] ||
        die "fleet bootstrap wrapper is not executable: $bootstrap"
    [ -x "$musl" ] ||
        die "fleet musl wrapper is not executable: $musl"
fi
run_dir=$checkout/.benchmarks/runs
compile_result=$run_dir/$stamp-$host.txt
runtime_result=$run_dir/$stamp-$host-kernels.txt
bootstrap_result=$run_dir/$stamp-$host-bootstrap.txt
musl_result=$run_dir/$stamp-$host-musl-full-build.txt
compile_holding=
runtime_holding=
musl_holding=
failure_dir=$checkout/build/fleet-failure-$host

preserve_failed_artifacts()
{
    [ "$failure_dir" = "$checkout/build/fleet-failure-$host" ] || return 3
    if [ -e "$failure_dir" ]; then
        [ -d "$failure_dir" ] && [ ! -L "$failure_dir" ] || return 3
        # Retain only the newest generated transaction failure per host so a
        # broken scheduler cannot grow ignored evidence without bound.
        rm -rf "$failure_dir" || return 3
    fi
    mkdir -p "$failure_dir" || return 3
    found=no
    for artifact in "$compile_result" "$runtime_result" "$bootstrap_result" \
        "$musl_result" "$compile_holding" "$runtime_holding" "$musl_holding"; do
        [ -n "$artifact" ] && [ -e "$artifact" ] || continue
        target=$failure_dir/$(basename "$artifact")
        [ ! -e "$target" ] || return 3
        mv "$artifact" "$target" || return 3
        found=yes
    done
    if [ "$found" = yes ]; then
        echo "$prog: retained failed transaction artifacts in $failure_dir" >&2
    fi
}

transaction_die()
{
    message=$*
    preserve_failed_artifacts || die "cannot preserve failed transaction artifacts"
    die "$message"
}

bench_status=0
CGF_FLEET_HOST=$host CGF_FLEET_STAMP=$stamp CGF_FLEET_COMMIT=0 \
CGF_FLEET_RESULT=$compile_result "$bench" || bench_status=$?
case $bench_status in
0 | 1) ;;
*) transaction_die "compile benchmark infrastructure failed (status $bench_status)" ;;
esac
[ -s "$compile_result" ] || transaction_die "compile benchmark artifact is missing"

# Keep the first dated artifact in ignored build/ while the second measurement
# captures revision provenance, so both truth lanes observe the same clean tree.
compile_holding=$checkout/build/$stamp-$host.compile-result
mkdir -p "$checkout/build"
mv "$compile_result" "$compile_holding" || die "cannot hold compile artifact during runtime measurement"

perf_status=0
CGF_FLEET_ROOT=$checkout CGF_FLEET_HOST=$host CGF_FLEET_STAMP=$stamp \
CGF_FLEET_RUNTIME_RESULT=$runtime_result "$perf" || perf_status=$?
case $perf_status in
0 | 1) ;;
*) transaction_die "kernel runtime infrastructure failed (status $perf_status)" ;;
esac
mv "$compile_holding" "$compile_result" || die "cannot restore compile artifact"
[ -s "$runtime_result" ] || transaction_die "kernel runtime artifact is missing"
runtime_trip=$(awk -F= '
    $1 == "fleet.runtime_gate_trip" { value = $2; count++ }
    END {
        if (count != 1 || (value != "yes" && value != "no"))
            exit 3
        print value
    }
' "$runtime_result") || transaction_die "kernel runtime artifact lacks a unique trip result"

musl_status=0
musl_trip=no
if [ "$system" = Linux ]; then
    # The musl receipt records clean-tree provenance. Keep the two earlier
    # artifacts under ignored build/ until this independent lane is complete.
    runtime_holding=$checkout/build/$stamp-$host.runtime-result
    mv "$compile_result" "$compile_holding" ||
        transaction_die "cannot hold compile artifact during musl measurement"
    mv "$runtime_result" "$runtime_holding" || {
        transaction_die "cannot hold runtime artifact during musl measurement"
    }
    restore_musl_inputs()
    {
        [ ! -e "$compile_result" ] && [ -e "$compile_holding" ] &&
            mv "$compile_holding" "$compile_result" || true
        [ ! -e "$runtime_result" ] && [ -e "$runtime_holding" ] &&
            mv "$runtime_holding" "$runtime_result" || true
    }
    trap 'preserve_failed_artifacts' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    CGF_FLEET_MUSL_ROOT=$checkout CGF_FLEET_MUSL_SOURCE=$musl_source \
    CGF_FLEET_HOST=$host CGF_FLEET_STAMP=$stamp \
    CGF_FLEET_GIT_CMD=$git_cmd CGF_FLEET_UNAME_CMD=$uname_cmd \
        "$musl" || musl_status=$?
    trap - EXIT HUP INT TERM
    [ "$musl_status" -eq 0 ] ||
        transaction_die "musl full-build infrastructure failed (status $musl_status)"
    [ -s "$musl_result" ] || transaction_die "musl full-build artifact is missing"
    restore_musl_inputs
    musl_gate=$(awk -F= '
        $1 == "fleet.gate" { value = $2; count++ }
        END {
            if (count != 1 ||
                (value != "warmup" && value != "trial-pass" &&
                 value != "trial-trip"))
                exit 3
            print value
        }
    ' "$musl_result") || transaction_die "musl full-build artifact lacks a unique gate result"
    [ "$musl_gate" != trial-trip ] || musl_trip=yes
fi

bootstrap_status=0
if [ "$system" = Linux ]; then
    # Bootstrap is long enough to perturb the shorter compile/runtime truth
    # lanes, so it runs last. Hold all already-produced artifacts under the
    # ignored build tree to preserve the clean-source timing contract.
    musl_holding=$checkout/build/$stamp-$host.musl-result
    mv "$compile_result" "$compile_holding" ||
        transaction_die "cannot hold compile artifact during bootstrap measurement"
    mv "$runtime_result" "$runtime_holding" || {
        transaction_die "cannot hold runtime artifact during bootstrap measurement"
    }
    mv "$musl_result" "$musl_holding" || {
        transaction_die "cannot hold musl artifact during bootstrap measurement"
    }
    restore_bootstrap_inputs()
    {
        [ ! -e "$compile_result" ] && [ -e "$compile_holding" ] &&
            mv "$compile_holding" "$compile_result" || true
        [ ! -e "$runtime_result" ] && [ -e "$runtime_holding" ] &&
            mv "$runtime_holding" "$runtime_result" || true
        [ ! -e "$musl_result" ] && [ -e "$musl_holding" ] &&
            mv "$musl_holding" "$musl_result" || true
    }
    trap 'preserve_failed_artifacts' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    HOSTCC=$cc CGF_FLEET_ROOT=$checkout CGF_FLEET_HOST=$host \
    CGF_FLEET_STAMP=$stamp CGF_FLEET_BOOTSTRAP_RESULT=$bootstrap_result \
    CGF_FLEET_MAKE_CMD=$make_cmd CGF_FLEET_GIT_CMD=$git_cmd \
    CGF_FLEET_UNAME_CMD=$uname_cmd "$bootstrap" || bootstrap_status=$?
    trap - EXIT HUP INT TERM
    case $bootstrap_status in
    0 | 1) ;;
    *) transaction_die "stage1 bootstrap timing infrastructure failed (status $bootstrap_status)" ;;
    esac
    [ -s "$bootstrap_result" ] ||
        transaction_die "stage1 bootstrap timing artifact is missing"
    restore_bootstrap_inputs
fi
{
    echo "fleet.nightly_stamp=$stamp"
    echo "fleet.nightly_host=$host"
} >>"$compile_result"
{
    echo "fleet.nightly_stamp=$stamp"
    echo "fleet.nightly_host=$host"
} >>"$runtime_result"
if [ "$system" = Linux ]; then
    {
        echo "fleet.nightly_stamp=$stamp"
        echo "fleet.nightly_host=$host"
    } >>"$musl_result"
fi

compile_relative=${compile_result#"$checkout"/}
runtime_relative=${runtime_result#"$checkout"/}
set -- "$compile_relative" "$runtime_relative"
if [ "$system" = Linux ]; then
    bootstrap_relative=${bootstrap_result#"$checkout"/}
    musl_relative=${musl_result#"$checkout"/}
    set -- "$@" "$bootstrap_relative" "$musl_relative"
fi
"$git_cmd" -C "$checkout" add -- "$@"
staged=$($git_cmd -C "$checkout" diff --cached --name-only | sort)
expected=$(printf '%s\n' "$@" | sort)
[ "$staged" = "$expected" ] ||
    die "refusing commit: staged paths are not exactly the dated artifacts"

trip=no
if [ "$bench_status" -eq 1 ] || [ "$perf_status" -eq 1 ] ||
   [ "$runtime_trip" = yes ] || [ "$bootstrap_status" -eq 1 ] ||
   [ "$musl_trip" = yes ]; then
    trip=yes
fi
"$git_cmd" -C "$checkout" -c commit.gpgsign=false commit -m \
    "Record nightly performance on $host at $stamp (gate-trip=$trip)" -- \
    "$@" || die "cannot commit nightly artifacts"
echo "$prog: committed the dated $host artifacts; baselines unchanged"

if [ "$push" -eq 1 ]; then
    if ! "$git_cmd" -C "$checkout" push origin trunk; then
        echo "$prog: initial push failed; pulling with rebase for one safe retry" >&2
        if ! "$git_cmd" -C "$checkout" pull --rebase --no-gpg-sign origin trunk; then
            "$git_cmd" -C "$checkout" rebase --abort >/dev/null 2>&1 || true
            die "push retry rebase failed"
        fi
        "$git_cmd" -C "$checkout" push origin trunk || die "push failed after one rebase retry"
    fi
    echo "$prog: pushed trunk"
else
    echo "$prog: push disabled (set CGF_FLEET_PUSH=1 to publish)"
fi

if [ "$bench_status" -eq 1 ] || [ "$perf_status" -eq 1 ]; then
    echo "$prog: artifacts preserved; one or more performance gates tripped" >&2
    exit 1
fi
if [ "$runtime_trip" = yes ]; then
    echo "$prog: non-blocking trial runtime trip recorded"
fi
if [ "$bootstrap_status" -eq 1 ]; then
    echo "$prog: non-blocking trial bootstrap-time trip recorded"
fi
if [ "$musl_trip" = yes ]; then
    echo "$prog: non-blocking trial musl full-build trip recorded"
fi
echo "$prog: nightly measurements passed"
