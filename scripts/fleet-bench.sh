#!/bin/sh
# Fleet measurement wrapper. It gates against the committed host-class
# baseline, then commits only the dated run artifact. Baselines are never
# mutated here: accepting a new baseline remains a separate reviewed commit.
set -eu
LC_ALL=C
export LC_ALL

ROOT=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
host=$(hostname -s 2>/dev/null || uname -n)
stamp=$(date -u '+%Y-%m-%dT%H%M%SZ')
run_dir=${CGF_FLEET_RUN_DIR:-$ROOT/.benchmarks/runs}
result=${CGF_FLEET_RESULT:-$run_dir/$stamp-$host.txt}

case $host in
    kasumi | hasu | nomad-1) ;;
    *) echo "fleet-bench: unsupported fleet host '$host'" >&2; exit 3 ;;
esac
case $result in
    "$ROOT"/.benchmarks/runs/*) ;;
    *) echo 'fleet-bench: result must be inside .benchmarks/runs' >&2; exit 3 ;;
esac
[ -d "$ROOT/.git" ] || { echo 'fleet-bench: a git checkout is required' >&2; exit 3; }
[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ] || {
    echo 'fleet-bench: checkout must be clean before measuring' >&2
    exit 3
}
[ ! -e "$result" ] || { echo "fleet-bench: refusing to overwrite $result" >&2; exit 3; }
mkdir -p "$run_dir"
CGF_BENCH_RESULTS=$result "$ROOT/scripts/bench.sh"
target=$(sed -n 's/^target=//p' "$result")
[ -n "$target" ] && [ "$(printf '%s\n' "$target" | wc -l)" -eq 1 ] || {
    echo 'fleet-bench: result has no unique target provenance' >&2
    exit 3
}
baseline=$ROOT/.benchmarks/baseline-$target.$host.txt
BENCH_SKIP_TIME=0 "$ROOT/scripts/benchmark_gate.sh" "$baseline" "$result"
{
    echo "fleet.host=$host"
    echo "fleet.run_id=$stamp-$host"
    echo 'fleet.baseline_mutated=no'
} >>"$result"
echo "fleet-bench: wrote $result"
relative_result=${result#"$ROOT"/}
git -C "$ROOT" add -- "$relative_result"
git -C "$ROOT" commit --only -m "Record Sprint 52 benchmark on $host" -- \
    "$relative_result"
echo 'fleet-bench: dated result committed; baseline unchanged'
