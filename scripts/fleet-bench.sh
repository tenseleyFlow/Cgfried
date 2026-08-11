#!/bin/sh
# Fleet measurement wrapper. It gates against the committed host-class
# baseline, then commits only the dated run artifact. Baselines are never
# mutated here: accepting a new baseline remains a separate reviewed commit.
set -eu
LC_ALL=C
export LC_ALL

ROOT=${CGF_FLEET_ROOT:-$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)}
git_cmd=${CGF_FLEET_GIT_CMD:-git}
bench_script=${CGF_FLEET_BENCH_SCRIPT:-$ROOT/scripts/bench.sh}
gate_script=${CGF_FLEET_BENCHMARK_GATE:-$ROOT/scripts/benchmark_gate.sh}
host=${CGF_FLEET_HOST:-$(hostname -s 2>/dev/null || uname -n)}
stamp=${CGF_FLEET_STAMP:-$(date -u '+%Y-%m-%dT%H%M%SZ')}
run_dir=${CGF_FLEET_RUN_DIR:-$ROOT/.benchmarks/runs}
result=${CGF_FLEET_RESULT:-$run_dir/$stamp-$host.txt}
commit_result=${CGF_FLEET_COMMIT:-1}

case $host in
    kasumi | hasu | nomad-1) ;;
    *) echo "fleet-bench: unsupported fleet host '$host'" >&2; exit 3 ;;
esac
case $commit_result in
    0 | 1) ;;
    *) echo 'fleet-bench: CGF_FLEET_COMMIT must be 0 or 1' >&2; exit 3 ;;
esac
case $result in
    "$ROOT"/.benchmarks/runs/*) ;;
    *) echo 'fleet-bench: result must be inside .benchmarks/runs' >&2; exit 3 ;;
esac
[ -d "$ROOT/.git" ] || { echo 'fleet-bench: a git checkout is required' >&2; exit 3; }
command -v "$git_cmd" >/dev/null 2>&1 || { echo "fleet-bench: git command not found: $git_cmd" >&2; exit 3; }
[ -x "$bench_script" ] || { echo "fleet-bench: benchmark script is not executable: $bench_script" >&2; exit 3; }
[ -x "$gate_script" ] || { echo "fleet-bench: gate script is not executable: $gate_script" >&2; exit 3; }
[ -z "$($git_cmd -C "$ROOT" status --porcelain --untracked-files=normal)" ] || {
    echo 'fleet-bench: checkout must be clean before measuring' >&2
    exit 3
}
[ ! -e "$result" ] || { echo "fleet-bench: refusing to overwrite $result" >&2; exit 3; }
mkdir -p "$run_dir"
CGF_BENCH_RESULTS=$result "$bench_script"
target=$(sed -n 's/^target=//p' "$result")
[ -n "$target" ] && [ "$(printf '%s\n' "$target" | wc -l)" -eq 1 ] || {
    echo 'fleet-bench: result has no unique target provenance' >&2
    exit 3
}
self_file_count=$(sed -n 's/^self\.corpus=cgfried-src-[^:]*:\([0-9][0-9]*\)-files\(:.*\)\{0,1\}$/\1/p' "$result")
[ -n "$self_file_count" ] &&
    [ "$(printf '%s\n' "$self_file_count" | wc -l)" -eq 1 ] || {
    echo 'fleet-bench: result has no unique self file-count provenance' >&2
    exit 3
}
baseline=$ROOT/.benchmarks/baseline-$target.$host.txt
gate_status=0
if [ -r "$baseline" ]; then
    rss_status=0
    BENCH_SKIP_TIME=1 BENCH_GATE_KIND=rss \
        "$gate_script" "$baseline" "$result" || rss_status=$?
    case $rss_status in
        0) rss_result=pass ;;
        1) rss_result=trip ;;
        *) echo "fleet-bench: RSS gate infrastructure failure (status $rss_status)" >&2; exit "$rss_status" ;;
    esac

    time_status=0
    time_output=
    if time_output=$(BENCH_SKIP_TIME=0 BENCH_GATE_KIND=time \
        BENCH_ALLOW_PROVENANCE_ONLY=1 \
        "$gate_script" "$baseline" "$result"); then
        time_status=0
    else
        time_status=$?
    fi
    [ -z "$time_output" ] || printf '%s\n' "$time_output"
    case $time_status:$time_output in
    0:'benchmark_gate: pass ('*')') time_result=pass ;;
    0:'benchmark_gate: provenance-only (uncontrolled timing evidence)')
        time_result=provenance-only
        ;;
    1:*) time_result=trip ;;
    3:*)
        echo "fleet-bench: time gate infrastructure failure (status 3)" >&2
        exit 3
        ;;
    *)
        echo "fleet-bench: unexpected time gate result (status $time_status)" >&2
        exit 3
        ;;
    esac

    if [ "$rss_result" = trip ] || [ "$time_result" = trip ]; then
        gate_result=trip
        gate_status=1
    elif [ "$time_result" = provenance-only ]; then
        gate_result=provenance-only
        gate_status=0
    else
        gate_result=pass
        gate_status=0
    fi
else
    rss_result=warmup
    time_result=warmup
    gate_result=warmup
    echo "fleet-bench: compile benchmark warmup: host=$host target=$target baseline=missing; gate not run"
fi
{
    echo "fleet.host=$host"
    echo "fleet.run_id=$stamp-$host"
    echo 'fleet.baseline_mutated=no'
    echo "fleet.self_file_count=$self_file_count"
    echo "fleet.rss_gate=$rss_result"
    echo "fleet.time_gate=$time_result"
    echo "fleet.gate=$gate_result"
} >>"$result"
echo "fleet-bench: wrote $result"
if [ "$commit_result" -eq 1 ]; then
    relative_result=${result#"$ROOT"/}
    "$git_cmd" -C "$ROOT" add -- "$relative_result"
    "$git_cmd" -C "$ROOT" commit --only -m "Record Sprint 52 benchmark on $host" -- \
        "$relative_result"
    echo 'fleet-bench: dated result committed; baseline unchanged'
else
    echo 'fleet-bench: commit deferred to caller; baseline unchanged'
fi
exit "$gate_status"
