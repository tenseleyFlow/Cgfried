#!/bin/sh
# Include-guard fast-path benchmark. Compares `cgf -E` with the fast path
# ON vs OFF (CGF_PP_GUARD_FASTPATH=0 — the permanent differential control)
# over N guarded headers included REPEATS times. Also asserts the two runs
# produce IDENTICAL output: the fast path must change nothing.
set -eu
LC_ALL=C
export LC_ALL

# NOTE ON THE CEILING: with REPEATS passes over N headers, the fast path
# can at best remove (REPEATS-1)/REPEATS of the tokenization work — the
# FIRST read of each header is real work and is irreducible. So the
# arithmetic ceiling is REPEATS-x, and measured speedup approaches it from
# below by the per-skip syscall/lookup cost. Sprint 7's ">5x" target is
# therefore only meaningful at REPEATS > 5; the default here is 20, which
# also better models real header amplification (stdio.h pulled in by
# dozens of headers across one TU). Measured on the dev box:
#   1000x4  off=171ms on=67ms  2x  (ceiling 4x)
#   1000x10 off=386ms on=83ms  4x  (ceiling 10x)
#   1000x20 off=777ms on=106ms 7x  (ceiling 20x)
CGF=${1:?usage: pp_include_bench.sh path/to/cgfried [count] [repeats]}
N=${2:-1000}
REPEATS=${3:-20}
WORK=$(dirname "$CGF")/pp-bench

sh tests/perf/gen_include_corpus.sh "$WORK" "$N" "$REPEATS"

CGF_PP_GUARD_FASTPATH=0 "$CGF" -E -I"$WORK" "$WORK/all.c" > "$WORK/off.out"
"$CGF" -E -I"$WORK" "$WORK/all.c" > "$WORK/on.out"
cmp "$WORK/off.out" "$WORK/on.out" || {
    echo "pp_bench: FAST PATH CHANGED OUTPUT — that is a bug, not a speedup" >&2
    exit 1
}

# This historical Sprint-7 microbenchmark deliberately retains its quick
# two-sample shell timer. The reproducible median/MAD harness for committed
# compile-speed baselines is tests/bench/timeit.c; this script is a focused
# include-guard diagnostic rather than a baseline gate.
time_it() {
    start=$(date +%s%N)
    env "$@" "$CGF" -E -I"$WORK" "$WORK/all.c" > /dev/null
    end=$(date +%s%N)
    echo $(((end - start) / 1000000))
}
off1=$(time_it CGF_PP_GUARD_FASTPATH=0)
off2=$(time_it CGF_PP_GUARD_FASTPATH=0)
on1=$(time_it CGF_PP_GUARD_FASTPATH=1)
on2=$(time_it CGF_PP_GUARD_FASTPATH=1)
off=$off1; [ "$off2" -lt "$off" ] && off=$off2
on=$on1;   [ "$on2" -lt "$on" ] && on=$on2
[ "$on" -eq 0 ] && on=1

echo "pp_bench: ${N} headers x ${REPEATS}: off=${off}ms on=${on}ms speedup=$((off / on))x (ceiling ${REPEATS}x)"
CGF_PP_STATS=1 "$CGF" -E -I"$WORK" "$WORK/all.c" > /dev/null
