#!/bin/sh
# Reproducible Sprint 52 profiling ritual.  Profile before optimizing; attach
# the before/after top-20 report summaries to the performance change.
set -eu
LC_ALL=C
export LC_ALL

ROOT=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
BUILD=${BUILD:-$ROOT/build}
CGF=${CGF_BENCH_CGF:-$BUILD/cgfried}
WORK=${CGF_PROFILE_WORK:-$BUILD/bench/profile}
SELF_LIMIT=${CGF_BENCH_SELF_LIMIT:-0}
lane=${1:-self}
profile_program=$CGF
profile_workload=compiler-direct

case $SELF_LIMIT in
    *[!0-9]* | '') echo 'profile: CGF_BENCH_SELF_LIMIT must be a nonnegative integer' >&2; exit 2 ;;
esac

command -v perf >/dev/null 2>&1 || { echo 'profile: perf is required' >&2; exit 3; }
[ -x "$CGF" ] || { echo "profile: compiler is not executable: $CGF" >&2; exit 3; }
mkdir -p "$WORK"

set --
case $lane in
    sqlite3)
        set -- -std=gnu17 -DSQLITE_DISABLE_INTRINSIC=1 -Wno-attributes \
            -Wno-mem -Wno-return-type -fsyntax-only \
            "$ROOT/tests/bench/corpus/sqlite3/sqlite3.c"
        ;;
    self)
        set -- -Wno-mem -Wno-return-type -fsyntax-only -I "$ROOT/src"
        seen=0
        SELF_MANIFEST=$WORK/self-sources.txt
        find "$ROOT/src" -type f -name '*.c' ! -path "$ROOT/src/rt/*" | \
            sort >"$SELF_MANIFEST"
        while IFS= read -r source; do
            if [ "$SELF_LIMIT" -ne 0 ] && [ "$seen" -ge "$SELF_LIMIT" ]; then
                break
            fi
            set -- "$@" "$source"
            seen=$((seen + 1))
        done <"$SELF_MANIFEST"
        ;;
    many-tu)
        MANY=$WORK/many-tu
        "$ROOT/scripts/gen-tu-corpus.sh" "$MANY" >/dev/null
        set -- -Wno-mem -Wno-return-type -fsyntax-only
        for source in "$MANY"/tu_*.c; do set -- "$@" "$source"; done
        ;;
    musl)
        MUSL_PIN=b306b16af15c89a04d8e0c55cac2dadbeb39c083
        MUSL_SOURCE=${CGF_PROFILE_MUSL_SOURCE:-$ROOT/.docs/refs/musl}
        MUSL_ONCE=${CGF_PROFILE_MUSL_ONCE:-$ROOT/scripts/musl-full-build-once.sh}
        MUSL_WRAPPER=${CGF_PROFILE_MUSL_WRAPPER:-$ROOT/scripts/campaigns/musl-cc.sh}
        MUSL_HOSTCC=${CGF_PROFILE_MUSL_HOSTCC:-gcc}
        [ -x "$MUSL_ONCE" ] || {
            echo "profile: musl build helper is not executable: $MUSL_ONCE" >&2
            exit 3
        }
        [ -x "$MUSL_WRAPPER" ] || {
            echo "profile: musl compiler wrapper is not executable: $MUSL_WRAPPER" >&2
            exit 3
        }
        musl_actual=$(git -C "$MUSL_SOURCE" rev-parse --verify HEAD 2>/dev/null || true)
        [ "$musl_actual" = "$MUSL_PIN" ] || {
            echo "profile: musl pin mismatch: expected $MUSL_PIN got ${musl_actual:-missing}" >&2
            exit 3
        }
        command -v "$MUSL_HOSTCC" >/dev/null 2>&1 || {
            echo "profile: host compiler is unavailable: $MUSL_HOSTCC" >&2
            exit 3
        }
        MUSL_PROFILE_ROOT=$(mktemp -d "$WORK/musl-build.XXXXXX") || {
            echo 'profile: cannot create a fresh musl profile workspace' >&2
            exit 3
        }
        CGF_MUSL_BUILD_WRAPPER=$MUSL_WRAPPER
        CGF_MUSL_BUILD_CGF=$CGF
        CGF_MUSL_BUILD_HOSTCC=$MUSL_HOSTCC
        export CGF_MUSL_BUILD_WRAPPER CGF_MUSL_BUILD_CGF
        export CGF_MUSL_BUILD_HOSTCC
        profile_program=$MUSL_ONCE
        profile_workload=musl-full-static-hybrid
        set -- --build "$MUSL_SOURCE" "$MUSL_PROFILE_ROOT/build"
        ;;
    *) echo 'usage: scripts/profile.sh {sqlite3|self|many-tu|musl}' >&2; exit 2 ;;
esac

data=$WORK/$lane.data
report=$WORK/$lane.report.txt
perf record -o "$data" -g --call-graph dwarf -- "$profile_program" "$@"
perf report -i "$data" --stdio --percent-limit 1 >"$report"
profile_rev=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal 2>/dev/null)" ]; then
    profile_tree=dirty
elif [ "$profile_rev" = unknown ]; then
    profile_tree=unavailable
else
    profile_tree=clean
fi
{
    echo "profile_lane=$lane"
    echo "profile_host=$(hostname 2>/dev/null || uname -n)"
    echo "profile_date=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "profile_cgf_rev=$profile_rev"
    echo "profile_cgf_tree=$profile_tree"
    echo "profile_workload=$profile_workload"
    if [ "$lane" = musl ]; then
        echo "profile_musl_commit=$MUSL_PIN"
        echo 'profile_musl_routes=1254-cgf-c,68-host-complex,32-host-assembler'
        echo 'profile_musl_protocol=fresh-tree,source-date-epoch-0,jobs-1'
    fi
} >"$WORK/$lane.provenance.txt"
echo "profile: wrote $data, $report, and provenance"
