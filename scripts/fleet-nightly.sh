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
host=${CGF_FLEET_HOST:-}

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
"$git_cmd" -C "$checkout" pull --rebase origin trunk ||
    die "cannot update dedicated trunk checkout"
[ -z "$($git_cmd -C "$checkout" status --porcelain --untracked-files=normal)" ] ||
    die "dedicated checkout is dirty after sync"

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

bench=${CGF_FLEET_BENCH:-$checkout/scripts/fleet-bench.sh}
perf=${CGF_FLEET_PERF:-$checkout/scripts/fleet-perf.sh}
[ -x "$bench" ] || die "fleet benchmark wrapper is not executable: $bench"
[ -x "$perf" ] || die "fleet runtime wrapper is not executable: $perf"
run_dir=$checkout/.benchmarks/runs
compile_result=$run_dir/$stamp-$host.txt
runtime_result=$run_dir/$stamp-$host-kernels.txt

bench_status=0
CGF_FLEET_HOST=$host CGF_FLEET_STAMP=$stamp CGF_FLEET_COMMIT=0 \
CGF_FLEET_RESULT=$compile_result "$bench" || bench_status=$?
case $bench_status in
0 | 1) ;;
*) die "compile benchmark infrastructure failed (status $bench_status)" ;;
esac
[ -s "$compile_result" ] || die "compile benchmark artifact is missing"

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
*)
    mv "$compile_holding" "$compile_result" || true
    die "kernel runtime infrastructure failed (status $perf_status)"
    ;;
esac
mv "$compile_holding" "$compile_result" || die "cannot restore compile artifact"
[ -s "$runtime_result" ] || die "kernel runtime artifact is missing"
runtime_trip=$(awk -F= '
    $1 == "fleet.runtime_gate_trip" { value = $2; count++ }
    END {
        if (count != 1 || (value != "yes" && value != "no"))
            exit 3
        print value
    }
' "$runtime_result") || die "kernel runtime artifact lacks a unique trip result"
{
    echo "fleet.nightly_stamp=$stamp"
    echo "fleet.nightly_host=$host"
} >>"$compile_result"
{
    echo "fleet.nightly_stamp=$stamp"
    echo "fleet.nightly_host=$host"
} >>"$runtime_result"

compile_relative=${compile_result#"$checkout"/}
runtime_relative=${runtime_result#"$checkout"/}
"$git_cmd" -C "$checkout" add -- "$compile_relative" "$runtime_relative"
staged=$($git_cmd -C "$checkout" diff --cached --name-only | sort)
expected=$(printf '%s\n%s\n' "$compile_relative" "$runtime_relative" | sort)
[ "$staged" = "$expected" ] || die "refusing commit: staged paths are not exactly the two dated artifacts"

trip=no
if [ "$bench_status" -eq 1 ] || [ "$perf_status" -eq 1 ] ||
   [ "$runtime_trip" = yes ]; then
    trip=yes
fi
"$git_cmd" -C "$checkout" commit -m \
    "Record nightly performance on $host at $stamp (gate-trip=$trip)" -- \
    "$compile_relative" "$runtime_relative" || die "cannot commit nightly artifacts"
echo "$prog: committed $compile_relative and $runtime_relative; baselines unchanged"

if [ "$push" -eq 1 ]; then
    if ! "$git_cmd" -C "$checkout" push origin trunk; then
        echo "$prog: initial push failed; pulling with rebase for one safe retry" >&2
        if ! "$git_cmd" -C "$checkout" pull --rebase origin trunk; then
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
echo "$prog: nightly measurements passed"
