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
        echo 'profile: musl full-build profile is deferred until Sprint 57; use the measured source lanes today' >&2
        exit 3
        ;;
    *) echo 'usage: scripts/profile.sh {sqlite3|self|many-tu|musl}' >&2; exit 2 ;;
esac

data=$WORK/$lane.data
report=$WORK/$lane.report.txt
perf record -o "$data" -g --call-graph dwarf -- "$CGF" "$@"
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
} >"$WORK/$lane.provenance.txt"
echo "profile: wrote $data, $report, and provenance"
