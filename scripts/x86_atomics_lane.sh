#!/bin/sh
# Sprint 49: the x86_64 atomics Sprint 25 left ICEing.
#
# `c += 3` on an `_Atomic int` is plain C11 and used to reach an internal
# compiler error, and a sequentially consistent STORE used to compile to a
# bare mov with no fence — silently wrong, since x86's store buffer lets a
# later load overtake it. Both are fixed; this lane proves the arithmetic.
#
# It runs through GNU as rather than the bundled assembler: afs-as has no
# xadd/cmpxchg/xchg-with-memory/mfence and no lock prefix (findings row
# F-S49-ATOMICS). Emission is correct either way, so the gap is upstream and
# recorded; when that PR lands this lane loses its CGF_AS=0.

set -eu

CGF=${1:-build/cgfried}
work=${CGF_ATOMICS_WORK:-build/x86-atomics}

case $("$CGF" -dumpmachine) in
x86_64-*) ;;
*)
    echo "x86_atomics_lane: skipped (host is not x86_64)"
    exit 0
    ;;
esac
if ! command -v as >/dev/null 2>&1; then
    echo "x86_atomics_lane: skipped (no GNU as)"
    exit 0
fi

mkdir -p "$work"
for opt in -O0 -O1 -O2 -Os; do
    CGF_AS=0 "$CGF" "$opt" tests/atomics/rmw_x86.c -o "$work/rmw$opt"
    out=$("$work/rmw$opt")
    if [ "$out" != OK ]; then
        echo "x86_atomics_lane: $opt disagreed with the C reference:" >&2
        echo "$out" >&2
        exit 1
    fi
done

# The fence is the part no single-threaded run can observe, so it is asserted
# in the TEXT: a seq_cst store must be followed by mfence.
CGF_AS=0 "$CGF" -S tests/atomics/rmw_x86.c -o "$work/rmw.s"
if ! grep -q mfence "$work/rmw.s"; then
    echo "x86_atomics_lane: no mfence after a sequentially consistent store" >&2
    exit 1
fi
if ! grep -q 'lock xadd' "$work/rmw.s"; then
    echo "x86_atomics_lane: fetch-add did not become a locked xadd" >&2
    exit 1
fi
if ! grep -q 'lock cmpxchg' "$work/rmw.s"; then
    echo "x86_atomics_lane: no locked cmpxchg for the and/or/xor loop" >&2
    exit 1
fi

# X64-C-01: -### is content-aware because 16-byte atomics acquire an implicit
# libatomic dependency only after in-process selection. Point both external
# tools at nonexistent paths: the plans must still succeed, proving planning
# invoked neither tool while matching the equivalent dynamic/static link.
CGF_AS_PATH="$work/no-such-as" CGF_LD_PATH="$work/no-such-ld" "$CGF" -### \
    tests/programs/lower-exec/exec_atomic_float_access.c \
    -o "$work/wide-dynamic" 2>"$work/wide-dynamic.plan"
CGF_AS_PATH="$work/no-such-as" CGF_LD_PATH="$work/no-such-ld" "$CGF" \
    -### -static \
    tests/programs/lower-exec/exec_atomic_float_access.c \
    -o "$work/wide-static" 2>"$work/wide-static.plan"
CGF_AS_PATH="$work/no-such-as" CGF_LD_PATH="$work/no-such-ld" "$CGF" -### \
    tests/atomics/rmw_x86.c \
    -o "$work/narrow" 2>"$work/narrow.plan"

# Planning may run the in-process frontend/backend to discover link
# dependencies, but it is still observational: development dumps and source
# fix-it copies are live-build products. Exercise both side-effect paths while
# the nonexistent tool routes continue to prove no assembler/linker is run.
plan_dump="$work/plan-only-dumps-$$"
plan_src="$work/plan-fixit-$$.c"
mkdir "$plan_dump"
cp tests/memsafe/autofix/copy.c "$plan_src"
CGF_DUMP_IR=all CGF_DUMP_IR_DIR="$plan_dump" \
    CGF_AS_PATH="$work/no-such-as" CGF_LD_PATH="$work/no-such-ld" \
    "$CGF" -### -Wmem-unbounded-copy -Wno-mem-leak \
    -fdiagnostics-parseable-fixits -fdiagnostics-apply-fixits=all \
    "$plan_src" -o "$work/plan-fixit" 2>"$work/plan-fixit.plan"
if [ "$(grep -c '^fix-it:' "$work/plan-fixit.plan")" -ne 2 ]; then
    echo "x86_atomics_lane: plan-only fix-it probe did not produce two edits" >&2
    exit 1
fi
if [ -n "$(find "$plan_dump" -mindepth 1 -print -quit)" ]; then
    echo "x86_atomics_lane: -### wrote phase-dump contents" >&2
    exit 1
fi
if [ -e "$plan_src.cgf-fixed" ]; then
    echo "x86_atomics_lane: -### wrote a .cgf-fixed source copy" >&2
    exit 1
fi
rm -f "$plan_src"
rmdir "$plan_dump"
CGF_AS=0 "$CGF" -v tests/programs/lower-exec/exec_atomic_float_access.c \
    -o "$work/wide-live" 2>"$work/wide-live.plan"
CGF_AS=0 "$CGF" -v -static \
    tests/programs/lower-exec/exec_atomic_float_access.c \
    -o "$work/wide-static-live" 2>"$work/wide-static-live.plan"
if ! grep -q -- '-latomic' "$work/wide-dynamic.plan" ||
   ! grep -q -- '-latomic' "$work/wide-static.plan" ||
   ! grep -q -- '-static' "$work/wide-static.plan" ||
   ! grep -q -- '-latomic' "$work/wide-live.plan" ||
   ! grep -q -- '-latomic' "$work/wide-static-live.plan"; then
    echo "x86_atomics_lane: wide -### plan omitted libatomic/static state" >&2
    exit 1
fi
if grep -q -- '-latomic' "$work/narrow.plan"; then
    echo "x86_atomics_lane: narrow -### plan added an unused libatomic" >&2
    exit 1
fi
if [ -e "$work/wide-dynamic" ] || [ -e "$work/wide-static" ] ||
   [ -e "$work/narrow" ] || [ -e "$work/plan-fixit" ]; then
    echo "x86_atomics_lane: -### produced an output file" >&2
    exit 1
fi
if [ "$("$work/wide-live")" != OK ] ||
   [ "$("$work/wide-static-live")" != OK ]; then
    echo "x86_atomics_lane: live wide atomic control failed" >&2
    exit 1
fi

echo "x86_atomics_lane: 4 opt levels and content-aware -### plans green;" \
    "lock xadd, lock cmpxchg and the seq_cst store fence all present"
