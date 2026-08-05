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

echo "x86_atomics_lane: 4 opt levels green; lock xadd, lock cmpxchg and the" \
    "seq_cst store fence all present"
